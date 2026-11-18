// Shared predicates and damage modifiers of the GameCube battle core
// (requests V13 and V14, docs/modifiers.md).
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
// sources, the rounding (V13: floor after each step; V14: one 4.12 chain,
// rounded once) and the minimum (1, except the final damage, whose minimum
// of 1 is applied once afterwards). No modifier draws a random number.
//
// Request V15 (docs/weather.md) adds the weather predicate and three stages:
// WEATHER (the weather step of the damage formula: the first field record
// that holds, nothing else), and the integer SPEED and ACCURACY stages of
// the holder's speed and of a move's accuracy. Field records (the request's
// field rules) also close the defending stat's chain (sandstorm, snow).

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
    Mod m = {.stage = battle_modifier_stage(r->stage),  // bit 5 is 0 before V15
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
    case BPRED_MOVE_TYPE: return c->move && !c->typeless && (c->move->type == p.arg || c->move->type == p.alt);
    case BPRED_POWER_AT_MOST: return c->base_power <= p.arg;
    case BPRED_WEATHER: return bc_weather_in(c->b, p.arg);
    case BPRED_HAS_TYPE: return bc_has_type(subject, p.arg);
    default: return false;
    }
}

static bool holds(const ModContext *c, const Mod *m) {
    if (m->category && (!c->move || c->move->category != m->category)) return false;
    for (unsigned i = 0; i < BATTLE_MAX_PREDICATES; ++i)
        if (!pred_holds(c, m->pred[i])) return false;
    return true;
}

// ---------------------------------------------------------------- arithmetic
//
// V13 and earlier: each modifier multiplies the value by percent / 100,
// floored, then the stage's minimum. V14 (fixed point, docs/modifiers.md):
// the stage's modifiers are first chained into one 4.12 factor, then the
// value is multiplied by it once and rounded to the nearest integer (an
// exact half rounds down), as the Gen 5+ damage formula does. A percent is
// the nearest 4.12 value (150 -> 6144, 130 -> 5325, 33 -> 1352). The chain
// rounds half up for the power stage and half down for the others.

uint32_t bc_fx_percent(unsigned percent) { return (uint32_t)((percent * 4096u + 50u) / 100u); }

uint64_t bc_fx_apply(uint64_t factor, uint64_t value) {
    if (factor > UINT32_MAX) factor = UINT32_MAX;  // far beyond any valid request; keeps the product in range
    if (value > UINT32_MAX) value = UINT32_MAX;
    return (factor * value + 2047u) >> 12;
}

static uint64_t fx_chain(uint8_t stage, uint64_t chain, uint32_t factor) {
    if (chain > UINT32_MAX) chain = UINT32_MAX;
    return (chain * factor + (stage == BMOD_POWER ? 2048u : 2047u)) >> 12;
}

// BASE_POWER, SPEED and ACCURACY are integer stages.
static bool fixed_point(const ModContext *c, uint8_t stage) {
    return stage != BMOD_BASE_POWER && stage != BMOD_SPEED && stage != BMOD_ACCURACY &&
           c->b->request->version >= BATTLE_REQUEST_VERSION_FIXED_POINT;
}

// The damage-like stages have no minimum of their own.
static bool no_minimum(uint8_t stage) {
    return stage == BMOD_FINAL || stage == BMOD_WEATHER || stage == BMOD_ACCURACY;
}

// One stage in progress: the value itself (V13), or the chain (V14).
typedef struct {
    uint8_t stage;
    bool fixed;
    uint64_t value;
} Acc;

static void step(Acc *a, unsigned percent) {
    if (a->fixed) {
        a->value = fx_chain(a->stage, a->value, bc_fx_percent(percent));
    } else {
        a->value = a->value * percent / 100u;
        if (!a->value && !no_minimum(a->stage)) a->value = 1;
    }
}

// ---------------------------------------------------------------- sources

// Sources of one stage, in order (first-only where V12 used the first hook
// of the kind). V13: the V12 kinds first, then the records of the move, the
// attacker and the defender. V14 orders the sources by holder as the Gen 5+
// formula does: the move, then the attacker's ability, then the defender's
// (the defender's first for its defending stat); the final modifiers of the
// abilities go in speed order (the faster Pokemon's first, the attacker's
// on a tie).
// V15 adds the field records (SRC_FIELD) where the formula has them.
enum { SRC_MOVE, SRC_ATTACKER, SRC_DEFENDER, SRC_FIELD };
typedef struct { uint8_t source, kind; bool first_only; } Pass;
typedef struct { const Pass *passes; unsigned count; } PassList;
#define MOVE_PASS {SRC_MOVE, BEFF_MODIFIER, false}
#define ATTACKER_PASS {SRC_ATTACKER, BABIL_MODIFIER, false}
#define DEFENDER_PASS {SRC_DEFENDER, BABIL_MODIFIER, false}
#define FIELD_PASS {SRC_FIELD, 0, false}
#define V13_PASSES MOVE_PASS, ATTACKER_PASS, DEFENDER_PASS
#define LIST(list) {list, sizeof(list) / sizeof(*list)}

static const Pass base_power_passes[] = {V13_PASSES};
static const Pass power_passes[] = {{SRC_ATTACKER, BABIL_LOW_POWER, true}, V13_PASSES};
static const Pass attack_passes[] = {{SRC_ATTACKER, BABIL_ATTACK_MULT, false}, {SRC_ATTACKER, BABIL_PINCH, true},
                                     {SRC_DEFENDER, BABIL_RESIST, true}, V13_PASSES};
static const Pass defense_passes[] = {{SRC_DEFENDER, BABIL_DEFENSE_MULT, false}, V13_PASSES};
static const Pass final_passes[] = {V13_PASSES};
static const PassList v13_stages[BMOD_COUNT] = {
    [BMOD_BASE_POWER] = LIST(base_power_passes), [BMOD_POWER] = LIST(power_passes),
    [BMOD_ATTACK] = LIST(attack_passes), [BMOD_DEFENSE] = LIST(defense_passes), [BMOD_FINAL] = LIST(final_passes),
};

static const Pass fx_power_passes[] = {MOVE_PASS, {SRC_ATTACKER, BABIL_LOW_POWER, true}, ATTACKER_PASS, DEFENDER_PASS};
static const Pass fx_attack_passes[] = {MOVE_PASS, {SRC_ATTACKER, BABIL_ATTACK_MULT, false},
                                        {SRC_ATTACKER, BABIL_PINCH, true}, ATTACKER_PASS,
                                        {SRC_DEFENDER, BABIL_RESIST, true}, DEFENDER_PASS};
static const Pass fx_defense_passes[] = {MOVE_PASS, {SRC_DEFENDER, BABIL_DEFENSE_MULT, false}, DEFENDER_PASS,
                                         ATTACKER_PASS};
static const Pass fx_final_passes[] = {MOVE_PASS, ATTACKER_PASS, DEFENDER_PASS};
static const Pass fx_final_slower_passes[] = {MOVE_PASS, DEFENDER_PASS, ATTACKER_PASS};
static const PassList fx_stages[BMOD_COUNT] = {
    [BMOD_POWER] = LIST(fx_power_passes), [BMOD_ATTACK] = LIST(fx_attack_passes),
    [BMOD_DEFENSE] = LIST(fx_defense_passes), [BMOD_FINAL] = LIST(fx_final_passes),
};
static const PassList fx_final_slower = LIST(fx_final_slower_passes);
// V15: as V14, the field records closing the defending stat (after the items,
// in the formula), the weather step (the first field record that holds) and
// the integer stages of speed and accuracy (the attacker's records, then the
// defender's: Sand Veil after Compound Eyes).
static const Pass v15_defense_passes[] = {MOVE_PASS, {SRC_DEFENDER, BABIL_DEFENSE_MULT, false}, DEFENDER_PASS,
                                          ATTACKER_PASS, FIELD_PASS};
static const Pass v15_weather_passes[] = {{SRC_FIELD, 0, true}};
static const Pass v15_speed_passes[] = {ATTACKER_PASS};
static const Pass v15_accuracy_passes[] = {ATTACKER_PASS, DEFENDER_PASS};
static const PassList v15_stages[BMOD_COUNT] = {
    [BMOD_POWER] = LIST(fx_power_passes), [BMOD_ATTACK] = LIST(fx_attack_passes),
    [BMOD_DEFENSE] = LIST(v15_defense_passes), [BMOD_FINAL] = LIST(fx_final_passes),
    [BMOD_WEATHER] = LIST(v15_weather_passes), [BMOD_SPEED] = LIST(v15_speed_passes),
    [BMOD_ACCURACY] = LIST(v15_accuracy_passes),
};
#undef LIST

static void run_move(const ModContext *c, Acc *a) {
    if (c->b->request->version < BATTLE_REQUEST_VERSION_MODIFIERS) return;
    for (unsigned i = 0; i < c->move->effect_count; ++i) {
        const BattleEffect *e = &c->move->effects[i];
        if (e->kind != BEFF_MODIFIER) continue;
        const Mod m = from_record(&e->mod);
        if (m.stage == a->stage && holds(c, &m)) step(a, m.percent);
    }
}

// The request's field records (V15), in their order.
static void run_field(const ModContext *c, const Pass *p, Acc *a) {
    if (!bc_weather_rules(c->b)) return;
    const BattleFieldRules *f = &c->b->request->field;
    for (unsigned i = 0; i < f->modifier_count; ++i) {
        const Mod m = from_record(&f->modifiers[i]);
        if (m.stage != a->stage || !holds(c, &m)) continue;
        step(a, m.percent);
        if (p->first_only) break;
    }
}

static void run_hooks(const ModContext *c, const Pass *p, Acc *a) {
    const Battler *holder = p->source == SRC_DEFENDER ? c->defender : c->attacker;
    const BattleAbilityHook *h = NULL;
    while ((h = bc_ability_next(c->b, holder, p->kind, h))) {
        const Mod m = from_hook(h);
        if (m.stage == a->stage && m.defender == (p->source == SRC_DEFENDER) && holds(c, &m)) step(a, m.percent);
        if (p->first_only) break;
    }
}

static const PassList *passes(const ModContext *c, uint8_t stage, bool fixed) {
    const bool v15 = bc_weather_rules(c->b);
    if (v15 && (stage == BMOD_SPEED || stage == BMOD_ACCURACY)) return &v15_stages[stage];
    if (!fixed) return &v13_stages[stage];
    if (stage == BMOD_FINAL && c->attacker->speed < c->defender->speed) return &fx_final_slower;
    return v15 ? &v15_stages[stage] : &fx_stages[stage];
}

uint64_t bc_modify(const ModContext *c, uint8_t stage, uint64_t value) {
    if (stage == BMOD_NONE || stage >= BMOD_COUNT) return value;
    const bool fixed = fixed_point(c, stage);
    Acc a = {.stage = stage, .fixed = fixed, .value = fixed ? 4096u : value};
    // The semi-invulnerable double damage (Earthquake on Dig) heads the final chain.
    if (fixed && stage == BMOD_FINAL && c->doubled) a.value = 8192u;
    const PassList *list = passes(c, stage, fixed);
    for (unsigned i = 0; i < list->count; ++i) {
        const Pass *p = &list->passes[i];
        if (p->source == SRC_MOVE) run_move(c, &a);
        else if (p->source == SRC_FIELD) run_field(c, p, &a);
        else run_hooks(c, p, &a);
    }
    if (!fixed) return a.value;
    value = bc_fx_apply(a.value, value);
    return value || no_minimum(stage) ? value : 1;
}

unsigned bc_modify_speed(const Battle *b, const Battler *holder, unsigned speed) {
    if (!bc_weather_rules(b)) return speed;
    const ModContext c = {.b = b, .attacker = holder, .defender = holder};
    return (unsigned)bc_modify(&c, BMOD_SPEED, speed);
}

unsigned bc_modify_accuracy(const Battle *b, const Battler *attacker, const BattleMove *move,
                            const Battler *defender, unsigned accuracy) {
    if (!bc_weather_rules(b)) return accuracy;
    const ModContext c = {.b = b, .attacker = attacker, .defender = defender, .move = move,
                          .base_power = move->power};
    return (unsigned)bc_modify(&c, BMOD_ACCURACY, accuracy);
}
