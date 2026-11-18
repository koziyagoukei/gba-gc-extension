// Shared predicates and damage modifiers of the GameCube battle core
// (request V13 PoC, docs/rule-poc.md).
//
// A modifier is one record type for moves (BEFF_MODIFIER) and abilities
// (BABIL_MODIFIER): a stage, a category, a percent and up to two predicates
// that must both hold. The V12 hook kinds that are plain multipliers
// (ATTACK_MULT, PINCH, LOW_POWER, RESIST, DEFENSE_MULT) are translated into
// the same form here and run through the same functions, so a V12 battle
// takes the same values in the same order as before.
//
// What the record cannot change is fixed per stage below: where the stage
// applies in the damage formula (bc_calculate_damage), the order of its
// sources, the rounding (floor after each step) and the minimum (1, except
// the final damage, whose minimum of 1 is applied once afterwards). No
// modifier draws a random number.

#include "battle_internal.h"

#include <stddef.h>

// An evaluated predicate. `alt` is a second accepted move type, used only by
// the translated V12 RESIST hook (two types); wire predicates have none.
typedef struct { uint8_t kind, arg, alt; } Pred;
typedef struct {
    uint8_t stage, category, percent;
    bool defender;  // applies while the holder is the defending Pokemon
    Pred pred[BATTLE_MAX_PREDICATES];
} Mod;

static Mod from_record(const BattleModifier *r) {
    Mod m = {.stage = r->stage & BMOD_STAGE_MASK,
             .category = (uint8_t)((r->stage & BMOD_CATEGORY_MASK) >> BMOD_CATEGORY_SHIFT),
             .percent = r->percent, .defender = (r->stage & BMODF_DEFENDER) != 0};
    for (unsigned i = 0; i < BATTLE_MAX_PREDICATES; ++i) m.pred[i] = (Pred){r->pred[i].kind, r->pred[i].arg, BABIL_NO_TYPE};
    return m;
}

// The V12 multiplier kinds as modifiers (their validity is checked on decode).
static Mod from_hook(const BattleAbilityHook *h) {
    if (h->kind == BABIL_MODIFIER) return from_record(&h->mod);
    Mod m = {.percent = h->percent, .pred = {{0, 0, BABIL_NO_TYPE}, {0, 0, BABIL_NO_TYPE}}};
    switch (h->kind) {
    case BABIL_ATTACK_MULT:  // arg0 = category (0 any); BAHF_TYPE_FILTER: moves of type arg2
        m.stage = BMOD_ATTACK;
        m.category = h->arg0;
        if (h->flags & BAHF_TYPE_FILTER) m.pred[0] = (Pred){BPRED_MOVE_TYPE, h->arg2, BABIL_NO_TYPE};
        break;
    case BABIL_PINCH:  // moves of type arg0 while the attacker's HP <= max / arg2
        m.stage = BMOD_ATTACK;
        m.pred[0] = (Pred){BPRED_MOVE_TYPE, h->arg0, BABIL_NO_TYPE};
        m.pred[1] = (Pred){BPRED_HP_AT_MOST, h->arg2, BABIL_NO_TYPE};
        break;
    case BABIL_RESIST:  // the defender's ability lowers the attacking stat for types arg0 / arg2
        m.stage = BMOD_ATTACK;
        m.defender = true;
        m.pred[0] = (Pred){BPRED_MOVE_TYPE, h->arg0, h->arg2};
        break;
    case BABIL_DEFENSE_MULT:  // the defender's stat for category arg0
        m.stage = BMOD_DEFENSE;
        m.defender = true;
        m.category = h->arg0;
        break;
    case BABIL_LOW_POWER:  // power <= arg0
        m.stage = BMOD_POWER;
        m.pred[0] = (Pred){BPRED_POWER_AT_MOST, h->arg0, BABIL_NO_TYPE};
        break;
    default:
        break;  // BMOD_NONE: never applies
    }
    return m;
}

static bool pred_holds(const ModContext *c, Pred p) {
    const Battler *subject = (p.kind & BPRED_DEFENDER) ? c->defender : c->attacker;
    switch (p.kind & BPRED_KIND_MASK) {
    case BPRED_NONE: return true;
    case BPRED_HP_AT_MOST: return subject->hp <= subject->max_hp / p.arg;
    case BPRED_STATUS: {
        const uint8_t kind = bc_status_kind(c->b, subject);
        return kind && (p.arg & (1u << kind));
    }
    case BPRED_MOVE_TYPE: return !c->typeless && (c->move->type == p.arg || c->move->type == p.alt);
    case BPRED_POWER_AT_MOST: return c->base_power <= p.arg;
    default: return false;
    }
}

static bool holds(const ModContext *c, const Mod *m) {
    if (m->category && c->move->category != m->category) return false;
    for (unsigned i = 0; i < BATTLE_MAX_PREDICATES; ++i)
        if (!pred_holds(c, m->pred[i])) return false;
    return true;
}

// The one multiplication: floor, then the stage's minimum.
static uint64_t apply(uint8_t stage, uint64_t value, unsigned percent) {
    value = value * percent / 100u;
    return value || stage == BMOD_FINAL ? value : 1;
}

// Sources of one stage, in order. The V12 kinds come first, as in V12
// (first-only where V12 used the first hook of the kind), then the V13
// records: the move's, the attacker's, the defender's.
enum { SRC_MOVE, SRC_ATTACKER, SRC_DEFENDER };
typedef struct { uint8_t source, kind; bool first_only; } Pass;
#define V13_PASSES {SRC_MOVE, BEFF_MODIFIER, false}, {SRC_ATTACKER, BABIL_MODIFIER, false}, \
                   {SRC_DEFENDER, BABIL_MODIFIER, false}
static const Pass base_power_passes[] = {V13_PASSES};
static const Pass power_passes[] = {{SRC_ATTACKER, BABIL_LOW_POWER, true}, V13_PASSES};
static const Pass attack_passes[] = {{SRC_ATTACKER, BABIL_ATTACK_MULT, false}, {SRC_ATTACKER, BABIL_PINCH, true},
                                     {SRC_DEFENDER, BABIL_RESIST, true}, V13_PASSES};
static const Pass defense_passes[] = {{SRC_DEFENDER, BABIL_DEFENSE_MULT, false}, V13_PASSES};
static const Pass final_passes[] = {V13_PASSES};
static const struct { const Pass *passes; unsigned count; } stages[BMOD_COUNT] = {
#define STAGE(id, list) [id] = {list, sizeof(list) / sizeof(*list)}
    STAGE(BMOD_BASE_POWER, base_power_passes), STAGE(BMOD_POWER, power_passes), STAGE(BMOD_ATTACK, attack_passes),
    STAGE(BMOD_DEFENSE, defense_passes), STAGE(BMOD_FINAL, final_passes),
#undef STAGE
};

static uint64_t run_move(const ModContext *c, uint8_t stage, uint64_t value) {
    if (c->b->request->version < BATTLE_REQUEST_VERSION_MODIFIERS) return value;
    for (unsigned i = 0; i < c->move->effect_count; ++i) {
        const BattleEffect *e = &c->move->effects[i];
        if (e->kind != BEFF_MODIFIER) continue;
        const Mod m = from_record(&e->mod);
        if (m.stage == stage && holds(c, &m)) value = apply(stage, value, m.percent);
    }
    return value;
}

static uint64_t run_hooks(const ModContext *c, const Pass *p, uint8_t stage, uint64_t value) {
    const Battler *holder = p->source == SRC_DEFENDER ? c->defender : c->attacker;
    const BattleAbilityHook *h = NULL;
    while ((h = bc_ability_next(c->b, holder, p->kind, h))) {
        const Mod m = from_hook(h);
        if (m.stage == stage && m.defender == (p->source == SRC_DEFENDER) && holds(c, &m))
            value = apply(stage, value, m.percent);
        if (p->first_only) break;
    }
    return value;
}

uint64_t bc_modify(const ModContext *c, uint8_t stage, uint64_t value) {
    if (stage == BMOD_NONE || stage >= BMOD_COUNT) return value;
    for (unsigned i = 0; i < stages[stage].count; ++i) {
        const Pass *p = &stages[stage].passes[i];
        value = p->source == SRC_MOVE ? run_move(c, stage, value) : run_hooks(c, p, stage, value);
    }
    return value;
}
