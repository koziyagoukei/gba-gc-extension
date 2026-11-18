// Field state of the GameCube battle core: weather (request V15,
// docs/weather.md).
//
// Three layers:
//   1. the state, one FieldEffect in Battle.field, written only here;
//   2. the shared operations below (set, suppression, countdown, residual)
//      and the weather predicates and stages of battle_modifiers.c;
//   3. the game's rules, resolved by the GBA into the request: the field
//      rules (residual damage, immune types, the weather damage and stat
//      records), the move effects (BEFF_WEATHER*) and the ability hooks
//      (BABIL_ENTRY_WEATHER, WEATHER_*).
// A move (Rain Dance) and an ability (Drizzle) set a weather through the same
// bc_weather_set; nothing here knows a game's move or ability ids.

#include "battle_internal.h"

bool bc_weather_rules(const Battle *b) { return b->request->version >= BATTLE_REQUEST_VERSION_WEATHER; }

const FieldEffect *battle_field_weather(const Battle *b) { return &b->field.weather; }

// A Pokemon on the field (any side, any slot, not fainted) whose ability
// suppresses the weather: the weather stays and counts down, but has no
// effect while it is there (Cloud Nine, Air Lock).
static bool suppressed(const Battle *b) {
    for (uint8_t side = 0; side < 2; ++side)
        for (uint8_t slot = 0; slot < b->sides[side].active_count; ++slot) {
            const Battler *mon = battle_at(b, (Position){side, slot});
            if (mon && !mon->fainted && bc_ability_suppresses_weather(b, mon)) return true;
        }
    return false;
}

uint8_t battle_weather(const Battle *b) {
    if (!bc_weather_rules(b) || !b->field.weather.kind || suppressed(b)) return BWEATHER_NONE;
    return b->field.weather.kind;
}

bool bc_weather_in(const Battle *b, uint8_t mask) {
    const uint8_t kind = battle_weather(b);
    return kind != BWEATHER_NONE && (mask & BWEATHER_BIT(kind));
}

// The same weather again changes nothing (a move fails, an ability does not
// restart the count); so does any change of a locked start weather. The
// comparison is with the weather on the field, suppressed or not.
WeatherResult bc_weather_set(Battle *b, Position source, uint8_t source_kind, uint8_t kind, uint8_t turns,
                             EventLog *log) {
    FieldEffect *w = &b->field.weather;
    if (w->kind == kind) return WEATHER_SAME;
    if (b->field.locked) return WEATHER_LOCKED;
    w->kind = kind;
    if (turns != BWEATHER_TURNS_KEEP) w->turns = turns;
    w->source_kind = source_kind;
    w->source = source;
    bc_emit(log, (BattleEvent){.kind = EV_WEATHER_START, .user = source, .target = source, .condition = kind,
                               .amount = w->turns});
    return WEATHER_SET;
}

void bc_weather_begin(Battle *b, EventLog *log) {
    const BattleFieldRules *r = &b->request->field;
    if (!bc_weather_rules(b) || !r->start_weather) return;
    bc_weather_set(b, (Position){0, BATTLE_SLOT_NONE}, FSRC_RULE, r->start_weather, r->start_turns, log);
    b->field.locked = (r->flags & BFR_START_LOCKED) != 0;
}

static bool immune_type(const BattleWeatherKindRules *rules, const Battler *mon) {
    for (unsigned t = 0; t < BATTLE_WEATHER_IMMUNE_TYPES; ++t)
        if (rules->immune_types[t] != BABIL_NO_TYPE && bc_has_type(mon, rules->immune_types[t])) return true;
    return false;
}

// One Pokemon's weather residual: its ability's (Rain Dish, Ice Body, Dry
// Skin, Solar Power) instead of the weather's own, else the weather's damage
// unless a type, its ability or being underground protects it.
static void residual(Battle *b, Position p, uint8_t kind, EventLog *log) {
    Battler *mon = bc_mutable_at(b, p);
    const BattleAbilityHook *h = bc_ability_weather_residual(b, mon, kind);
    const bool underground = mon->vol.semi_invulnerable == BSEMI_UNDERGROUND;
    if (h) {
        if ((h->flags & BAHF_NOT_UNDERGROUND) && underground) return;
        unsigned amount = mon->max_hp / h->arg2;
        if (!amount) amount = 1;
        const uint16_t ability = mon->entry->mon.ability.source;
        if (h->arg1 > 0) {
            if (mon->hp >= mon->max_hp) return;
            if (amount > (unsigned)(mon->max_hp - mon->hp)) amount = mon->max_hp - mon->hp;
            bc_ability_event(b, log, p, p, h);
            mon->hp = (uint16_t)(mon->hp + amount);
            bc_emit(log, (BattleEvent){.kind = EV_HEAL, .user = p, .target = p, .amount = (uint16_t)amount,
                                       .hp_after = mon->hp, .ability = ability});
        } else {
            bc_ability_event(b, log, p, p, h);
            bc_apply_damage(b, (BattleEvent){.kind = EV_ABILITY_DAMAGE, .user = p, .target = p, .ability = ability},
                            (uint16_t)amount, log);
        }
        return;
    }
    const BattleWeatherKindRules *rules = &b->request->field.weather[kind - 1];
    if (!rules->residual_divisor || immune_type(rules, mon) || bc_ability_weather_immunity(b, mon, kind) ||
        ((rules->flags & BWKF_UNDERGROUND_SAFE) && underground))
        return;
    unsigned amount = mon->max_hp / rules->residual_divisor;
    bc_apply_damage(b, (BattleEvent){.kind = EV_WEATHER_DAMAGE, .user = p, .target = p, .condition = kind},
                    (uint16_t)(amount ? amount : 1), log);
}

// First the countdown (a weather that ends now does nothing more this turn),
// then each Pokemon's residual in the end-of-turn speed order. A suppressed
// weather counts down but has no residual.
void bc_weather_end_turn(Battle *b, const Position *order, unsigned count, EventLog *log) {
    FieldEffect *w = &b->field.weather;
    if (!bc_weather_rules(b) || !w->kind) return;
    const uint8_t kind = w->kind;
    if (w->turns && --w->turns == 0) {
        *w = (FieldEffect){.kind = BWEATHER_NONE};
        bc_emit(log, (BattleEvent){.kind = EV_WEATHER_END, .user = {0, BATTLE_SLOT_NONE},
                                   .target = {0, BATTLE_SLOT_NONE}, .condition = kind});
        return;
    }
    bc_emit(log, (BattleEvent){.kind = EV_WEATHER_CONTINUE, .user = {0, BATTLE_SLOT_NONE},
                               .target = {0, BATTLE_SLOT_NONE}, .condition = kind, .amount = w->turns});
    for (unsigned i = 0; i < count; ++i) {
        const Battler *mon = battle_at(b, order[i]);
        if (!mon || mon->fainted || battle_weather(b) != kind) continue;
        residual(b, order[i], kind, log);
    }
}

// ------------------------------------------------------------- move rules in a weather

uint8_t bc_weather_move_type(const Battle *b, const BattleMove *m) {
    if (!bc_weather_rules(b)) return m->type;
    for (unsigned i = 0; i < m->effect_count; ++i) {
        const BattleEffect *e = &m->effects[i];
        if (e->kind == BEFF_WEATHER_TYPE && bc_weather_in(b, e->arg0)) return e->arg2;
    }
    return m->type;
}

bool bc_weather_accuracy(const Battle *b, const BattleMove *m, unsigned *accuracy) {
    if (!bc_weather_rules(b)) return false;
    for (unsigned i = 0; i < m->effect_count; ++i) {
        const BattleEffect *e = &m->effects[i];
        if (e->kind == BEFF_WEATHER_ACCURACY && bc_weather_in(b, e->arg0)) {
            *accuracy = e->arg2;
            return true;
        }
    }
    return false;
}

bool bc_weather_prevents_freeze(const Battle *b) {
    const uint8_t kind = battle_weather(b);
    return kind != BWEATHER_NONE && (b->request->field.weather[kind - 1].flags & BWKF_NO_FREEZE);
}

bool bc_weather_skips_charge(const Battle *b, const BattleEffect *charge) {
    return bc_weather_rules(b) && charge->arg2 && bc_weather_in(b, charge->arg2);
}

void bc_weather_heal_fraction(const Battle *b, const BattleMove *m, unsigned *num, unsigned *den) {
    if (!bc_weather_rules(b)) return;
    for (unsigned i = 0; i < m->effect_count; ++i) {
        const BattleEffect *e = &m->effects[i];
        if (e->kind == BEFF_WEATHER_HEAL && bc_weather_in(b, e->arg0)) {
            *num = (unsigned)e->arg1;
            *den = e->arg2;
            return;
        }
    }
}
