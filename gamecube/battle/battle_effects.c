// Move effect engine of the GameCube battle core.
//
// A move is a list of effect descriptors (battle_protocol.h, request V8/V9).
// Older requests carry one effect byte per move, translated into the same
// descriptors, so every version runs through these handlers.
//
// Order for one action (battle_core.c runs the action checks, this file the move):
//   0. Action checks in battle_core.c: recharge, sleep/freeze, flinch,
//      confusion (self-hit), paralysis. Any of them interrupts the action
//      and cancels a charging move.
//   1. A charging move's first turn spends PP, emits MOVE_USED and CHARGE
//      and ends; its second turn (a forced action) spends no PP.
//   2. PP is spent and MOVE_USED is emitted.
//   3. Targets are resolved for the format (Triple distance, Rotation front).
//   4. A self-only handler (Protect, Substitute) runs once on the user.
//   5. For every target in field order:
//        Protect -> semi-invulnerable -> substitute (status moves) ->
//        accuracy -> type immunity ->
//        damaging move: hits (a substitute takes them until it breaks;
//                       multi-hit stops when the target faints), then drain,
//                       then the remaining effects in list order (chance
//                       rolls per target; target effects need the Pokemon
//                       itself to have been hit and to be standing), then
//                       force-out / pivot;
//        status move:   the effects in list order; NO_EFFECT if none applied.
//   6. Recoil once, from the HP all targets (and substitutes) lost; recharge.
// Faint resolution, replacement and pivot prompts stay in battle_core.c.
// The rules come from the request; the code is an independent implementation.

#include "battle_internal.h"

#include <string.h>

bool bc_move_has_effect(const BattleMove *m, uint8_t kind) {
    for (unsigned i = 0; i < m->effect_count; ++i)
        if (m->effects[i].kind == kind) return true;
    return false;
}

static const BattleEffect *find_effect(const BattleMove *m, uint8_t kind) {
    for (unsigned i = 0; i < m->effect_count; ++i)
        if (m->effects[i].kind == kind) return &m->effects[i];
    return NULL;
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
    const bool v9 = r->version >= BATTLE_REQUEST_VERSION_VOLATILES;
    if (!v9 && (m->move_flags & ~BMF_V8_MASK)) return false;
    const uint8_t target = m->target & BTARGET_CLASS_MASK;
    for (unsigned i = 0; i < m->effect_count; ++i) {
        const BattleEffect *e = &m->effects[i];
        if (!v9 && e->kind > BEFF_PROTECT) return false;
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
        case BEFF_SUBSTITUTE:
            if (target != BTARGET_SELF) return false;
            break;
        case BEFF_CHARGE:
            // The charging turn keeps one chosen target.
            if (target != BTARGET_SINGLE) return false;
            break;
        default:
            break;
        }
    }
    return true;
}

// ------------------------------------------------------------- volatile lifetime

void bc_volatiles_on(Battler *mon, VolatileEvent event) {
    BattlerVolatiles *v = &mon->vol;
    switch (event) {
    case VOL_TURN_START:
        v->protected_turn = false;
        v->acted = false;
        break;
    case VOL_TURN_END:
        v->flinched = false;
        if (v->recharge) --v->recharge;
        break;
    case VOL_SWITCH_OUT:  // normal switch, drag and pivot: everything leaves with the battler
    case VOL_FAINT:
        memset(v, 0, sizeof(*v));
        v->charge_slot = BATTLE_SLOT_NONE;
        break;
    case VOL_SHIFT:       // Triple Shift: same battler, same volatiles
    case VOL_ROTATE:      // Gen 5 rotation: no switch-out reset
        break;
    }
}

void bc_effects_reset_volatiles(Battler *mon) {
    bc_volatiles_on(mon, VOL_SWITCH_OUT);
}

void bc_effects_begin_turn(Battle *b) {
    for (unsigned side = 0; side < 2; ++side)
        for (unsigned i = 0; i < b->sides[side].count; ++i) bc_volatiles_on(&b->sides[side].battlers[i], VOL_TURN_START);
}

void bc_effects_end_turn(Battle *b) {
    for (unsigned side = 0; side < 2; ++side)
        for (unsigned i = 0; i < b->sides[side].count; ++i) bc_volatiles_on(&b->sides[side].battlers[i], VOL_TURN_END);
}

void bc_cancel_charge(Battle *b, Position user, EventLog *log) {
    Battler *mon = bc_mutable_at(b, user);
    if (!mon || mon->vol.charge_slot == BATTLE_SLOT_NONE) return;
    mon->vol.charge_slot = BATTLE_SLOT_NONE;
    mon->vol.semi_invulnerable = BSEMI_NONE;
    bc_emit(log, (BattleEvent){.kind = EV_CHARGE_END, .user = user, .target = user});
}

// ------------------------------------------------------------- action checks

bool bc_confusion_check(Battle *b, Position user, EventLog *log) {
    Battler *mon = bc_mutable_at(b, user);
    if (!mon->vol.confusion) return false;
    if (--mon->vol.confusion == 0) {
        bc_emit(log, (BattleEvent){.kind = EV_CONFUSION_END, .user = user, .target = user});
        return false;
    }
    bc_emit(log, (BattleEvent){.kind = EV_CONFUSED, .user = user, .target = user});
    const BattleVolatileRules *rules = &b->request->volatiles;
    if (bc_next_random(b) % 100u >= rules->confusion_hit_percent) return false;
    // Typeless physical self-hit with the user's own stages; no random factor,
    // STAB or type; burn only if the rules say so. Substitutes do not take it.
    const unsigned atk = bc_stage_scale(mon->attack, mon->stat_stages[BSTAT_ATK], 2);
    const unsigned def = bc_stage_scale(mon->defense, mon->stat_stages[BSTAT_DEF], 2);
    unsigned damage = (2u * mon->level / 5u + 2u) * rules->confusion_power * atk / (def ? def : 1) / 50u + 2u;
    if ((rules->flags & BVR_CONFUSION_BURN) && bc_status_kind(b, mon) == BST_BURN) damage /= 2;
    if (!damage) damage = 1;
    bc_apply_damage(b, (BattleEvent){.kind = EV_CONFUSION_HIT, .user = user, .target = user},
                    (uint16_t)(damage > 0xffff ? 0xffff : damage), log);
    return true;
}

// ------------------------------------------------------------- handlers

typedef struct {
    Battle *b;
    EventLog *log;
    Position user;
    const BattleMove *move;
    uint16_t id;
    bool spread;
    unsigned lost;   // HP all targets and substitutes lost, for recoil
    bool landed;     // any target was hit
    bool pivot;
} MoveContext;

static Position subject(const MoveContext *c, const BattleEffect *e, Position target) {
    return e->who == BWHO_USER ? c->user : target;
}

static bool same(Position a, Position b) { return a.side == b.side && a.slot == b.slot; }

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

static void fail(MoveContext *c, Position target) {
    bc_emit(c->log, (BattleEvent){.kind = EV_FAIL, .user = c->user, .target = target, .move_id = c->id});
}

static bool apply_heal(MoveContext *c, const BattleEffect *e, Position target) {
    const Battler *mon = battle_at(c->b, target);
    if (!restore(c, target, (unsigned)mon->max_hp * e->arg0 / e->arg2, EV_HEAL)) fail(c, target);
    return true;  // a failure message is the result
}

static bool apply_confuse(MoveContext *c, Position target) {
    Battler *mon = bc_mutable_at(c->b, target);
    if (mon->vol.confusion) return false;
    const BattleVolatileRules *rules = &c->b->request->volatiles;
    mon->vol.confusion = (uint8_t)(rules->confusion_min +
                                   bc_next_random(c->b) % (rules->confusion_max - rules->confusion_min + 1u));
    bc_emit(c->log, (BattleEvent){.kind = EV_CONFUSE, .user = c->user, .target = target, .move_id = c->id});
    return true;
}

static bool apply_flinch(MoveContext *c, Position target) {
    Battler *mon = bc_mutable_at(c->b, target);
    // Only an action still to come this turn can be lost.
    if (mon->vol.acted || mon->vol.flinched) return false;
    mon->vol.flinched = true;
    return true;
}

static bool apply_substitute(MoveContext *c, Position target) {
    Battler *mon = bc_mutable_at(c->b, target);
    unsigned cost = mon->max_hp / c->b->request->volatiles.substitute_divisor;
    if (!cost) cost = 1;
    if (mon->vol.substitute_hp || mon->hp <= cost) {
        fail(c, target);
        return true;
    }
    mon->hp = (uint16_t)(mon->hp - cost);
    mon->vol.substitute_hp = (uint16_t)cost;
    bc_emit(c->log, (BattleEvent){.kind = EV_SUBSTITUTE, .user = c->user, .target = target, .move_id = c->id,
                                  .amount = (uint16_t)cost, .hp_after = mon->hp});
    return true;
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
    case BEFF_FORCE_OUT: return bc_drag_out(c->b, who, c->log);
    case BEFF_PIVOT: c->pivot = true; return true;
    case BEFF_CONFUSE: return apply_confuse(c, who);
    case BEFF_FLINCH: return apply_flinch(c, who);
    case BEFF_SUBSTITUTE: return apply_substitute(c, who);
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

static bool behind_substitute(const MoveContext *c, Position target) {
    return !same(target, c->user) && battle_at(c->b, target)->vol.substitute_hp &&
           !(c->move->move_flags & BMF_IGNORES_SUBSTITUTE);
}

// Semi-invulnerable targets: only moves flagged for that state reach them.
static bool out_of_reach(const MoveContext *c, Position target) {
    if (same(target, c->user)) return false;
    switch (battle_at(c->b, target)->vol.semi_invulnerable) {
    case BSEMI_AIR: return !(c->move->move_flags & (BMF_HITS_AIRBORNE | BMF_DOUBLE_AIRBORNE));
    case BSEMI_UNDERGROUND: return !(c->move->move_flags & (BMF_HITS_UNDERGROUND | BMF_DOUBLE_UNDERGROUND));
    default: return false;
    }
}

static bool doubled(const MoveContext *c, const Battler *target) {
    return (target->vol.semi_invulnerable == BSEMI_AIR && (c->move->move_flags & BMF_DOUBLE_AIRBORNE)) ||
           (target->vol.semi_invulnerable == BSEMI_UNDERGROUND && (c->move->move_flags & BMF_DOUBLE_UNDERGROUND));
}

// Damage for one target. Returns the HP the target and its substitute lost;
// *struck tells whether the Pokemon itself took a hit.
static unsigned strike(MoveContext *c, const BattleEffect *damage, Position target, bool *landed, bool *struck) {
    Battle *b = c->b;
    BattleEvent ev = {.user = c->user, .target = target, .move_id = c->id};
    *landed = *struck = false;
    if (bc_missed(b, c->user, target, c->move)) {
        ev.kind = EV_MISS;
        bc_emit(c->log, ev);
        return 0;
    }
    unsigned lost = 0, hits = 0;
    unsigned total = damage->arg2 > 1 ? 0 : 1;  // multi-hit counts are rolled after the first damage roll
    const bool twice = doubled(c, battle_at(b, target));
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
        if (twice) amount = (uint16_t)(amount > 0x7fff ? 0xffff : amount * 2u);
        ++hits;
        *landed = true;
        if (behind_substitute(c, target)) {
            Battler *mon = bc_mutable_at(b, target);
            const uint16_t taken = amount < mon->vol.substitute_hp ? amount : mon->vol.substitute_hp;
            mon->vol.substitute_hp = (uint16_t)(mon->vol.substitute_hp - taken);
            lost += taken;
            bc_emit(c->log, (BattleEvent){.kind = EV_SUBSTITUTE_HIT, .user = c->user, .target = target,
                                          .move_id = c->id, .amount = taken, .hp_after = mon->vol.substitute_hp,
                                          .effectiveness = ev.effectiveness});
            if (!mon->vol.substitute_hp) {
                bc_emit(c->log, (BattleEvent){.kind = EV_SUBSTITUTE_BREAK, .user = c->user, .target = target,
                                              .move_id = c->id});
                if (!(b->request->volatiles.flags & BVR_SUBSTITUTE_MULTI_HIT_CONTINUES)) break;
            }
        } else {
            const uint16_t before = defender->hp;
            ev.kind = EV_DAMAGE;
            bc_apply_damage(b, ev, amount, c->log);
            lost += before - battle_at(b, target)->hp;
            *struck = true;
        }
        if (h + 1 >= total || battle_at(b, target)->fainted) break;
    }
    if (total > 1)
        bc_emit(c->log, (BattleEvent){.kind = EV_MULTI_HIT, .user = c->user, .target = target, .move_id = c->id,
                                      .amount = (uint16_t)hits});
    const Battler *defender = battle_at(b, target);
    if (*struck && !defender->fainted && bc_status_kind(b, defender) == BST_FREEZE &&
        (b->request->conditions.flags & BCR_THAW_ON_FIRE_HIT) && c->move->type == b->request->conditions.fire_type)
        bc_cure_status(bc_mutable_at(b, target), ev, c->log);
    return lost;
}

static bool blocked_by_protect(MoveContext *c, Position target) {
    if (same(target, c->user)) return false;
    if (!battle_at(c->b, target)->vol.protected_turn || (c->move->move_flags & BMF_IGNORES_PROTECT)) return false;
    bc_emit(c->log, (BattleEvent){.kind = EV_PROTECTED, .user = c->user, .target = target, .move_id = c->id});
    return true;
}

static bool evaded(MoveContext *c, Position target) {
    if (!out_of_reach(c, target)) return false;
    bc_emit(c->log, (BattleEvent){.kind = EV_MISS, .user = c->user, .target = target, .move_id = c->id});
    return true;
}

static void damaging_target(MoveContext *c, const BattleEffect *damage, Position target) {
    if (blocked_by_protect(c, target) || evaded(c, target)) return;
    bool landed, struck;
    const unsigned lost = strike(c, damage, target, &landed, &struck);
    if (!landed) return;
    c->landed = true;
    c->lost += lost;
    for (unsigned i = 0; i < c->move->effect_count; ++i) {
        const BattleEffect *e = &c->move->effects[i];
        switch (e->kind) {
        case BEFF_DAMAGE:
        case BEFF_RECOIL:
        case BEFF_RECHARGE:
        case BEFF_CHARGE:
            break;
        case BEFF_DRAIN:  // from the HP the target or its substitute lost
            if (lost) restore(c, c->user, lost * e->arg0 / 100u, EV_DRAIN);
            break;
        case BEFF_PIVOT:
            apply(c, e, target);
            break;
        case BEFF_FORCE_OUT:
            if (struck) apply(c, e, target);
            break;
        default:
            // Secondary effects: rolled per target. Effects on the target need
            // the Pokemon itself to have been hit (not only its substitute)
            // and to be standing; self effects always apply.
            if (e->who == BWHO_TARGET && (!struck || battle_at(c->b, target)->fainted)) break;
            if (roll(c, e)) apply(c, e, target);
            break;
        }
    }
}

static void status_target(MoveContext *c, Position target) {
    if (blocked_by_protect(c, target) || evaded(c, target)) return;
    if (behind_substitute(c, target)) {
        fail(c, target);
        return;
    }
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
    for (unsigned i = 0; i < mon->vol.protect_uses && i < 3; ++i) odds *= e->arg0;
    if (last_to_move(c->b) || (odds > 1 && bc_next_random(c->b) % odds)) {
        mon->vol.protect_uses = 0;
        fail(c, c->user);
        return;
    }
    ++mon->vol.protect_uses;
    mon->vol.protected_turn = true;
    bc_emit(c->log, (BattleEvent){.kind = EV_PROTECT, .user = c->user, .target = c->user, .move_id = c->id});
}

bool bc_execute_move(Battle *b, Position user, uint8_t slot, Position selected, EventLog *log) {
    Battler *a = bc_mutable_at(b, user);
    const BattleMove *m = &a->moves[slot];
    const uint16_t id = a->move_ids[slot];
    const uint8_t foe = 1 - user.side;
    const BattleEffect *charge = find_effect(m, BEFF_CHARGE);
    const bool releasing = charge && a->vol.charge_slot == slot;
    if (!bc_move_has_effect(m, BEFF_PROTECT)) a->vol.protect_uses = 0;
    if (releasing) {  // second turn: the stored target, no PP
        selected = a->vol.charge_target;
        a->vol.charge_slot = BATTLE_SLOT_NONE;
        a->vol.semi_invulnerable = BSEMI_NONE;
    }
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
    if (!releasing) --a->pp[slot];
    bc_emit(log, (BattleEvent){.kind = EV_MOVE_USED, .user = user, .target = first, .move_id = id});
    if (charge && !releasing) {  // first turn: charge (and maybe leave the field)
        a->vol.charge_slot = slot;
        a->vol.charge_target = first;
        a->vol.semi_invulnerable = charge->arg0;
        bc_emit(log, (BattleEvent){.kind = EV_CHARGE, .user = user, .target = user, .move_id = id,
                                   .condition = charge->arg0});
        return false;
    }
    if (!count) {
        bc_emit(log, (BattleEvent){.kind = EV_NO_EFFECT, .user = user, .target = first, .move_id = id});
        return false;
    }
    MoveContext c = {.b = b, .log = log, .user = user, .move = m, .id = id};
    const BattleEffect *damage = find_effect(m, BEFF_DAMAGE), *recoil = find_effect(m, BEFF_RECOIL);
    if (find_effect(m, BEFF_PROTECT)) {
        protect(&c, find_effect(m, BEFF_PROTECT));
        return false;
    }
    if (find_effect(m, BEFF_SUBSTITUTE)) {
        apply_substitute(&c, user);
        return false;
    }
    c.spread = damage && count > 1;
    bool all_fainted = true;
    for (uint8_t side = 0; side < 2; ++side)
        for (uint8_t s = 0; s < BC_MAX_ACTIVE; ++s) {
            if (!(targets[side] & (1u << s))) continue;
            const Position target = {side, s};
            const Battler *mon = battle_at(b, target);
            if (!mon || (mon->fainted && !same(target, user))) continue;
            if (damage) damaging_target(&c, damage, target);
            else status_target(&c, target);
            if (!battle_at(b, target)->fainted) all_fainted = false;
        }
    if (recoil && c.lost && !battle_at(b, user)->fainted) {
        const unsigned amount = c.lost * recoil->arg0 / 100u;
        bc_apply_damage(b, (BattleEvent){.kind = EV_RECOIL, .user = user, .target = user, .move_id = id},
                        (uint16_t)(amount ? amount : 1), log);
    }
    if (c.landed && find_effect(m, BEFF_RECHARGE) && !battle_at(b, user)->fainted &&
        !(all_fainted && (b->request->volatiles.flags & BVR_RECHARGE_SKIP_ON_KO)))
        bc_mutable_at(b, user)->vol.recharge = 2;  // blocks the next turn, ticks down at turn ends
    return c.pivot;
}
