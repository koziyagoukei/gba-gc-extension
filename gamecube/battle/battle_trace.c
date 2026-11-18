// Trace equivalence tool for the battle core (docs/rule-poc.md).
//
// Builds a fixed corpus of V12 requests (every V12 damage modifier kind,
// unusual hook combinations, statuses, spread and multi-hit moves, all four
// formats), encodes and decodes each one, plays it with the AI on both
// sides and prints one line per battle: a hash of the encoded request, of
// every event in order (all fields), and of the final state and RNG.
// Building the same file against two versions of the core and comparing the
// output shows that a refactoring left V12 battles unchanged.
//
//   cc -std=c11 -O2 -Iprotocol -Igamecube/battle protocol/*.c gamecube/battle/battle_*.c
//      (without battle_test.c) -o build/battle-trace
//   build/battle-trace [version] > trace.txt     (version 12 by default; 13 relabels the same data)
//
// It uses only the public core API and the protocol records, so the same
// source builds against older trees.

#include "battle_core.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { T_NORMAL, T_FIRE, T_WATER, T_GHOST, T_ELECTRIC, T_GRASS, NTYPES };

static uint32_t lcg;
static unsigned rnd(unsigned n) {
    lcg = lcg * 1103515245u + 12345u;
    return (lcg >> 16) % n;
}

static uint32_t fnv(uint32_t h, const void *data, unsigned n) {
    const uint8_t *p = data;
    for (unsigned i = 0; i < n; ++i) h = (h ^ p[i]) * 16777619u;
    return h;
}
#define MIX(h, v) do { const uint32_t mix_v = (uint32_t)(v); h = fnv(h, &mix_v, sizeof mix_v); } while (0)

static BattleAbilityHook hk(uint8_t kind, uint8_t arg0, int8_t arg1, uint8_t arg2, uint8_t num, uint8_t den,
                            uint8_t percent, uint8_t flags) {
    BattleAbilityHook h;
    memset(&h, 0, sizeof h);
    h.kind = kind;
    h.arg0 = arg0;
    h.arg1 = arg1;
    h.arg2 = arg2;
    h.chance = num;
    h.chance_den = den;
    h.percent = percent;
    h.flags = flags;
    return h;
}

// V12 abilities: the damage modifier kinds alone and in combinations a
// refactoring could reorder (first-only kinds, two hooks of a kind, flags).
static BattleAbility ability(unsigned which) {
    BattleAbility a;
    memset(&a, 0, sizeof a);
    a.cls = BACLASS_HOOKS;
    a.source = (uint16_t)(100 + which);
#define H(...) a.hooks[a.count++] = hk(__VA_ARGS__)
    switch (which) {
    case 0: a.cls = BACLASS_NONE; break;
    case 1: a.cls = BACLASS_UNSUPPORTED; break;
    case 2: H(BABIL_ATTACK_MULT, BCAT_PHYSICAL, 0, 0, 0, 0, 200, 0); break;                     // Huge Power
    case 3: H(BABIL_ATTACK_MULT, BCAT_PHYSICAL, 0, 0, 0, 0, 150, BAHF_REQUIRES_STATUS);         // Guts
            H(BABIL_IGNORE_BURN, 0, 0, 0, 0, 0, 0, 0); break;
    case 4: H(BABIL_ABSORB_STATE, T_FIRE, 0, 0, 0, 0, 0, BAHF_NOT_FROZEN);                       // Flash Fire
            H(BABIL_ATTACK_MULT, 0, 0, T_FIRE, 0, 0, 150, BAHF_REQUIRES_STATE | BAHF_TYPE_FILTER); break;
    case 5: H(BABIL_PINCH, T_FIRE, 0, 3, 0, 0, 150, 0); break;                                   // Blaze
    case 6: H(BABIL_PINCH, T_WATER, 0, 2, 0, 0, 150, 0); break;
    case 7: H(BABIL_LOW_POWER, 60, 0, 0, 0, 0, 150, 0); break;                                   // Technician
    case 8: H(BABIL_RESIST, T_FIRE, 0, T_WATER, 0, 0, 50, BAHF_BREAKABLE); break;                // Thick Fat
    case 9: H(BABIL_RESIST, T_ELECTRIC, 0, BABIL_NO_TYPE, 0, 0, 50, 0); break;
    case 10: H(BABIL_DEFENSE_MULT, BCAT_PHYSICAL, 0, 0, 0, 0, 150, BAHF_REQUIRES_STATUS); break; // Marvel Scale
    case 11: H(BABIL_DEFENSE_MULT, BCAT_SPECIAL, 0, 0, 0, 0, 200, 0); break;
    case 12: H(BABIL_STAB_MULT, 0, 0, 0, 0, 0, 200, 0); break;                                   // Adaptability
    // Combinations: several kinds and their order within one stage.
    case 13: H(BABIL_PINCH, T_NORMAL, 0, 2, 0, 0, 150, 0);
             H(BABIL_ATTACK_MULT, 0, 0, 0, 0, 0, 130, 0);
             H(BABIL_ATTACK_MULT, BCAT_SPECIAL, 0, 0, 0, 0, 170, 0); break;
    case 14: H(BABIL_PINCH, T_GRASS, 0, 2, 0, 0, 150, 0);
             H(BABIL_PINCH, T_NORMAL, 0, 2, 0, 0, 200, 0); break;                                // first PINCH only
    case 15: H(BABIL_LOW_POWER, 50, 0, 0, 0, 0, 150, 0);
             H(BABIL_LOW_POWER, 90, 0, 0, 0, 0, 133, 0); break;                                  // first LOW_POWER only
    case 16: H(BABIL_RESIST, T_GRASS, 0, BABIL_NO_TYPE, 0, 0, 50, 0);
             H(BABIL_RESIST, T_NORMAL, 0, BABIL_NO_TYPE, 0, 0, 25, 0); break;                    // first RESIST only
    case 17: H(BABIL_DEFENSE_MULT, BCAT_PHYSICAL, 0, 0, 0, 0, 150, BAHF_REQUIRES_STATUS);
             H(BABIL_DEFENSE_MULT, BCAT_PHYSICAL, 0, 0, 0, 0, 133, 0);
             H(BABIL_RESIST, T_FIRE, 0, BABIL_NO_TYPE, 0, 0, 50, 0); break;
    case 18: H(BABIL_ATTACK_MULT, BCAT_PHYSICAL, 0, 0, 0, 0, 150, BAHF_REQUIRES_STATUS);
             H(BABIL_LOW_POWER, 60, 0, 0, 0, 0, 150, 0);
             H(BABIL_PINCH, T_NORMAL, 0, 3, 0, 0, 150, 0);
             H(BABIL_DEFENSE_MULT, BCAT_SPECIAL, 0, 0, 0, 0, 150, BAHF_REQUIRES_STATUS); break;
    case 19: H(BABIL_STURDY, 0, 0, 0, 0, 0, 0, 0);
             H(BABIL_ATTACK_MULT, BCAT_PHYSICAL, 0, 0, 0, 0, 200, 0); break;
    // Other hooks, so the modifier paths run next to everything else.
    case 20: H(BABIL_CONTACT_STATUS, BST_PARALYSIS, 0, 0, 30, 100, 0, 0); break;
    case 21: H(BABIL_CONTACT_DAMAGE, 0, 0, 8, 0, 0, 0, 0); break;
    case 22: H(BABIL_ENTRY_STAT, BSTAT_ATK, -1, BRANGE_ADJACENT_FOES, 0, 0, 0, 0); break;
    case 23: H(BABIL_END_TURN_STAT, BSTAT_SPE, 1, 0, 0, 0, 0, 0); break;
    case 24: H(BABIL_SECONDARY_CHANCE, 0, 0, 0, 0, 0, 200, 0); break;
    case 25: H(BABIL_TYPE_IMMUNE, T_ELECTRIC, 0, 0, 0, 0, 0, 0); break;
    case 26: H(BABIL_TYPE_PASS, T_NORMAL, 0, T_GHOST, 0, 0, 0, 0);
             H(BABIL_VOLATILE_IMMUNE, BABIL_VOL_INTIMIDATE, 0, 0, 0, 0, 0, 0); break;
    case 27: H(BABIL_KO_STAT, BSTAT_ATK, 1, 0, 0, 0, 0, 0); break;
    case 28: H(BABIL_END_TURN_CURE, 0xfe, 0, 0, 1, 3, 0, 0); break;
    case 29: H(BABIL_STATUS_MIRROR, 0x1e, 0, 0, 0, 0, 0, BAHF_TOXIC_TO_POISON); break;
    case 30: H(BABIL_ACCURACY, 0, 0, 0, 0, 0, 130, 0); break;
    default: H(BABIL_NO_GUARD, 0, 0, 0, 0, 0, 0, 0); break;
    }
#undef H
    return a;
}
#define ABILITY_KINDS 32

static BattleEffect fx(uint8_t kind, uint8_t who, uint8_t chance, uint8_t arg0, int8_t arg1, uint8_t arg2) {
    BattleEffect e;
    memset(&e, 0, sizeof e);
    e.kind = kind;
    e.who = who;
    e.chance = chance;
    e.arg0 = arg0;
    e.arg1 = arg1;
    e.arg2 = arg2;
    return e;
}

static BattleMove random_move(void) {
    BattleMove m;
    memset(&m, 0, sizeof m);
    m.accuracy = (uint8_t)(rnd(4) ? 100 : 85);
    m.target = BTARGET_SINGLE;
    m.type = (uint8_t)rnd(NTYPES);
    m.category = (uint8_t)(rnd(2) ? BCAT_PHYSICAL : BCAT_SPECIAL);
    m.power = (uint16_t)(20 + 5 * rnd(19));
#define PUSH(e) m.effects[m.effect_count++] = (e)
    switch (rnd(10)) {
    case 0:  // multi-hit contact
        m.category = BCAT_PHYSICAL;
        m.power = 25;
        m.move_flags = BMF_CONTACT;
        PUSH(fx(BEFF_DAMAGE, BWHO_TARGET, 0, 2, BHITS_2TO5_GEN5, 5));
        break;
    case 1:  // spread
        m.target = rnd(2) ? BTARGET_ALL_FOES : BTARGET_ALL_ADJACENT;
        PUSH(fx(BEFF_DAMAGE, BWHO_TARGET, 0, 1, 0, 1));
        break;
    case 2:  // secondary status
        PUSH(fx(BEFF_DAMAGE, BWHO_TARGET, 0, 1, 0, 1));
        PUSH(fx(BEFF_STATUS, BWHO_TARGET, 30, rnd(2) ? BST_BURN : BST_PARALYSIS, 0, 0));
        break;
    case 3:  // recoil
        m.category = BCAT_PHYSICAL;
        m.move_flags = BMF_CONTACT;
        PUSH(fx(BEFF_DAMAGE, BWHO_TARGET, 0, 1, 0, 1));
        PUSH(fx(BEFF_RECOIL, BWHO_USER, 0, 33, 0, 0));
        break;
    case 4: {  // status move
        static const uint8_t statuses[] = {BST_BURN, BST_PARALYSIS, BST_POISON, BST_SLEEP};
        m.category = BCAT_STATUS;
        m.power = 0;
        PUSH(fx(BEFF_STATUS, BWHO_TARGET, 0, statuses[rnd(4)], 0, 0));
        break;
    }
    case 5:  // self boost
        m.category = BCAT_STATUS;
        m.power = 0;
        m.target = BTARGET_SELF;
        m.type = T_NORMAL;
        PUSH(fx(BEFF_STAT, BWHO_USER, 0, rnd(2) ? BSTAT_ATK : BSTAT_SPA, 2, 0));
        break;
    case 6:  // Struggle-like typeless? no: a weak contact move for Technician
        m.category = BCAT_PHYSICAL;
        m.power = (uint16_t)(30 + 10 * rnd(5));
        m.move_flags = BMF_CONTACT;
        PUSH(fx(BEFF_DAMAGE, BWHO_TARGET, 0, 1, 0, 1));
        break;
    default:
        if (m.category == BCAT_PHYSICAL) m.move_flags = BMF_CONTACT;
        PUSH(fx(BEFF_DAMAGE, BWHO_TARGET, 0, 1, 0, 1));
        break;
    }
#undef PUSH
    assert(battle_move_effects_valid(&m));
    return m;
}

static void random_entry(BattleEntry *e, uint8_t slot, unsigned format, unsigned index) {
    memset(e, 0, sizeof *e);
    PokemonRecord *r = &e->record;
    r->version = PKMN_RECORD_VERSION;
    r->slot = slot;
    r->species = (uint16_t)(1 + rnd(380));
    r->level = (uint8_t)(20 + rnd(41));
    r->max_hp = (uint16_t)(40 + rnd(81));
    r->hp = rnd(3) ? r->max_hp : (uint16_t)(1 + rnd(r->max_hp));
    r->attack = (uint16_t)(30 + rnd(91));
    r->defense = (uint16_t)(30 + rnd(91));
    r->sp_attack = (uint16_t)(30 + rnd(91));
    r->sp_defense = (uint16_t)(30 + rnd(91));
    r->speed = (uint16_t)(30 + rnd(91));
    static const uint8_t statuses[] = {BST_NONE, BST_NONE, BST_NONE, BST_BURN, BST_PARALYSIS, BST_POISON};
    r->status = statuses[rnd(6)];
    memset(r->nickname, 0xff, sizeof r->nickname);
    e->mon.type_count = (uint8_t)(1 + rnd(2));
    e->mon.types[0] = (uint8_t)rnd(NTYPES);
    e->mon.types[1] = e->mon.type_count == 2 ? (uint8_t)rnd(NTYPES) : e->mon.types[0];
    for (unsigned m = 0; m < 4; ++m) {
        e->mon.moves[m] = random_move();
        r->moves[m] = (uint16_t)(1 + rnd(700));
        r->pp[m] = r->max_pp[m] = (uint8_t)(5 + rnd(20));
    }
    e->mon.ability = ability((rnd(ABILITY_KINDS) + format + index) % ABILITY_KINDS);
    r->ability = e->mon.ability.source;
    assert(battle_ability_valid(&e->mon.ability, NTYPES, BATTLE_REQUEST_VERSION_ABILITY_HOOKS));
}

static void build(BattleRequest *r, uint8_t format, uint32_t seed, uint8_t version) {
    memset(r, 0, sizeof *r);
    lcg = seed * 2654435761u + format;
    const uint8_t active = (uint8_t)battle_formation_size(format);
    r->version = version;
    r->format = format;
    r->battle_type = BTYPE_TRAINER;
    r->active_per_side = active;
    r->player_count = r->opponent_count = (uint8_t)(format == BATTLE_FORMAT_ROTATION ? 3 : active + 1);
    r->rules = RULE_IGNORE_ITEMS | RULE_NO_GIMMICKS | RULE_NO_CRITICAL_HITS;
    if (seed % 11 == 0) r->rules |= RULE_IGNORE_ABILITIES;
    r->trainer_id = 7;
    r->result_policy = RESULT_POLICY_DISPLAY_ONLY;
    r->type_count = NTYPES;
    r->battle_id = seed;
    r->struggle_power = 50;
    r->struggle_recoil = BSTRUGGLE_MAX_HP_NEAREST;
    for (unsigned a = 0; a < NTYPES; ++a)
        for (unsigned d = 0; d < NTYPES; ++d) r->type_chart[a * NTYPES + d] = 4;
    r->type_chart[T_FIRE * NTYPES + T_WATER] = 2;
    r->type_chart[T_FIRE * NTYPES + T_GRASS] = 8;
    r->type_chart[T_WATER * NTYPES + T_FIRE] = 8;
    r->type_chart[T_GRASS * NTYPES + T_WATER] = 8;
    r->type_chart[T_ELECTRIC * NTYPES + T_WATER] = 8;
    r->type_chart[T_ELECTRIC * NTYPES + T_GRASS] = 2;
    r->type_chart[T_NORMAL * NTYPES + T_GHOST] = 0;
    r->type_chart[T_GHOST * NTYPES + T_NORMAL] = 0;
    r->conditions = (BattleConditionRules){
        .flags = BCR_ELECTRIC_IMMUNE | BCR_DYNAMIC_SPEED | BCR_THAW_ON_FIRE_HIT,
        .poison_type = T_GHOST, .steel_type = T_GHOST, .fire_type = T_FIRE, .electric_type = T_ELECTRIC,
        .ice_type = T_WATER, .paralysis_speed_divisor = 2, .paralysis_block_divisor = 4,
        .burn_residual_divisor = 16, .sleep_min = 2, .sleep_max = 4, .thaw_percent = 20,
    };
    r->volatiles = (BattleVolatileRules){.flags = BVR_SUBSTITUTE_MULTI_HIT_CONTINUES, .confusion_hit_percent = 33,
                                         .confusion_min = 2, .confusion_max = 5, .confusion_power = 40,
                                         .substitute_divisor = 4};
    for (unsigned i = 0; i < r->player_count; ++i) random_entry(&r->player[i], (uint8_t)i, format, i);
    for (unsigned i = 0; i < r->opponent_count; ++i) random_entry(&r->opponent[i], (uint8_t)(0x80 + i), format, i + 3);
    // Leads must stand.
    for (unsigned i = 0; i < active; ++i) {
        if (!r->player[i].record.hp) r->player[i].record.hp = 1;
        if (!r->opponent[i].record.hp) r->opponent[i].record.hp = 1;
    }
}

static uint32_t hash_events(uint32_t h, const EventLog *log) {
    for (unsigned i = 0; i < log->count; ++i) {
        const BattleEvent *e = &log->events[i];
        MIX(h, e->kind);
        MIX(h, e->user.side << 8 | e->user.slot);
        MIX(h, e->target.side << 8 | e->target.slot);
        MIX(h, e->move_id);
        MIX(h, e->amount);
        MIX(h, e->hp_after);
        MIX(h, e->effectiveness);
        MIX(h, e->outcome);
        MIX(h, e->rotation);
        MIX(h, e->battler);
        MIX(h, e->condition);
        MIX(h, e->stat);
        MIX(h, e->stage);
        MIX(h, e->ability);
    }
    return h;
}

static uint32_t hash_state(const Battle *b) {
    uint32_t h = 2166136261u;
    for (unsigned side = 0; side < 2; ++side)
        for (unsigned i = 0; i < b->sides[side].count; ++i) {
            const Battler *x = &b->sides[side].battlers[i];
            MIX(h, x->hp);
            MIX(h, x->status);
            MIX(h, x->fainted);
            for (unsigned s = 0; s < BSTAT_COUNT; ++s) MIX(h, x->stat_stages[s]);
            for (unsigned m = 0; m < 4; ++m) MIX(h, x->pp[m]);
        }
    MIX(h, b->rng);
    MIX(h, b->turn);
    MIX(h, b->outcome);
    return h;
}

int main(int argc, char **argv) {
    const uint8_t version = (uint8_t)(argc > 1 ? atoi(argv[1]) : BATTLE_REQUEST_VERSION_ABILITY_HOOKS);
    const unsigned seeds = argc > 2 ? (unsigned)atoi(argv[2]) : 300;
    static BattleRequest built, decoded;
    static uint8_t bytes[8192];
    static Battle b;
    static EventLog log;
    uint32_t total = 2166136261u;
    unsigned battles = 0, damage_events = 0;
    for (uint8_t format = BATTLE_FORMAT_SINGLE; format <= BATTLE_FORMAT_ROTATION; ++format)
        for (uint32_t seed = 1; seed <= seeds; ++seed) {
            build(&built, format, seed, version);
            const uint32_t n = battle_request_encode(&built, bytes, sizeof bytes);
            if (!n || !battle_request_decode(bytes, n, &decoded)) {
                printf("f=%u s=%u codec failed (n=%u)\n", format, seed, n);
                return 1;
            }
            // The wire bytes after the version byte (a relabelled request differs only there).
            const uint32_t wire = fnv(2166136261u, bytes + 1, n - 1);
            const int init = battle_init(&b, &decoded, seed);
            uint32_t events = 2166136261u;
            if (init == BI_OK) {
                log.count = 0;
                battle_begin(&b, &log);
                events = hash_events(events, &log);
                for (unsigned t = 0; t < 60 && b.outcome == OUTCOME_NONE; ++t) {
                    Position pending;
                    while (battle_pending_replacement(&b, &pending)) {
                        const uint8_t mask = battle_replacement_mask(&b, pending);
                        uint8_t pick = 0;
                        while (!(mask & (1u << pick))) ++pick;
                        log.count = 0;
                        battle_replace(&b, pending, pick, &log);
                        events = hash_events(events, &log);
                    }
                    if (b.outcome != OUTCOME_NONE) break;
                    Action actions[BC_MAX_ACTIONS];
                    unsigned count = 0;
                    for (uint8_t side = 0; side < 2; ++side) {
                        Position actors[BC_MAX_ACTIVE];
                        const unsigned k = battle_collect_actors(&b, side, actors);
                        for (unsigned i = 0; i < k; ++i) battle_choose_at(&b, actors[i], &actions[count++]);
                    }
                    log.count = 0;
                    battle_run_turn(&b, actions, count, &log);
                    for (unsigned i = 0; i < log.count; ++i) damage_events += log.events[i].kind == EV_DAMAGE;
                    events = hash_events(events, &log);
                }
            }
            const uint32_t state = hash_state(&b);
            printf("f=%u s=%u bytes=%u wire=%08x init=%d events=%08x state=%08x turns=%u outcome=%u\n", format, seed, n,
                   wire, init, events, state, b.turn, b.outcome);
            MIX(total, events);
            MIX(total, state);
            ++battles;
        }
    printf("battles=%u damage_events=%u total=%08x\n", battles, damage_events, total);
    return 0;
}
