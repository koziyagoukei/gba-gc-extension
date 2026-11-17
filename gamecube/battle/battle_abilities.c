// Ability hooks of the GameCube battle core (request V11, docs/abilities.md).
//
// Each Pokemon carries one ability descriptor resolved by the GBA. The hooks
// below are called from fixed points of the move order (battle_effects.c)
// and of the turn (battle_core.c). Hooks that modify a move effect run
// inside that effect's handler, so they inherit its V10 unit; they never
// decide how often the effect runs.

#include "battle_internal.h"

#include <stddef.h>

bool bc_abilities_enabled(const Battle *b) {
    return b->request->version >= BATTLE_REQUEST_VERSION_ABILITIES && !(b->request->rules & RULE_IGNORE_ABILITIES);
}

const BattleAbility *bc_ability(const Battle *b, const Battler *mon, uint8_t kind) {
    if (!mon || !mon->entry || !bc_abilities_enabled(b)) return NULL;
    const BattleAbility *a = &mon->entry->mon.ability;
    return a->kind == kind ? a : NULL;
}

static const BattleAbility *at(const Battle *b, Position p, uint8_t kind) {
    return bc_ability(b, battle_at(b, p), kind);
}

static bool same(Position a, Position b) { return a.side == b.side && a.slot == b.slot; }

static void announce(EventLog *log, Position holder, Position target, const BattleAbility *a, EventKind kind) {
    bc_emit(log, (BattleEvent){.kind = kind, .user = holder, .target = target, .ability = a->source,
                               .condition = a->kind});
}

void bc_ability_event(EventLog *log, Position holder, Position target, const BattleAbility *a) {
    announce(log, holder, target, a, EV_ABILITY);
}

void bc_ability_block(EventLog *log, Position holder, Position source, const BattleAbility *a) {
    announce(log, holder, source, a, EV_ABILITY_BLOCK);
}

// ------------------------------------------------------------- damage (per hit)

static unsigned scaled(unsigned value, unsigned percent) {
    const unsigned v = value * percent / 100u;
    return v ? v : 1;
}

unsigned bc_ability_power(const Battle *b, const Battler *a, unsigned power) {
    const BattleAbility *x = bc_ability(b, a, BABIL_LOW_POWER);
    return x && power <= x->arg0 ? scaled(power, x->percent) : power;
}

unsigned bc_ability_attack(const Battle *b, const Battler *a, const BattleMove *m, const Battler *d,
                           bool typeless, unsigned atk) {
    const BattleAbility *x;
    if ((x = bc_ability(b, a, BABIL_ATTACK_MULT)) && m->category == x->arg0) atk = scaled(atk, x->percent);
    if (!typeless && (x = bc_ability(b, a, BABIL_PINCH)) && m->type == x->arg0 && a->hp <= a->max_hp / x->arg2)
        atk = scaled(atk, x->percent);
    if (!typeless && (x = bc_ability(b, d, BABIL_RESIST)) && (m->type == x->arg0 || m->type == x->arg2))
        atk = scaled(atk, x->percent);
    return atk;
}

// Sturdy: from full HP, a hit that would knock the Pokemon out leaves 1 HP.
uint16_t bc_ability_endure(const Battle *b, Position target, uint16_t amount, bool *endured) {
    const Battler *mon = battle_at(b, target);
    *endured = false;
    if (!bc_ability(b, mon, BABIL_STURDY) || mon->hp != mon->max_hp || amount < mon->hp) return amount;
    *endured = true;
    return (uint16_t)(mon->hp - 1);
}

// ------------------------------------------------------------- targeting (per target)

static bool uses_type_chart(const BattleMove *m) {
    if (m->category != BCAT_STATUS) return true;
    for (unsigned i = 0; i < m->effect_count; ++i)
        if (m->effects[i].flags & BEFL_TYPE_CHART) return true;
    return false;
}

bool bc_ability_blocks_move(Battle *b, Position user, Position target, const BattleMove *m, bool typeless,
                            uint16_t move_id, EventLog *log) {
    if (same(user, target) || typeless) return false;
    const BattleAbility *x;
    if ((x = at(b, target, BABIL_TYPE_ABSORB)) && m->type == x->arg0) {
        Battler *mon = bc_mutable_at(b, target);
        if (mon->hp >= mon->max_hp) {
            bc_ability_block(log, target, user, x);
            return true;
        }
        bc_ability_event(log, target, target, x);
        unsigned amount = scaled(mon->max_hp, x->percent);
        if (amount > (unsigned)(mon->max_hp - mon->hp)) amount = mon->max_hp - mon->hp;
        mon->hp = (uint16_t)(mon->hp + amount);
        bc_emit(log, (BattleEvent){.kind = EV_HEAL, .user = target, .target = target, .move_id = move_id,
                                   .amount = (uint16_t)amount, .hp_after = mon->hp});
        return true;
    }
    if ((x = at(b, target, BABIL_TYPE_IMMUNE)) && m->type == x->arg0 && uses_type_chart(m)) {
        bc_ability_block(log, target, user, x);
        return true;
    }
    return false;
}

bool bc_ability_sure_hit(const Battle *b, Position user, Position target) {
    return at(b, user, BABIL_NO_GUARD) || at(b, target, BABIL_NO_GUARD);
}

unsigned bc_ability_accuracy(const Battle *b, Position user, unsigned accuracy) {
    const BattleAbility *x = at(b, user, BABIL_ACCURACY);
    return x ? accuracy * x->percent / 100u : accuracy;
}

bool bc_ability_ignores_evasion(const Battle *b, Position user) {
    const BattleAbility *x = at(b, user, BABIL_STAT_DROP_IMMUNE);
    return x && (x->arg2 & BABIL_IGNORE_EVASION);
}

bool bc_ability_passes_substitute(const Battle *b, Position user) {
    return at(b, user, BABIL_INFILTRATOR) != NULL;
}

// ------------------------------------------------------------- effect level

unsigned bc_ability_chance(const Battle *b, Position user, unsigned chance) {
    const BattleAbility *x = at(b, user, BABIL_SECONDARY_CHANCE);
    return x && chance ? chance * x->percent / 100u : chance;
}

// Only for effects on the target of a hit, which is never its user.
bool bc_ability_shields(const Battle *b, Position target) {
    return at(b, target, BABIL_SHIELD_DUST);
}

const BattleAbility *bc_ability_status_immunity(const Battle *b, Position target, uint8_t kind) {
    const BattleAbility *x = at(b, target, BABIL_STATUS_IMMUNE);
    return x && (x->arg0 & (1u << kind)) ? x : NULL;
}

const BattleAbility *bc_ability_volatile_immunity(const Battle *b, Position target, uint8_t bit) {
    const BattleAbility *x = at(b, target, BABIL_VOLATILE_IMMUNE);
    return x && (x->arg0 & bit) ? x : NULL;
}

const BattleAbility *bc_ability_drop_immunity(const Battle *b, Position target, uint8_t stat) {
    const BattleAbility *x = at(b, target, BABIL_STAT_DROP_IMMUNE);
    return x && (x->arg0 & (1u << stat)) ? x : NULL;
}

bool bc_ability_no_recoil(const Battle *b, Position user) {
    return at(b, user, BABIL_NO_RECOIL) != NULL;
}

// ------------------------------------------------------------- contact (per hit)

void bc_ability_contact(Battle *b, Position user, Position target, const BattleMove *m, uint16_t move_id,
                        EventLog *log) {
    if (!(m->move_flags & BMF_CONTACT) || same(user, target)) return;
    const Battler *attacker = battle_at(b, user);
    if (!attacker || attacker->fainted) return;
    const BattleAbility *x;
    if ((x = at(b, target, BABIL_CONTACT_STATUS))) {
        // Every other condition first; the chance is drawn only when the status could be set.
        if (attacker->status || bc_immune_to_status(&b->request->conditions, attacker, x->arg0) ||
            bc_ability_status_immunity(b, user, x->arg0) || bc_next_random(b) % 100u >= x->chance)
            return;
        bc_ability_event(log, target, user, x);
        bc_set_status(b, target, user, x->arg0, move_id, log);
    } else if ((x = at(b, target, BABIL_CONTACT_DAMAGE))) {
        const unsigned amount = attacker->max_hp / x->arg2;
        bc_ability_event(log, target, user, x);
        bc_apply_damage(b, (BattleEvent){.kind = EV_ABILITY_DAMAGE, .user = target, .target = user,
                                         .move_id = move_id, .ability = x->source},
                        (uint16_t)(amount ? amount : 1), log);
    }
}

// ------------------------------------------------------------- entry, end of turn

static void lower_on_entry(Battle *b, Position holder, Position foe, const BattleAbility *x, EventLog *log) {
    Battler *mon = bc_mutable_at(b, foe);
    const BattleAbility *block;
    if (x->arg1 < 0 && mon->vol.substitute_hp) {
        bc_emit(log, (BattleEvent){.kind = EV_FAIL, .user = holder, .target = foe});
        return;
    }
    if (x->arg1 < 0 && ((block = bc_ability_volatile_immunity(b, foe, BABIL_VOL_INTIMIDATE)) ||
                        (block = bc_ability_drop_immunity(b, foe, x->arg0)))) {
        bc_ability_block(log, foe, holder, block);
        return;
    }
    int stage = mon->stat_stages[x->arg0] + x->arg1;
    if (stage < -6) stage = -6;
    if (stage > 6) stage = 6;
    if (stage == mon->stat_stages[x->arg0]) {
        bc_emit(log, (BattleEvent){.kind = EV_FAIL, .user = holder, .target = foe});
        return;
    }
    mon->stat_stages[x->arg0] = (int8_t)stage;
    bc_emit(log, (BattleEvent){.kind = EV_STAGE, .user = holder, .target = foe, .stat = x->arg0,
                               .stage = (int8_t)stage});
}

void bc_ability_on_entry(Battle *b, Position p, EventLog *log) {
    Battler *mon = bc_mutable_at(b, p);
    if (!mon || mon->fainted || !bc_abilities_enabled(b)) return;
    const BattleAbility *x = &mon->entry->mon.ability;
    if (x->kind == BABIL_STATUS_IMMUNE) {
        const uint8_t kind = bc_status_kind(b, mon);
        if (kind && (x->arg0 & (1u << kind))) {
            bc_ability_event(log, p, p, x);
            bc_cure_status(mon, (BattleEvent){.user = p, .target = p}, log);
        }
    } else if (x->kind == BABIL_ENTRY_STAT) {
        const uint8_t foe = 1 - p.side;
        bool any = false;
        for (uint8_t s = 0; s < b->sides[foe].active_count; ++s) {
            const Battler *f = battle_at(b, (Position){foe, s});
            any |= f && !f->fainted;
        }
        if (!any) return;
        bc_ability_event(log, p, (Position){foe, 0}, x);
        for (uint8_t s = 0; s < b->sides[foe].active_count; ++s) {
            const Battler *f = battle_at(b, (Position){foe, s});
            if (f && !f->fainted) lower_on_entry(b, p, (Position){foe, s}, x, log);
        }
    }
}

void bc_ability_end_turn(Battle *b, Position p, EventLog *log) {
    Battler *mon = bc_mutable_at(b, p);
    const BattleAbility *x = bc_ability(b, mon, BABIL_END_TURN_STAT);
    if (!x || mon->fainted || mon->vol.entered) return;  // on the field since the turn started
    int stage = mon->stat_stages[x->arg0] + x->arg1;
    if (stage < -6) stage = -6;
    if (stage > 6) stage = 6;
    if (stage == mon->stat_stages[x->arg0]) return;
    mon->stat_stages[x->arg0] = (int8_t)stage;
    bc_ability_event(log, p, p, x);
    bc_emit(log, (BattleEvent){.kind = EV_STAGE, .user = p, .target = p, .stat = x->arg0, .stage = (int8_t)stage});
}

void bc_ability_begin_battle(Battle *b, EventLog *log) {
    if (!bc_abilities_enabled(b)) return;
    for (uint8_t side = 0; side < 2; ++side)
        for (uint8_t i = 0; i < b->sides[side].count; ++i) {
            const BattleAbility *a = &b->sides[side].battlers[i].entry->mon.ability;
            if (a->kind == BABIL_UNSUPPORTED)
                bc_emit(log, (BattleEvent){.kind = EV_ABILITY_UNSUPPORTED, .user = {side, BATTLE_SLOT_NONE},
                                           .target = {side, BATTLE_SLOT_NONE}, .battler = i, .ability = a->source});
        }
}
