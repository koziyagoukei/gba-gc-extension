// Replays a fixed-seed battle from a Dolphin log on the native core
// (docs/rule-poc.md).
//
// The companion logs the request bytes of a RULE_FIXED_SEED battle
// ("PH9 REQ"), the player's commands ("MULTI ACTION", "ROSTER PLAYER") and
// every event and state line ("PH3 EVENT", "PH8 STATE"). This tool decodes
// the same bytes, seeds the core with the battle id, gives it the same
// commands in the companion's order (players, then the AI for the
// opponents) and compares its own event and state lines with the logged
// ones.
//
//   cc -std=c11 -O2 -Iprotocol -Igamecube/battle protocol/*.c gamecube/battle/battle_core.c
//      gamecube/battle/battle_effects.c gamecube/battle/battle_abilities.c
//      gamecube/battle/battle_modifiers.c gamecube/battle/battle_replay.c -o build/battle-replay
//   build/battle-replay dolphin.log [-print | -without]
// -without removes every V13 modifier record first (same bytes otherwise,
// same seed and commands) and prints the damage events: the counterfactual.

#include "battle_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINES 20000
static char *expected[MAX_LINES];
static unsigned expected_count, compared, mismatches;
static bool print_all, without;

typedef struct { unsigned turn, actor, kind, slot, target, rotation; } Command;
static Command commands[512];
static unsigned command_count, command_next;
typedef struct { unsigned pivot, pos, roster; } Roster;
static Roster rosters[64];
static unsigned roster_count, roster_next;
static uint8_t bytes[8192];
static unsigned byte_count;
static uint32_t seed;

static void check(const char *line) {
    if (without) {  // the counterfactual: only its damage lines
        if (strstr(line, " kind=2 ")) printf("%s\n", line);
        return;
    }
    if (print_all) printf("%s\n", line);
    if (compared >= expected_count || strcmp(expected[compared], line) != 0) {
        if (mismatches++ < 5)
            printf("MISMATCH at %u\n  log:    %s\n  native: %s\n", compared,
                   compared < expected_count ? expected[compared] : "(end)", line);
    }
    ++compared;
}

static void emit(const Battle *b, const EventLog *log, unsigned turn) {
    char line[512];
    for (unsigned i = 0; i < log->count; ++i) {
        const BattleEvent *e = &log->events[i];
        snprintf(line, sizeof line,
                 "PH3 EVENT turn=%u kind=%u user=%u target=%u move=%u amount=%u hp_after=%u eff=%u outcome=%u pos=%u:%u "
                 "rotation=%u battler=%u condition=%u stat=%u stage=%d ability=%u",
                 turn, e->kind, e->user.side, e->target.side, e->move_id, e->amount, e->hp_after, e->effectiveness,
                 e->outcome, e->user.slot, e->target.slot, e->rotation, e->battler, e->condition, e->stat, e->stage,
                 e->ability);
        check(line);
    }
    for (uint8_t side = 0; side < 2; ++side)
        for (uint8_t i = 0; i < b->sides[side].count; ++i) {
            const Battler *x = &b->sides[side].battlers[i];
            int pos = -1;
            for (uint8_t s = 0; s < b->sides[side].active_count; ++s)
                if (b->sides[side].active[s] == i) pos = s;
            snprintf(line, sizeof line,
                     "PH8 STATE turn=%u side=%u entry=%u pos=%d hp=%u/%u status=%04x stages=%d,%d,%d,%d,%d,%d,%d"
                     " sub=%u conf=%u state=%u%s", turn, side, i, pos, x->hp, x->max_hp, (unsigned)x->status,
                     x->stat_stages[0], x->stat_stages[1], x->stat_stages[2], x->stat_stages[3], x->stat_stages[4],
                     x->stat_stages[5], x->stat_stages[6], x->vol.substitute_hp, x->vol.confusion, x->vol.ability_state,
                     x->fainted ? " fainted" : "");
            check(line);
        }
}

static void strip(char *s) {
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) s[--n] = 0;
}

// Keeps the lines of the last fixed-seed battle in the log.
static bool parse(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    static char raw[4096];
    while (fgets(raw, sizeof raw, f)) {
        char *p = strstr(raw, "OSREPORT]: ");
        if (!p) continue;
        p += 11;
        strip(p);
        unsigned at, a[6];
        char hex[80];
        if (sscanf(p, "PH9 REQ %x %70s", &at, hex) == 2) {
            if (at == 0) {  // a new battle: forget the previous one
                byte_count = expected_count = command_count = roster_count = 0;
                seed = 0;
            }
            for (unsigned i = 0; hex[2 * i] && hex[2 * i + 1] && at + i < sizeof bytes; ++i) {
                unsigned v;
                sscanf(hex + 2 * i, "%2x", &v);
                bytes[at + i] = (uint8_t)v;
                if (at + i + 1 > byte_count) byte_count = at + i + 1;
            }
        } else if (strncmp(p, "PH3 CORE init=", 14) == 0 && strstr(p, "seed=")) {
            sscanf(strstr(p, "seed=") + 5, "%x", &seed);
        } else if (sscanf(p, "MULTI ACTION turn=%u actor=%u kind=%u move_slot=%u target=%u rotation=%u", &a[0], &a[1], &a[2],
                          &a[3], &a[4], &a[5]) == 6) {
            if (command_count < 512) commands[command_count++] = (Command){a[0], a[1], a[2], a[3], a[4], a[5]};
        } else if (sscanf(p, "ROSTER PLAYER SEND OUT pos=%u roster=%u", &a[0], &a[1]) == 2) {
            if (roster_count < 64) rosters[roster_count++] = (Roster){0, a[0], a[1]};
        } else if (sscanf(p, "ROSTER PLAYER PIVOT pos=%u roster=%u", &a[0], &a[1]) == 2) {
            if (roster_count < 64) rosters[roster_count++] = (Roster){1, a[0], a[1]};
        } else if ((strncmp(p, "PH3 EVENT ", 10) == 0 || strncmp(p, "PH8 STATE ", 10) == 0) && expected_count < MAX_LINES) {
            expected[expected_count] = malloc(strlen(p) + 1);
            strcpy(expected[expected_count++], p);
        }
    }
    fclose(f);
    return byte_count && seed;
}

int main(int argc, char **argv) {
    if (argc < 2 || !parse(argv[1])) {
        fprintf(stderr, "usage: battle-replay dolphin.log [-print] (needs a PH9 REQ dump)\n");
        return 2;
    }
    print_all = argc > 2 && strcmp(argv[2], "-print") == 0;
    without = argc > 2 && strcmp(argv[2], "-without") == 0;
    static BattleRequest r;
    static Battle b;
    static EventLog log;
    if (!battle_request_decode(bytes, byte_count, &r)) {
        printf("request (%u bytes) does not decode\n", byte_count);
        return 1;
    }
#ifdef BATTLE_REQUEST_VERSION_MODIFIERS  // (absent when built against a V12 tree)
    for (unsigned side = 0; without && side < 2; ++side)
        for (unsigned i = 0; i < (side ? r.opponent_count : r.player_count); ++i) {
            BattleMon *m = side ? &r.opponent[i].mon : &r.player[i].mon;
            for (unsigned k = 0; k < 4; ++k) {
                BattleMove *move = &m->moves[k];
                unsigned kept = 0;
                for (unsigned e = 0; e < move->effect_count; ++e)
                    if (move->effects[e].kind != BEFF_MODIFIER) move->effects[kept++] = move->effects[e];
                move->effect_count = (uint8_t)kept;
            }
            unsigned kept = 0;
            for (unsigned h = 0; h < m->ability.count; ++h)
                if (m->ability.hooks[h].kind != BABIL_MODIFIER) m->ability.hooks[kept++] = m->ability.hooks[h];
            if (m->ability.cls == BACLASS_HOOKS && !kept) m->ability.cls = BACLASS_NONE;
            m->ability.count = (uint8_t)kept;
        }
#endif
    const int init = battle_init(&b, &r, seed);
    printf("request v%u format=%u bytes=%u seed=%08x init=%d, %u logged lines, %u commands\n", r.version, r.format,
           byte_count, seed, init, expected_count, command_count);
    if (init != BI_OK) return init == BI_UNSUPPORTED_ABILITY && !expected_count ? 0 : 1;
    battle_begin(&b, &log);
    if (log.count) emit(&b, &log, 0);
    while (b.outcome == OUTCOME_NONE) {
        Position pos;
        const bool pivot = battle_pending_pivot(&b, &pos);
        if (pivot || battle_pending_replacement(&b, &pos)) {
            if (pos.side == 1) {
                for (uint8_t i = 0; i < BATTLE_MAX_PER_SIDE; ++i)
                    if (battle_replace(&b, pos, i, &log)) break;
                emit(&b, &log, b.turn);
                continue;
            }
            if (roster_next >= roster_count) break;
            const Roster *x = &rosters[roster_next++];
            const unsigned turn = b.turn + pivot;
            if (!(pivot ? battle_resume_pivot(&b, (uint8_t)x->roster, &log)
                        : battle_replace(&b, pos, (uint8_t)x->roster, &log)))
                break;
            emit(&b, &log, turn);
            continue;
        }
        const unsigned turn = b.turn + 1u;
        Position actors[BC_MAX_ACTIVE], players[BC_MAX_ACTIVE];
        unsigned n = battle_collect_actors(&b, 0, actors), count = 0;
        for (unsigned i = 0; i < n; ++i) {
            Action forced;
            if (!battle_forced_action(&b, actors[i], &forced)) players[count++] = actors[i];
        }
        Action actions[BC_MAX_ACTIONS];
        unsigned k = 0;
        bool forfeit = false;
        for (unsigned i = 0; i < count; ++i) {
            if (command_next >= command_count || commands[command_next].turn != turn) {
                forfeit = true;  // no command logged: the player forfeited at this prompt
                actions[0] = (Action){.kind = ACT_FORFEIT, .user = players[i]};
                k = 1;
                break;
            }
            const Command *c = &commands[command_next++];
            actions[k++] = (Action){.kind = (ActionKind)c->kind, .user = {0, (uint8_t)c->actor},
                                    .move_slot = (uint8_t)c->slot, .target = {{1, (uint8_t)c->target}},
                                    .rotation = (uint8_t)c->rotation, .reserve = (uint8_t)c->target};
        }
        if (!forfeit) {
            n = battle_collect_actors(&b, 1, actors);
            for (unsigned i = 0; i < n; ++i) battle_choose_at(&b, actors[i], &actions[k++]);
        }
        battle_run_turn(&b, actions, k, &log);
        emit(&b, &log, turn);
        if (forfeit) break;
    }
    printf("compared %u lines (%u logged): %u mismatches; outcome=%u turns=%u rng=%08x\n", compared, expected_count,
           mismatches, b.outcome, b.turn, b.rng);
    return without ? 0 : mismatches || compared != expected_count;
}
