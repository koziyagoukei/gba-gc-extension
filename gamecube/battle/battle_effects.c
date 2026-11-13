// Move effect engine of the GameCube battle core.
//
// A move is a list of effect descriptors (battle_protocol.h, request V8).
// Older requests carry one effect byte per move, translated into the same
// descriptors, so every version runs through these handlers.
//
// Order for one move:
//   1. PP is spent and MOVE_USED is emitted (battle_core.c already checked
//      that the action is legal and that the user is able to act).
//   2. Targets are resolved for the format (Triple distance, Rotation front).
//   3. A self-only handler (Protect) runs once on the user and ends the move.
//   4. For every target in field order:
//        Protect check -> accuracy -> type immunity ->
//        damaging move: hits (multi-hit stops when the target faints),
//                       then drain, then the remaining effects in list order
//                       (chance rolls per target; effects on a fainted target
//                       are skipped), then force-out / pivot;
//        status move:   the effects in list order; NO_EFFECT if none applied.
//   5. Recoil once, from the HP all targets lost.
// Faint resolution, replacement and pivot prompts stay in battle_core.c.
// The rules come from the request; the code is an independent implementation.

#include "battle_internal.h"

#include <string.h>

bool bc_move_has_effect(const BattleMove *m, uint8_t kind) {
    for (unsigned i = 0; i < m->effect_count; ++i)
        if (m->effects[i].kind == kind) return true;
    return false;
}

static void add(BattleMove *m, BattleEffect e) {
    if (m->effect_count < BATTLE_MAX_EFFECTS) m->effects[m->effect_count++] = e;
}

void bc_translate_legacy_effects(BattleMove *m) {
    m->effect_count = 0;
    m->move_flags = 0;
    m->source_effect = 0;
    const uint8_t effect = m->effect;
    if (m->category != BCAT_STATUS && m->power)
        add(m, (BattleEffect){.kind = BEFF_DAMAGE, .arg0 = 1, .arg2 = 1});
    if (effect == BME_FORCE_OUT || effect == BME_FORCE_OUT_ON_HIT) {
        add(m, (BattleEffect){.kind = BEFF_FORCE_OUT});
    } else if (effect == BME_PIVOT) {
        add(m, (BattleEffect){.kind = BEFF_PIVOT});
    } else if (effect >= BME_STAGE_BASE && effect < BME_STATUS_BASE) {
        const int encoded = effect - BME_STAGE_BASE, offset = encoded % 12;
        add(m, (BattleEffect){.kind = BEFF_STAT, .arg0 = (uint8_t)(encoded / 12),
                              .arg1 = (int8_t)(offset < 6 ? offset - 6 : offset - 5)});
    } else if (effect == BME_REFRESH) {
        add(m, (BattleEffect){.kind = BEFF_CURE,
                              .arg0 = 1u << BST_POISON | 1u << BST_TOXIC | 1u << BST_BURN | 1u << BST_PARALYSIS});
    } else if (effect == BME_ELECTRIC_PARALYSIS) {
        add(m, (BattleEffect){.kind = BEFF_STATUS, .arg0 = BST_PARALYSIS, .flags = BEFL_TYPE_CHART});
    } else if (effect > BME_STATUS_BASE && effect <= BME_STATUS_BASE + BST_FROSTBITE) {
        add(m, (BattleEffect){.kind = BEFF_STATUS, .arg0 = effect - BME_STATUS_BASE});
    }
}

bool bc_effects_supported(const BattleRequest *r, const BattleMove *m) {
    if (!battle_move_effects_valid(m)) return false;
    const uint8_t target = m->target & BTARGET_CLASS_MASK;
    for (unsigned i = 0; i < m->effect_count; ++i) {
        const BattleEffect *e = &m->effects[i];
        switch (e->kind) {
        case BEFF_STATUS:
            if (e->arg0 == BST_FROSTBITE && !(r->conditions.flags & BCR_FROSTBITE)) return false;
            if (e->arg0 == BST_FREEZE && (r->conditions.flags & BCR_FROSTBITE)) return false;
            break;
        case BEFF_FORCE_OUT:
        case BEFF_PIVOT:
            // Trainer battles only: the target must have a team to switch in.
            if (r->battle_type != BTYPE_TRAINER || target != BTARGET_SINGLE) return false;
            break;
        case BEFF_PROTECT:
        case BEFF_HEAL:
            if (target != BTARGET_SELF) return false;
            break;
        default:
            break;
        }
    }
    return true;
}

void bc_effects_reset_volatiles(Battler *mon) {
    mon->protected_turn = false;
    mon->protect_uses = 0;
}

void bc_effects_begin_turn(Battle *b) {
    for (unsigned side = 0; side < 2; ++side)
        for (unsigned i = 0; i < b->sides[side].count; ++i) b->sides[side].battlers[i].protected_turn = false;
}

// ------------------------------------------------------------- handlers

typedef struct {
    Battle *b;
    EventLog *log;
    Position user;
    const BattleMove *move;
    uint16_t id;
    bool spread;
    unsigned lost;   // HP all targets lost, for recoil
    bool pivot;
} MoveContext;

static Position subject(const MoveContext *c, const BattleEffect *e, Position target) {
    return e->who == BWHO_USER ? c->user : target;
}

static bool roll(MoveContext *c, const BattleEffect *e) {
    return !e->chance || bc_next_random(c->b) % 100u < e->chance;
}

static bool apply_stat(MoveContext *c, const BattleEffect *e, Position target) {
    Battler *mon = bc_mutable_at(c->b, target);
    int stage = mon->stat_stages[e->arg0] + e->arg1;
    if (stage < -6) stage = -6;
    if (stage > 6) stage = 6;
    if (stage == mon->stat_stages[e->arg0]) return false;
    mon->stat_stages[e->arg0] = (int8_t)stage;
    bc_emit(c->log, (BattleEvent){.kind = EV_STAGE, .user = c->user, .target = target, .move_id = c->id,
                                  .stat = e->arg0, .stage = (int8_t)stage});
    return true;
}

static bool apply_status(MoveContext *c, const BattleEffect *e, Position target) {
    Battler *mon = bc_mutable_at(c->b, target);
    const BattleConditionRules *rules = &c->b->request->conditions;
    const uint8_t kind = e->arg0;
    if (mon->status || bc_immune_to_status(rules, mon, kind) ||
        ((e->flags & BEFL_TYPE_CHART) && !bc_effectiveness(c->b, c->move->type, mon)))
        return false;
    const unsigned counter = kind == BST_SLEEP ?
        rules->sleep_min + bc_next_random(c->b) % (rules->sleep_max - rules->sleep_min + 1u) : 0;
    mon->status = BATTLE_CONDITION(kind, counter);
    mon->sleep_start = (uint8_t)counter;
    bc_emit(c->log, (BattleEvent){.kind = EV_STATUS, .user = c->user, .target = target, .move_id = c->id,
                                  .condition = kind});
    return true;
}

static bool apply_cure(MoveContext *c, const BattleEffect *e, Position target) {
    Battler *mon = bc_mutable_at(c->b, target);
    const uint8_t kind = bc_status_kind(c->b, mon);
    if (!kind || !(e->arg0 & (1u << kind))) return false;
    bc_cure_status(mon, (BattleEvent){.user = c->user, .target = target, .move_id = c->id}, c->log);
    return true;
}

static bool restore(MoveContext *c, Position target, unsigned amount, EventKind kind) {
    Battler *mon = bc_mutable_at(c->b, target);
    if (mon->fainted || mon->hp >= mon->max_hp) return false;
    if (!amount) amount = 1;
    const unsigned room = mon->max_hp - mon->hp;
    if (amount > room) amount = room;
    mon->hp = (uint16_t)(mon->hp + amount);
    bc_emit(c->log, (BattleEvent){.kind = kind, .user = c->user, .target = target, .move_id = c->id,
                                  .amount = (uint16_t)amount, .hp_after = mon->hp});
    return true;
}

static bool apply_heal(MoveContext *c, const BattleEffect *e, Position target) {
    const Battler *mon = battle_at(c->b, target);
    if (restore(c, target, (unsigned)mon->max_hp * e->arg0 / e->arg2, EV_HEAL)) return true;
    bc_emit(c->log, (BattleEvent){.kind = EV_FAIL, .user = c->user, .target = target, .move_id = c->id});
    return true;  // a failure message is the result
}

static bool apply_force_out(MoveContext *c, Position target) {
    return bc_drag_out(c->b, target, c->log);
}

// One effect of a status move or a secondary effect of a landed hit.
static bool apply(MoveContext *c, const BattleEffect *e, Position target) {
    const Position who = subject(c, e, target);
    const Battler *mon = battle_at(c->b, who);
    if (!mon || mon->fainted) return false;
    switch (e->kind) {
    case BEFF_STAT: return apply_stat(c, e, who);
    case BEFF_STATUS: return apply_status(c, e, who);
    case BEFF_CURE: return apply_cure(c, e, who);
    case BEFF_HEAL: return apply_heal(c, e, who);
    case BEFF_FORCE_OUT: return apply_force_out(c, who);
    case BEFF_PIVOT: c->pivot = true; return true;
    default: return false;
    }
}

static unsigned roll_hits(Battle *b, const BattleEffect *d) {
    if (d->arg0 == d->arg2) return d->arg0;
    switch (d->arg1) {
    case BHITS_2TO5_GEN5: {  // 35% / 35% / 15% / 15%
        const unsigned r = bc_next_random(b) % 20u;
        return r < 7 ? 2 : r < 14 ? 3 : r < 17 ? 4 : 5;
    }
    case BHITS_2TO5_GEN1: {  // 37.5% / 37.5% / 12.5% / 12.5%
        const unsigned r = bc_next_random(b) % 8u;
        return r < 3 ? 2 : r < 6 ? 3 : r < 7 ? 4 : 5;
    }
    default:
        return d->arg0 + bc_next_random(b) % (unsigned)(d->arg2 - d->arg0 + 1);
    }
}

// Damage for one target; returns the HP it lost (0 if nothing landed).
static unsigned strike(MoveContext *c, const BattleEffect *damage, Position target, bool *landed) {
    Battle *b = c->b;
    BattleEvent ev = {.user = c->user, .target = target, .move_id = c->id};
    *landed = false;
    if (bc_missed(b, c->user, target, c->move)) {
        ev.kind = EV_MISS;
        bc_emit(c->log, ev);
        return 0;
    }
    unsigned lost = 0, hits = 0;
    const unsigned planned = damage->arg2 > 1 ? 0 : 1;  // rolled after the first damage roll
    unsigned total = planned;
    for (unsigned h = 0; ; ++h) {
        const Battler *defender = battle_at(b, target);
        uint16_t amount = bc_calculate_damage(b, battle_at(b, c->user), c->move, defender,
                                              85u + bc_next_random(b) % 16u, false, &ev.effectiveness);
        if (!ev.effectiveness) {
            ev.kind = EV_NO_EFFECT;
            bc_emit(c->log, ev);
            return 0;
        }
        if (!total) total = roll_hits(b, damage);
        if (c->spread) {
            amount = (uint16_t)(amount * 3u / 4u);
            if (!amount) amount = 1;
        }
        const uint16_t before = defender->hp;
        ev.kind = EV_DAMAGE;
        bc_apply_damage(b, ev, amount, c->log);
        lost += before - battle_at(b, target)->hp;
        ++hits;
        *landed = true;
        if (h + 1 >= total || battle_at(b, target)->fainted) break;
    }
    if (total > 1)
        bc_emit(c->log, (BattleEvent){.kind = EV_MULTI_HIT, .user = c->user, .target = target, .move_id = c->id,
                                      .amount = (uint16_t)hits});
    const Battler *defender = battle_at(b, target);
    if (!defender->fainted && bc_status_kind(b, defender) == BST_FREEZE &&
        (b->request->conditions.flags & BCR_THAW_ON_FIRE_HIT) && c->move->type == b->request->conditions.fire_type)
        bc_cure_status(bc_mutable_at(b, target), ev, c->log);
    return lost;
}

static bool blocked_by_protect(MoveContext *c, Position target) {
    if (target.side == c->user.side && target.slot == c->user.slot) return false;
    if (!battle_at(c->b, target)->protected_turn || (c->move->move_flags & BMF_IGNORES_PROTECT)) return false;
    bc_emit(c->log, (BattleEvent){.kind = EV_PROTECTED, .user = c->user, .target = target, .move_id = c->id});
    return true;
}

static void damaging_target(MoveContext *c, const BattleEffect *damage, Position target) {
    if (blocked_by_protect(c, target)) return;
    bool landed;
    const unsigned lost = strike(c, damage, target, &landed);
    if (!landed) return;
    c->lost += lost;
    for (unsigned i = 0; i < c->move->effect_count; ++i) {
        const BattleEffect *e = &c->move->effects[i];
        switch (e->kind) {
        case BEFF_DAMAGE:
        case BEFF_RECOIL:
            break;
        case BEFF_DRAIN:
            if (lost) restore(c, c->user, lost * e->arg0 / 100u, EV_DRAIN);
            break;
        case BEFF_FORCE_OUT:
        case BEFF_PIVOT:
            apply(c, e, target);
            break;
        default:
            // Secondary effects: rolled per target, never on a fainted target.
            if (e->who == BWHO_TARGET && battle_at(c->b, target)->fainted) break;
            if (roll(c, e)) apply(c, e, target);
            break;
        }
    }
}

static void status_target(MoveContext *c, Position target) {
    if (blocked_by_protect(c, target)) return;
    BattleEvent ev = {.kind = EV_NO_EFFECT, .user = c->user, .target = target, .move_id = c->id};
    if (bc_missed(c->b, c->user, target, c->move)) {
        ev.kind = EV_MISS;
        bc_emit(c->log, ev);
        return;
    }
    bool applied = false;
    for (unsigned i = 0; i < c->move->effect_count; ++i) applied |= apply(c, &c->move->effects[i], target);
    if (!applied) bc_emit(c->log, ev);
}

static bool last_to_move(const Battle *b) {
    const TurnResolution *turn = &b->resolution;
    for (unsigned i = turn->next; i < turn->count; ++i) {
        const TurnAction *a = &turn->actions[i];
        const Battler *mon = &b->sides[a->action.user.side].battlers[a->battler];
        if (!mon->fainted) return false;
    }
    return true;
}

static void protect(MoveContext *c, const BattleEffect *e) {
    Battler *mon = bc_mutable_at(c->b, c->user);
    unsigned odds = 1;
    for (unsigned i = 0; i < mon->protect_uses && i < 3; ++i) odds *= e->arg0;
    if (last_to_move(c->b) || (odds > 1 && bc_next_random(c->b) % odds)) {
        mon->protect_uses = 0;
        bc_emit(c->log, (BattleEvent){.kind = EV_FAIL, .user = c->user, .target = c->user, .move_id = c->id});
        return;
    }
    ++mon->protect_uses;
    mon->protected_turn = true;
    bc_emit(c->log, (BattleEvent){.kind = EV_PROTECT, .user = c->user, .target = c->user, .move_id = c->id});
}

bool bc_execute_move(Battle *b, Position user, uint8_t slot, Position selected, EventLog *log) {
    Battler *a = bc_mutable_at(b, user);
    const BattleMove *m = &a->moves[slot];
    const uint16_t id = a->move_ids[slot];
    const uint8_t foe = 1 - user.side;
    if (!bc_move_has_effect(m, BEFF_PROTECT)) a->protect_uses = 0;
    uint8_t targets[2] = {battle_target_mask(b, user, m, 0), battle_target_mask(b, user, m, 1)};
    if ((m->target & BTARGET_CLASS_MASK) == BTARGET_SINGLE) {
        const uint8_t chosen = 1u << selected.slot;
        if (targets[foe]) targets[foe] &= targets[foe] & chosen ? chosen : 1u << bc_first_target(targets[foe]);
    }
    unsigned count = 0;
    Position first = {foe, selected.slot};
    for (uint8_t side = 0; side < 2; ++side)
        for (uint8_t s = 0; s < BC_MAX_ACTIVE; ++s)
            if (targets[side] & (1u << s))
                if (count++ == 0) first = (Position){side, s};
    --a->pp[slot];
    bc_emit(log, (BattleEvent){.kind = EV_MOVE_USED, .user = user, .target = first, .move_id = id});
    if (!count) {
        bc_emit(log, (BattleEvent){.kind = EV_NO_EFFECT, .user = user, .target = first, .move_id = id});
        return false;
    }
    MoveContext c = {.b = b, .log = log, .user = user, .move = m, .id = id};
    const BattleEffect *damage = NULL, *recoil = NULL;
    for (unsigned i = 0; i < m->effect_count; ++i) {
        if (m->effects[i].kind == BEFF_DAMAGE) damage = &m->effects[i];
        if (m->effects[i].kind == BEFF_RECOIL) recoil = &m->effects[i];
        if (m->effects[i].kind == BEFF_PROTECT) {
            protect(&c, &m->effects[i]);
            return false;
        }
    }
    c.spread = damage && count > 1;
    for (uint8_t side = 0; side < 2; ++side)
        for (uint8_t s = 0; s < BC_MAX_ACTIVE; ++s) {
            if (!(targets[side] & (1u << s))) continue;
            const Position target = {side, s};
            const Battler *mon = battle_at(b, target);
            if (!mon || (mon->fainted && !(side == user.side && s == user.slot))) continue;
            if (damage) damaging_target(&c, damage, target);
            else status_target(&c, target);
        }
    if (recoil && c.lost && !battle_at(b, user)->fainted) {
        const unsigned amount = c.lost * recoil->arg0 / 100u;
        bc_apply_damage(b, (BattleEvent){.kind = EV_RECOIL, .user = user, .target = user, .move_id = id},
                        (uint16_t)(amount ? amount : 1), log);
    }
    return c.pivot;
}
