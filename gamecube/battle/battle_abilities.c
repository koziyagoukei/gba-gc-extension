// Ability hooks of the GameCube battle core (requests V11/V12, docs/abilities.md).
//
// An ability is a list of hooks resolved by the GBA (a V11 descriptor is one
// hook). The functions below are called from fixed points of the move order
// (battle_effects.c) and of the turn (battle_core.c). Hooks that modify a
// move effect run inside that effect's handler, so they inherit its V10
// unit; they never decide how often the effect runs.

#include "battle_internal.h"

#include <stddef.h>

bool bc_abilities_enabled(const Battle *b) {
    return b->request->version >= BATTLE_REQUEST_VERSION_ABILITIES && !(b->request->rules & RULE_IGNORE_ABILITIES);
}

static const BattleAbility *ability_of(const Battle *b, const Battler *mon) {
    if (!mon || !mon->entry || !bc_abilities_enabled(b)) return NULL;
    const BattleAbility *a = &mon->entry->mon.ability;
    return a->cls == BACLASS_HOOKS ? a : NULL;
}

// The hook of `kind` after `prev` (NULL for the first), whose holder-side
// conditions (status, ability state) hold.
const BattleAbilityHook *bc_ability_next(const Battle *b, const Battler *mon, uint8_t kind,
                                         const BattleAbilityHook *prev) {
    const BattleAbility *a = ability_of(b, mon);
    if (!a) return NULL;
    unsigned i = prev ? (unsigned)(prev - a->hooks) + 1 : 0;
    for (; i < a->count; ++i) {
        const BattleAbilityHook *h = &a->hooks[i];
        if (h->kind != kind) continue;
        if ((h->flags & BAHF_REQUIRES_STATUS) && !bc_status_kind(b, mon)) continue;
        if ((h->flags & BAHF_REQUIRES_STATE) && !mon->vol.ability_state) continue;
        return h;
    }
    return NULL;
}

const BattleAbilityHook *bc_ability(const Battle *b, const Battler *mon, uint8_t kind) {
    return bc_ability_next(b, mon, kind, NULL);
}

static const BattleAbilityHook *at(const Battle *b, Position p, uint8_t kind) {
    return bc_ability(b, battle_at(b, p), kind);
}

static bool same(Position a, Position b) { return a.side == b.side && a.slot == b.slot; }

static void announce(const Battle *b, EventLog *log, Position holder, Position target, const BattleAbilityHook *h,
                     EventKind kind) {
    const Battler *mon = battle_at(b, holder);
    bc_emit(log, (BattleEvent){.kind = kind, .user = holder, .target = target,
                               .ability = mon && mon->entry ? mon->entry->mon.ability.source : 0,
                               .condition = h->kind});
}

void bc_ability_event(const Battle *b, EventLog *log, Position holder, Position target, const BattleAbilityHook *h) {
    announce(b, log, holder, target, h, EV_ABILITY);
}

void bc_ability_block(const Battle *b, EventLog *log, Position holder, Position source, const BattleAbilityHook *h) {
    announce(b, log, holder, source, h, EV_ABILITY_BLOCK);
}

static bool roll(Battle *b, const BattleAbilityHook *h) {
    return !h->chance_den || bc_next_random(b) % h->chance_den < h->chance;
}

static void stage_change(Battle *b, Position holder, Position target, const BattleAbilityHook *h, int delta,
                         EventLog *log) {
    Battler *mon = bc_mutable_at(b, target);
    int stage = mon->stat_stages[h->arg0] + delta;
    if (stage < -6) stage = -6;
    if (stage > 6) stage = 6;
    if (stage == mon->stat_stages[h->arg0]) return;
    mon->stat_stages[h->arg0] = (int8_t)stage;
    bc_ability_event(b, log, holder, target, h);
    bc_emit(log, (BattleEvent){.kind = EV_STAGE, .user = holder, .target = target, .stat = h->arg0,
                               .stage = (int8_t)stage});
}

// ------------------------------------------------------------- damage (per hit)

static unsigned scaled(unsigned value, unsigned percent) {
    const unsigned v = value * percent / 100u;
    return v ? v : 1;
}

unsigned bc_ability_power(const Battle *b, const Battler *a, unsigned power) {
    const BattleAbilityHook *x = bc_ability(b, a, BABIL_LOW_POWER);
    return x && power <= x->arg0 ? scaled(power, x->percent) : power;
}

unsigned bc_ability_attack(const Battle *b, const Battler *a, const BattleMove *m, const Battler *d,
                           bool typeless, unsigned atk) {
    const BattleAbilityHook *x = NULL;
    while ((x = bc_ability_next(b, a, BABIL_ATTACK_MULT, x)))
        if ((!x->arg0 || m->category == x->arg0) &&
            (!(x->flags & BAHF_TYPE_FILTER) || (!typeless && m->type == x->arg2)))
            atk = scaled(atk, x->percent);
    if (!typeless && (x = bc_ability(b, a, BABIL_PINCH)) && m->type == x->arg0 && a->hp <= a->max_hp / x->arg2)
        atk = scaled(atk, x->percent);
    if (!typeless && (x = bc_ability(b, d, BABIL_RESIST)) && (m->type == x->arg0 || m->type == x->arg2))
        atk = scaled(atk, x->percent);
    return atk;
}

unsigned bc_ability_defense(const Battle *b, const BattleMove *m, const Battler *d, unsigned def) {
    const BattleAbilityHook *x = NULL;
    while ((x = bc_ability_next(b, d, BABIL_DEFENSE_MULT, x)))
        if (m->category == x->arg0) def = scaled(def, x->percent);
    return def;
}

bool bc_ability_ignores_burn(const Battle *b, const Battler *a) {
    return bc_ability(b, a, BABIL_IGNORE_BURN) != NULL;
}

unsigned bc_ability_stab(const Battle *b, const Battler *a) {
    const BattleAbilityHook *x = bc_ability(b, a, BABIL_STAB_MULT);
    return x ? x->percent : 150u;
}

// One defending type's multiplier (quarters) after the attacker's TYPE_PASS hooks.
unsigned bc_ability_type_factor(const Battle *b, const Battler *a, uint8_t move_type, uint8_t defend_type,
                                unsigned factor) {
    if (factor) return factor;
    const BattleAbilityHook *x = NULL;
    while ((x = bc_ability_next(b, a, BABIL_TYPE_PASS, x)))
        if (move_type == x->arg0 && defend_type == x->arg2) return 4;
    return factor;
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
    const BattleAbilityHook *x;
    Battler *mon = bc_mutable_at(b, target);
    if ((x = at(b, target, BABIL_TYPE_ABSORB)) && m->type == x->arg0) {
        if (mon->hp >= mon->max_hp) {
            bc_ability_block(b, log, target, user, x);
            return true;
        }
        bc_ability_event(b, log, target, target, x);
        unsigned amount = scaled(mon->max_hp, x->percent);
        if (amount > (unsigned)(mon->max_hp - mon->hp)) amount = mon->max_hp - mon->hp;
        mon->hp = (uint16_t)(mon->hp + amount);
        bc_emit(log, (BattleEvent){.kind = EV_HEAL, .user = target, .target = target, .move_id = move_id,
                                   .amount = (uint16_t)amount, .hp_after = mon->hp});
        return true;
    }
    // Flash Fire: absorbed (also when already boosted), and the holder's state is set.
    if ((x = at(b, target, BABIL_ABSORB_STATE)) && m->type == x->arg0 &&
        (!(x->flags & BAHF_NOT_FROZEN) || bc_status_kind(b, mon) != BST_FREEZE)) {
        mon->vol.ability_state = 1;
        bc_ability_event(b, log, target, target, x);
        return true;
    }
    if ((x = at(b, target, BABIL_TYPE_IMMUNE)) && m->type == x->arg0 && uses_type_chart(m)) {
        bc_ability_block(b, log, target, user, x);
        return true;
    }
    return false;
}

bool bc_ability_sure_hit(const Battle *b, Position user, Position target) {
    return at(b, user, BABIL_NO_GUARD) || at(b, target, BABIL_NO_GUARD);
}

unsigned bc_ability_accuracy(const Battle *b, Position user, unsigned accuracy) {
    const BattleAbilityHook *x = at(b, user, BABIL_ACCURACY);
    return x ? accuracy * x->percent / 100u : accuracy;
}

bool bc_ability_ignores_evasion(const Battle *b, Position user) {
    const BattleAbilityHook *x = at(b, user, BABIL_STAT_DROP_IMMUNE);
    return x && (x->arg2 & BABIL_IGNORE_EVASION);
}

bool bc_ability_passes_substitute(const Battle *b, Position user) {
    return at(b, user, BABIL_INFILTRATOR) != NULL;
}

// ------------------------------------------------------------- effect level

unsigned bc_ability_chance(const Battle *b, Position user, unsigned chance) {
    const BattleAbilityHook *x = at(b, user, BABIL_SECONDARY_CHANCE);
    return x && chance ? chance * x->percent / 100u : chance;
}

// Only for effects on the target of a hit, which is never its user.
bool bc_ability_shields(const Battle *b, Position target) {
    return at(b, target, BABIL_SHIELD_DUST) != NULL;
}

const BattleAbilityHook *bc_ability_status_immunity(const Battle *b, Position target, uint8_t kind) {
    const BattleAbilityHook *x = at(b, target, BABIL_STATUS_IMMUNE);
    return x && (x->arg0 & (1u << kind)) ? x : NULL;
}

const BattleAbilityHook *bc_ability_volatile_immunity(const Battle *b, Position target, uint8_t bit) {
    const BattleAbilityHook *x = NULL;
    while ((x = bc_ability_next(b, battle_at(b, target), BABIL_VOLATILE_IMMUNE, x)))
        if (x->arg0 & bit) return x;
    return NULL;
}

const BattleAbilityHook *bc_ability_drop_immunity(const Battle *b, Position target, uint8_t stat) {
    const BattleAbilityHook *x = at(b, target, BABIL_STAT_DROP_IMMUNE);
    return x && (x->arg0 & (1u << stat)) ? x : NULL;
}

bool bc_ability_no_recoil(const Battle *b, Position user) {
    return at(b, user, BABIL_NO_RECOIL) != NULL;
}

// Whether `target` could be given `kind` now (status, type and ability immunity).
static bool can_take_status(const Battle *b, Position target, uint8_t kind) {
    const Battler *mon = battle_at(b, target);
    return mon && !mon->fainted && !mon->status &&
           !bc_immune_to_status(&b->request->conditions, mon, kind) && !bc_ability_status_immunity(b, target, kind);
}

// Synchronize: right after `holder` got a status from another battler.
// Statuses it gives back do not reflect again (the game's synchronize state).
void bc_ability_status_set(Battle *b, Position source, Position holder, uint8_t kind, uint16_t move_id,
                           EventLog *log) {
    if (same(source, holder) || b->ability_mirroring) return;
    const BattleAbilityHook *x = at(b, holder, BABIL_STATUS_MIRROR);
    if (!x || !(x->arg0 & (1u << kind))) return;
    const Battler *src = battle_at(b, source);
    if (!src) return;
    const uint8_t given = kind == BST_TOXIC && (x->flags & BAHF_TOXIC_TO_POISON) ? BST_POISON : kind;
    bc_ability_event(b, log, holder, source, x);  // shown even when the status cannot be given back
    if (!can_take_status(b, source, given)) return;
    b->ability_mirroring = true;
    bc_set_status(b, holder, source, given, move_id, log);
    b->ability_mirroring = false;
}

// ------------------------------------------------------------- contact (per hit)

void bc_ability_contact(Battle *b, Position user, Position target, const BattleMove *m, uint16_t move_id,
                        EventLog *log) {
    if (!(m->move_flags & BMF_CONTACT) || same(user, target)) return;
    const Battler *attacker = battle_at(b, user);
    if (!attacker || attacker->fainted) return;
    const BattleAbilityHook *x;
    if ((x = at(b, target, BABIL_CONTACT_STATUS))) {
        // Every other condition first; the chance is drawn only when the status could be set.
        if (!can_take_status(b, user, x->arg0) || !roll(b, x)) return;
        bc_ability_event(b, log, target, user, x);
        bc_set_status(b, target, user, x->arg0, move_id, log);
    } else if ((x = at(b, target, BABIL_CONTACT_DAMAGE))) {
        const unsigned amount = attacker->max_hp / x->arg2;
        bc_ability_event(b, log, target, user, x);
        bc_apply_damage(b, (BattleEvent){.kind = EV_ABILITY_DAMAGE, .user = target, .target = user,
                                         .move_id = move_id, .ability = battle_at(b, target)->entry->mon.ability.source},
                        (uint16_t)(amount ? amount : 1), log);
    }
}

// ------------------------------------------------------------- after the move

// Moxie: after the move (after recoil), one stage step per Pokemon this move
// knocked out; not when the battle is already decided.
void bc_ability_after_move(Battle *b, Position user, unsigned knocked_out, EventLog *log) {
    const Battler *mon = battle_at(b, user);
    const BattleAbilityHook *x = bc_ability(b, mon, BABIL_KO_STAT);
    if (!x || !knocked_out || mon->fainted || bc_battle_decided(b)) return;
    stage_change(b, user, user, x, x->arg1 * (int)knocked_out, log);
}

// ------------------------------------------------------------- entry, end of turn

static void lower_on_entry(Battle *b, Position holder, Position foe, const BattleAbilityHook *x, EventLog *log) {
    Battler *mon = bc_mutable_at(b, foe);
    const BattleAbilityHook *block;
    if (x->arg1 < 0 && mon->vol.substitute_hp) {
        bc_emit(log, (BattleEvent){.kind = EV_FAIL, .user = holder, .target = foe});
        return;
    }
    if (x->arg1 < 0 && ((block = bc_ability_volatile_immunity(b, foe, BABIL_VOL_INTIMIDATE)) ||
                        (block = bc_ability_drop_immunity(b, foe, x->arg0)))) {
        bc_ability_block(b, log, foe, holder, block);
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
    const BattleAbility *a = ability_of(b, mon);
    if (!a || mon->fainted) return;
    for (unsigned i = 0; i < a->count; ++i) {
        const BattleAbilityHook *x = &a->hooks[i];
        if (x->kind == BABIL_STATUS_IMMUNE) {
            const uint8_t kind = bc_status_kind(b, mon);
            if (kind && (x->arg0 & (1u << kind))) {
                bc_ability_event(b, log, p, p, x);
                bc_cure_status(mon, (BattleEvent){.user = p, .target = p}, log);
            }
        } else if (x->kind == BABIL_ENTRY_STAT) {
            // The foes in the hook's range (shared Position rule, battle_in_range).
            const uint8_t foe = 1 - p.side;
            uint8_t targets = 0;
            for (uint8_t s = 0; s < b->sides[foe].active_count; ++s) {
                const Position f = {foe, s};
                const Battler *m = battle_at(b, f);
                if (m && !m->fainted && battle_in_range(b, p, f, x->arg2)) targets |= 1u << s;
            }
            if (!targets) continue;
            bc_ability_event(b, log, p, (Position){foe, bc_first_target(targets)}, x);
            for (uint8_t s = 0; s < BC_MAX_ACTIVE; ++s)
                if (targets & (1u << s)) lower_on_entry(b, p, (Position){foe, s}, x, log);
        }
    }
}

// Before the residual damage: Shed Skin.
void bc_ability_end_turn_cure(Battle *b, Position p, EventLog *log) {
    Battler *mon = bc_mutable_at(b, p);
    const BattleAbilityHook *x = bc_ability(b, mon, BABIL_END_TURN_CURE);
    const uint8_t kind = mon ? bc_status_kind(b, mon) : BST_NONE;
    if (!x || mon->fainted || !kind || !(x->arg0 & (1u << kind)) || !roll(b, x)) return;
    bc_ability_event(b, log, p, p, x);
    bc_cure_status(mon, (BattleEvent){.user = p, .target = p}, log);
}

// After the residual damage: Speed Boost.
void bc_ability_end_turn(Battle *b, Position p, EventLog *log) {
    Battler *mon = bc_mutable_at(b, p);
    const BattleAbilityHook *x = bc_ability(b, mon, BABIL_END_TURN_STAT);
    if (!x || mon->fainted || mon->vol.entered) return;  // on the field since the turn started
    stage_change(b, p, p, x, x->arg1, log);
}

bool bc_ability_unsupported(const Battle *b, uint8_t side, uint8_t index) {
    return bc_abilities_enabled(b) && b->sides[side].battlers[index].entry->mon.ability.cls == BACLASS_UNSUPPORTED;
}

void bc_ability_begin_battle(Battle *b, EventLog *log) {
    for (uint8_t side = 0; side < 2; ++side)
        for (uint8_t i = 0; i < b->sides[side].count; ++i)
            if (bc_ability_unsupported(b, side, i))
                bc_emit(log, (BattleEvent){.kind = EV_ABILITY_UNSUPPORTED, .user = {side, BATTLE_SLOT_NONE},
                                           .target = {side, BATTLE_SLOT_NONE}, .battler = i,
                                           .ability = b->sides[side].battlers[i].entry->mon.ability.source});
}
