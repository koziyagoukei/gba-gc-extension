// Desktop tests for the battle protocol codecs and the battle core.
#include "battle_core.h"
#include "battle_internal.h"  // bc_calculate_damage (spread / semi-invulnerable flags)
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { T_NORMAL = 0, T_FIRE = 1, T_WATER = 2, T_GHOST = 3, NTYPES = 4 };

static BattleMove hit(uint16_t power, uint8_t type, uint8_t category) {
    return (BattleMove){.power = power, .type = type, .category = category, .accuracy = 100, .target = BTARGET_SINGLE};
}

static void make_entry(BattleEntry *e, uint8_t slot, uint16_t species, uint8_t level, uint16_t hp, uint16_t stat,
                       uint16_t speed, uint8_t type) {
    memset(e, 0, sizeof(*e));
    e->record.version = PKMN_RECORD_VERSION;
    e->record.slot = slot;
    e->record.species = species;
    e->record.level = level;
    e->record.hp = e->record.max_hp = hp;
    e->record.attack = e->record.defense = e->record.sp_attack = e->record.sp_defense = stat;
    e->record.speed = speed;
    memset(e->record.nickname, 0xff, sizeof(e->record.nickname));
    e->mon.type_count = 1;
    e->mon.types[0] = e->mon.types[1] = type;
    for (unsigned i = 0; i < 4; ++i) e->mon.moves[i].unsupported = UNSUP_EMPTY;
}

static void set_move(BattleEntry *e, unsigned i, uint16_t id, BattleMove m, uint8_t pp) {
    e->record.moves[i] = id;
    e->record.pp[i] = e->record.max_pp[i] = pp;
    e->mon.moves[i] = m;
}

static void make_request(BattleRequest *r) {
    memset(r, 0, sizeof(*r));
    r->format = BATTLE_FORMAT_SINGLE;
    r->battle_type = BTYPE_TRAINER;
    r->player_count = r->opponent_count = r->active_per_side = 1;
    r->rules = RULE_IGNORE_ABILITIES | RULE_IGNORE_ITEMS | RULE_NO_GIMMICKS | RULE_NO_CRITICAL_HITS;
    r->trainer_id = 42;
    r->type_count = NTYPES;
    r->battle_id = 0x1234;
    for (unsigned a = 0; a < NTYPES; ++a)
        for (unsigned d = 0; d < NTYPES; ++d) r->type_chart[a * NTYPES + d] = 4;
    r->type_chart[T_FIRE * NTYPES + T_WATER] = 2;
    r->type_chart[T_WATER * NTYPES + T_FIRE] = 8;
    r->type_chart[T_NORMAL * NTYPES + T_GHOST] = 0;
    make_entry(&r->player[0], 0, 7, 50, 120, 60, 80, T_WATER);
    set_move(&r->player[0], 0, 55, hit(40, T_WATER, BCAT_SPECIAL), 25);
    BattleMove secondary = hit(90, T_WATER, BCAT_SPECIAL);
    secondary.unsupported = UNSUP_SECONDARY;
    set_move(&r->player[0], 1, 57, secondary, 15);
    make_entry(&r->opponent[0], 0x80, 4, 50, 100, 60, 70, T_FIRE);
    set_move(&r->opponent[0], 0, 52, hit(40, T_FIRE, BCAT_SPECIAL), 25);
}

static void test_request_roundtrip(void) {
    static BattleRequest in, out;
    static uint8_t bytes[2048];
    make_request(&in);
    in.environment = 7;
    in.player[0].mon.front_y = -3;
    in.player[0].mon.back_y = 12;
    in.player[0].mon.elevation = 9;
    const uint32_t size = battle_request_encode(&in, bytes, sizeof(bytes));
    assert(size == BATTLE_REQUEST_HEADER + 2 * BATTLE_ENTRY_BYTES + NTYPES * NTYPES);
    assert(battle_request_encode(&in, bytes, size - 1) == 0);
    const unsigned reserved = BATTLE_REQUEST_HEADER + PKMN_RECORD_BYTES + 4;
    for (unsigned i = 0; i < 4; ++i) assert(bytes[reserved + i] == 0);
    // Old frontier ROMs still populate the V1 sprite-placement bytes.
    bytes[reserved] = 0xfd;
    bytes[reserved + 1] = 12;
    bytes[reserved + 2] = 9;
    assert(battle_request_decode(bytes, size, &out));
    assert(out.format == BATTLE_FORMAT_SINGLE && out.trainer_id == 42 && out.battle_id == 0x1234);
    assert(out.player[0].record.species == 7 && out.opponent[0].record.slot == 0x80);
    assert(out.player[0].mon.moves[1].unsupported == UNSUP_SECONDARY && out.player[0].mon.moves[0].power == 40);
    assert(out.type_chart[T_WATER * NTYPES + T_FIRE] == 8);
    assert(out.environment == 7);
    assert(!out.player[0].mon.front_y && !out.player[0].mon.back_y && !out.player[0].mon.elevation);
    assert(!battle_request_decode(bytes, size - 1, &out));
    bytes[0] = 9;
    assert(!battle_request_decode(bytes, size, &out));
}

static void test_messages(void) {
    const BattleState s = {BPHASE_CHOOSE, 3, 0, 1, 50, 120, 10, 100, {1, 2, 3, 4}};
    BattleState so;
    uint8_t buf[64];
    assert(battle_state_encode(&s, buf) == BATTLE_STATE_BYTES && battle_state_decode(buf, BATTLE_STATE_BYTES, &so));
    assert(so.turn == 3 && so.player_hp == 50 && so.opponent_max_hp == 100 && so.pp[3] == 4);
    const BattleAction a = {3, BACTION_MOVE, 2, BACTION_FLAG_EXIT};
    BattleAction ao;
    assert(battle_action_encode(&a, buf) == 4 && battle_action_decode(buf, 4, &ao) && ao.move_slot == 2 && ao.flags == 1);
    buf[2] = 4;
    assert(!battle_action_decode(buf, 4, &ao));
    BattleResult r = {OUTCOME_WIN, 4, 1, 1, 0xabcd, {{0}}, {0}}, ro;
    r.player[0].slot = 2; r.player[0].hp = 77; r.player[0].status = 8; r.player[0].pp[1] = 9;
    const unsigned n = battle_result_encode(&r, buf);
    assert(n == 8 + 12 + 2 && n <= RESULT_MAX_PAYLOAD);
    assert(battle_result_decode(buf, n, &ro) && ro.outcome == OUTCOME_WIN && ro.player[0].hp == 77 &&
           ro.player[0].pp[1] == 9 && ro.battle_id == 0xabcd);
    assert(!battle_result_decode(buf, n - 1, &ro));
}

static void test_core(void) {
    static BattleRequest r;
    Battle b;
    EventLog log;
    make_request(&r);
    assert(battle_init(&b, &r, 12345) == BI_OK);
    Battler *p = battle_active(&b, 0), *o = battle_active(&b, 1);
    assert(battle_usable_moves(&b, p) == 1);  // secondary-effect move refused
    uint8_t eff;
    // Level 50, power 40, 60/60: ((22*40*60/60)/50)+2 = 19; x1.5 STAB = 28; x2 super effective = 56.
    assert(battle_damage(&b, p, &p->moves[0], o, 100, &eff) == 56 && eff == 8);
    assert(battle_damage(&b, p, &p->moves[0], o, 85, &eff) == (19 * 85 / 100) * 3 / 2 * 2);
    assert(battle_damage(&b, o, &o->moves[0], p, 100, &eff) == 14 && eff == 2);

    BattleMove spread = hit(40, T_WATER, BCAT_SPECIAL);
    spread.target = BTARGET_ALL_FOES;
    assert(battle_move_supported(&r, &spread));
    r.format = BATTLE_FORMAT_DOUBLE;
    assert(battle_move_supported(&r, &spread));
    r.format = BATTLE_FORMAT_SINGLE;
    Action bad = {.kind = ACT_MOVE, .move_slot = 1, .target = {{1, 0}}};
    assert(!battle_validate_action(&b, &bad));
    Action acts[2] = {{.kind = ACT_MOVE, .target = {{1, 0}}}, {0}};
    battle_choose_ai(&b, 1, &acts[1]);
    assert(acts[1].kind == ACT_MOVE && acts[1].move_slot == 0);
    battle_run_turn(&b, acts, 2, &log);
    // The faster player moves first.
    assert(log.count >= 4 && log.events[0].kind == EV_MOVE_USED && log.events[0].user.side == 0);
    assert(p->pp[0] == 24 && o->pp[0] == 24 && b.turn == 1);
    assert(o->hp < 100 && p->hp < 120 && b.outcome == OUTCOME_NONE);
    unsigned turns = 1;
    while (b.outcome == OUTCOME_NONE && turns < 20) {
        battle_choose_ai(&b, 1, &acts[1]);
        battle_run_turn(&b, acts, 2, &log);
        ++turns;
    }
    assert(b.outcome == OUTCOME_WIN && o->fainted && o->hp == 0 && !p->fainted);
    assert(log.events[log.count - 1].kind == EV_END && log.events[log.count - 2].kind == EV_FAINT);
    printf("win after %u turns, player hp %u/%u\n", turns, p->hp, p->max_hp);

    // Immunity and forfeit.
    make_request(&r);
    r.opponent[0].mon.types[0] = r.opponent[0].mon.types[1] = T_GHOST;
    r.player[0].mon.moves[0].type = T_NORMAL;
    assert(battle_init(&b, &r, 1) == BI_OK);
    assert(battle_damage(&b, battle_active(&b, 0), &battle_active(&b, 0)->moves[0], battle_active(&b, 1), 100, &eff) == 0 && eff == 0);
    Action forfeit[1] = {{.kind = ACT_FORFEIT}};
    battle_run_turn(&b, forfeit, 1, &log);
    assert(b.outcome == OUTCOME_CANCELLED);

    // Requests the core cannot run are refused before the battle starts.
    make_request(&r);
    r.format = BATTLE_FORMAT_TRIPLE;
    assert(battle_init(&b, &r, 1) == BI_FORMAT);
    make_request(&r);
    r.player[0].mon.moves[0].unsupported = UNSUP_EFFECT;
    assert(battle_init(&b, &r, 1) == BI_NO_PLAYER_MOVES);
    make_request(&r);
    r.player_count = 2;
    assert(battle_init(&b, &r, 1) == BI_PARTY_SIZE);
}

static void make_multi(BattleRequest *r, uint8_t format) {
    make_request(r);
    r->format = format;
    r->player_count = r->opponent_count = r->active_per_side = battle_formation_size(format);
    for (unsigned side = 0; side < 2; ++side) {
        BattleEntry *entries = side ? r->opponent : r->player;
        for (uint8_t i = 0; i < r->active_per_side; ++i) {
            make_entry(&entries[i], 0x80 + 3 * side + i, 4 + i, 50, 200, 60, 60 + 20*i + 10*side, T_NORMAL);
            set_move(&entries[i], 0, 33, hit(40, T_NORMAL, BCAT_PHYSICAL), 20);
            entries[i].mon.moves[0].accuracy = 0;
        }
    }
}

static Action move(uint8_t side, uint8_t source, uint8_t target) {
    return (Action){.kind = ACT_MOVE, .user = {side, source}, .target = {{1 - side, target}}};
}

static unsigned event_count(const EventLog *log, EventKind kind) {
    unsigned count = 0;
    for (unsigned i = 0; i < log->count; ++i)
        count += log->events[i].kind == kind;
    return count;
}

static void test_double(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    make_multi(&r, BATTLE_FORMAT_DOUBLE);
    r.player[0].mon.moves[0].priority = 1;
    assert(battle_init(&b, &r, 73) == BI_OK);
    Action actions[] = {move(1, 0, 1), move(0, 0, 1), move(0, 1, 1), move(1, 1, 0)};
    Action invalid = move(0, 0, 2);
    assert(!battle_validate_action(&b, &invalid));
    invalid.target.pos = (Position){0, 1};
    assert(!battle_validate_action(&b, &invalid));
    battle_run_turn(&b, actions, 4, &log);
    Position expected[] = {{0, 0}, {1, 1}, {0, 1}, {1, 0}};
    unsigned used = 0;
    for (unsigned i = 0; i < log.count; ++i)
        if (log.events[i].kind == EV_MOVE_USED) {
            assert(used < 4);
            assert(log.events[i].user.side == expected[used].side && log.events[i].user.slot == expected[used].slot);
            ++used;
        }
    assert(used == 4);
    for (unsigned i = 0; i < 4; ++i) assert(battle_at(&b, actions[i].user)->pp[0] == 19);

    r.player[0].mon.moves[0].target = BTARGET_ALL_FOES;
    r.opponent[1].record.hp = 1;
    assert(battle_init(&b, &r, 73) == BI_OK);
    battle_run_turn(&b, actions, 4, &log);
    assert(event_count(&log, EV_MOVE_USED) == 3 && event_count(&log, EV_FAINT) == 1);
    assert(log.events[1].kind == EV_DAMAGE && log.events[1].target.slot == 0);
    assert(log.events[2].kind == EV_DAMAGE && log.events[2].target.slot == 1);
    assert(battle_at(&b, (Position){1, 1})->pp[0] == 20);
    assert(battle_at(&b, (Position){0, 0})->pp[0] == 19 && battle_at(&b, (Position){0, 1})->pp[0] == 19);

    // A side wins/loses only when its entire field is down.
    r.opponent[0].record.hp = 1;
    assert(battle_init(&b, &r, 73) == BI_OK);
    battle_run_turn(&b, actions, 4, &log);
    assert(b.outcome == OUTCOME_WIN && event_count(&log, EV_FAINT) == 2);
    r.player[0].record.hp = r.player[1].record.hp = 1;
    r.opponent[1].mon.moves[0].priority = 2;
    r.opponent[1].mon.moves[0].target = BTARGET_ALL_FOES;
    assert(battle_init(&b, &r, 73) == BI_OK);
    battle_run_turn(&b, actions, 4, &log);
    assert(b.outcome == OUTCOME_LOSE && event_count(&log, EV_MOVE_USED) == 1);

    make_multi(&r, BATTLE_FORMAT_DOUBLE);
    r.player[0].mon.moves[0].target = BTARGET_ALL_ADJACENT;
    assert(battle_init(&b, &r, 73) == BI_OK);
    assert(battle_target_mask(&b, (Position){0, 0}, &r.player[0].mon.moves[0], 0) == 2);
    r.player[0].mon.moves[0].target = BTARGET_SELF;
    assert(battle_target_mask(&b, (Position){0, 0}, &r.player[0].mon.moves[0], 0) == 1);

    make_multi(&r, BATTLE_FORMAT_DOUBLE);
    r.player[0].record.speed = r.player[1].record.speed;
    bool first[2] = {false, false};
    const Action ties[] = {move(0, 0, 0), move(0, 1, 0)};
    for (unsigned seed = 1; seed < 32; ++seed) {
        assert(battle_init(&b, &r, seed * 0x9e3779b9u) == BI_OK);
        battle_run_turn(&b, ties, 2, &log);
        first[log.events[0].user.slot] = true;
    }
    assert(first[0] && first[1]);
}

static void test_triple(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    make_multi(&r, BATTLE_FORMAT_TRIPLE);
    r.player[0].mon.moves[0].target |= BTARGET_IGNORE_DISTANCE;
    assert(battle_init(&b, &r, 17) == BI_OK);
    const bool neighbors[3][3] = {{true, true, false}, {true, true, true}, {false, true, true}};
    for (uint8_t a = 0; a < 3; ++a)
        for (uint8_t d = 0; d < 3; ++d) {
            assert(battle_at(&b, (Position){0, a}) && battle_at(&b, (Position){1, d}));
            assert(battle_adjacent(&b, (Position){0, a}, (Position){1, d}) == neighbors[a][d]);
            assert(battle_adjacent(&b, (Position){0, a}, (Position){0, d}) == (a != d && neighbors[a][d]));
            const Action near = move(1, a, d);
            assert(battle_validate_action(&b, &near) == neighbors[a][d]);
        }
    const Action distant = move(0, BPOS_LEFT, BPOS_RIGHT);
    assert(battle_validate_action(&b, &distant));

    const Action six[] = {distant, move(0, 1, 1), move(0, 2, 1), move(1, 0, 1), move(1, 1, 1), move(1, 2, 2)};
    battle_run_turn(&b, six, 6, &log);
    assert(event_count(&log, EV_MOVE_USED) == 6);

    for (uint8_t edge = BPOS_LEFT; edge <= BPOS_RIGHT; edge += 2) {
        assert(battle_init(&b, &r, 17) == BI_OK);
        const Action actions[] = {{.kind = ACT_SHIFT, .user = {0, edge}},
                                  move(0, BPOS_CENTER, (uint8_t)(2 - edge))};
        const uint8_t slot = battle_at(&b, (Position){0, edge})->party_slot;
        assert(battle_validate_action(&b, &actions[0]));
        battle_run_turn(&b, actions, 2, &log);
        assert(log.events[0].kind == EV_SHIFT && log.events[0].target.slot == BPOS_CENTER);
        assert(battle_at(&b, (Position){0, BPOS_CENTER})->party_slot == slot);
        assert(log.events[1].kind == EV_MOVE_USED && log.events[1].user.slot == edge);
        assert(log.events[1].target.slot != 2 - edge); // Rechecked distance after the swap.
    }
}

static void test_rotation(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    make_multi(&r, BATTLE_FORMAT_ROTATION);
    r.player[0].record.speed = 140;
    r.player[1].record.speed = 10;
    r.player[0].record.status = 16;
    assert(battle_init(&b, &r, 88) == BI_OK);
    b.sides[0].battlers[0].stat_stages[2] = -2;
    b.sides[0].battlers[0].hp = 171;
    Position actors[3];
    assert(battle_collect_actors(&b, 0, actors) == 1 && actors[0].slot == BPOS_CENTER);
    Action actions[] = {move(0, BPOS_CENTER, BPOS_CENTER), move(1, BPOS_CENTER, BPOS_CENTER)};
    Action wing = move(0, BPOS_LEFT, BPOS_CENTER);
    assert(!battle_validate_action(&b, &wing));
    battle_run_turn(&b, actions, 2, &log);
    assert(log.events[0].kind == EV_MOVE_USED && log.events[0].user.side == 1);

    actions[0].rotation = BROT_LEFT;
    assert(battle_validate_action(&b, &actions[0]));
    const uint16_t old_hp = b.sides[0].battlers[1].hp;
    battle_run_turn(&b, actions, 2, &log);
    assert(log.events[0].kind == EV_ROTATE && log.events[1].kind == EV_MOVE_USED && log.events[1].user.side == 0);
    assert(battle_active(&b, 0) == &b.sides[0].battlers[0] && battle_active(&b, 0)->status == 16);
    assert(battle_active(&b, 0)->stat_stages[2] == -2 && b.sides[0].battlers[1].hp == old_hp);
    actions[0].rotation = BROT_RIGHT;
    battle_run_turn(&b, actions, 2, &log);
    assert(battle_active(&b, 0) == &b.sides[0].battlers[1]);
    b.sides[0].battlers[0].hp = 0;
    b.sides[0].battlers[0].fainted = true;
    actions[0].rotation = BROT_LEFT;
    assert(!battle_validate_action(&b, &actions[0]));
    assert(!battle_command_targets(&b, (Position){0, BPOS_LEFT}, 0));
}

static void test_multi_wire(void) {
    uint8_t bytes[WIRE_PAYLOAD];
    BattleMultiState state = {.phase = BPHASE_CHOOSE, .turn = 6, .actor = 2, .flags = BATTLE_MULTI_SHIFT, .token = 513};
    BattleMultiState decoded;
    state.field[1][2] = (BattleFieldMon){0x85, 45, 90};
    state.pp[2][1] = 7;
    state.targets[2][1] = 6;
    assert(battle_multi_state_encode(&state, bytes) == BATTLE_MULTI_STATE_BYTES);
    assert(battle_multi_state_decode(bytes, BATTLE_MULTI_STATE_BYTES, &decoded));
    assert(decoded.token == 513 && decoded.field[1][2].hp == 45 && decoded.targets[2][1] == 6);
    assert(!battle_multi_state_decode(bytes, BATTLE_MULTI_STATE_BYTES - 1, &decoded));
    BattleMultiAction action = {513, 2, BACTION_MOVE, 1, 2, BROT_RIGHT, 0}, reply;
    assert(battle_multi_action_encode(&action, bytes) == BATTLE_MULTI_ACTION_BYTES);
    assert(battle_multi_action_decode(bytes, BATTLE_MULTI_ACTION_BYTES, &reply));
    assert(reply.token == 513 && reply.actor == 2 && reply.rotation == BROT_RIGHT);
    bytes[5] = 3;
    assert(!battle_multi_action_decode(bytes, BATTLE_MULTI_ACTION_BYTES, &reply));
}

static void add_reserve(BattleRequest *r, unsigned side) {
    BattleEntry *entries = side ? r->opponent : r->player;
    uint8_t *count = side ? &r->opponent_count : &r->player_count;
    entries[*count] = entries[0];
    entries[*count].record.slot = entries[0].record.slot + *count;
    ++*count;
}

static void set_fainted(Battle *b, unsigned side, unsigned index) {
    b->sides[side].battlers[index].hp = 0;
    b->sides[side].battlers[index].fainted = true;
}

static void test_roster_wire(void) {
    BattleRequest r, decoded;
    make_multi(&r, BATTLE_FORMAT_SINGLE);
    add_reserve(&r, 0);
    r.version = BATTLE_REQUEST_VERSION_ROSTER;
    uint8_t buffer[2048];
    unsigned n = battle_request_encode(&r, buffer, sizeof(buffer));
    assert(n && buffer[0] == BATTLE_REQUEST_VERSION_ROSTER);
    assert(battle_request_decode(buffer, n, &decoded) && decoded.player_count == 2);
    assert(decoded.version == BATTLE_REQUEST_VERSION_ROSTER);
    buffer[0] = BATTLE_REQUEST_VERSION_CONDITIONS + 1;
    assert(!battle_request_decode(buffer, n, &decoded));

    const BattleRosterState state = {
        .field_state = {.phase = BPHASE_REPLACE, .turn = 1, .actor = 0, .token = 17},
        .reserves = 1u << 5, .player = {[5] = {0x85, 10, 30}},
    };
    BattleRosterState roster;
    BattleMultiState legacy;
    n = battle_roster_state_encode(&state, buffer);
    assert(n == BATTLE_ROSTER_STATE_BYTES && n + APP_REQUEST_HEADER <= WIRE_PAYLOAD);
    assert(battle_roster_state_decode(buffer, n, &roster) && roster.reserves == (1u << 5));
    assert(roster.player[5].party_slot == 0x85 && roster.player[5].hp == 10);
    assert(!battle_multi_state_decode(buffer, BATTLE_MULTI_STATE_BYTES, &legacy));
    assert(!battle_roster_state_decode(buffer, n - 1, &roster));
    const BattleMultiAction action = {.token = 17, .action = BACTION_REPLACE, .target = 5};
    BattleMultiAction reply;
    n = battle_multi_action_encode(&action, buffer);
    assert(battle_roster_action_decode(buffer, n, &reply) && reply.target == 5);
    assert(!battle_multi_action_decode(buffer, n, &reply));
    buffer[5] = BATTLE_MAX_PER_SIDE;
    assert(!battle_roster_action_decode(buffer, n, &reply));
}

static void test_single_replacement(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    Position vacant;
    make_multi(&r, BATTLE_FORMAT_SINGLE);
    add_reserve(&r, 1);
    assert(battle_init(&b, &r, 8) == BI_PARTY_SIZE);
    r.version = BATTLE_REQUEST_VERSION_ROSTER;
    r.opponent[0].record.hp = 1;
    assert(battle_init(&b, &r, 8) == BI_OK);
    const Action action = move(0, 0, 0);
    battle_run_turn(&b, &action, 1, &log);
    assert(b.outcome == OUTCOME_NONE && b.turn == 1);
    assert(battle_pending_replacement(&b, &vacant) && vacant.side == 1 && vacant.slot == 0);
    assert(!battle_at(&b, vacant));
    assert(!battle_replace(&b, vacant, 0, &log));
    battle_run_turn(&b, &action, 1, &log);
    assert(b.turn == 1 && log.count == 0);
    assert(battle_replace(&b, vacant, 1, &log));
    assert(log.count == 1 && log.events[0].kind == EV_SEND_OUT && log.events[0].battler == 1);
    assert(battle_at(&b, vacant)->party_slot == r.opponent[1].record.slot);
    assert(!battle_pending_replacement(&b, NULL));
}

static void test_double_replacements(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    Position vacant;
    make_multi(&r, BATTLE_FORMAT_DOUBLE);
    r.version = BATTLE_REQUEST_VERSION_ROSTER;
    add_reserve(&r, 0);
    add_reserve(&r, 1);
    r.player[0].mon.moves[0].target = BTARGET_ALL_ADJACENT;
    r.player[1].record.hp = r.opponent[0].record.hp = r.opponent[1].record.hp = 1;
    assert(battle_init(&b, &r, 81) == BI_OK);
    const Action attack = move(0, 0, 0);
    battle_run_turn(&b, &attack, 1, &log);
    assert(event_count(&log, EV_FAINT) == 3 && b.outcome == OUTCOME_NONE);
    assert(battle_pending_replacement(&b, &vacant) && vacant.side == 0 && vacant.slot == 1);
    assert(battle_replacement_mask(&b, vacant) == 4);
    assert(battle_replace(&b, vacant, 2, &log));
    assert(!battle_replace(&b, (Position){0, 0}, 2, &log));
    assert(battle_pending_replacement(&b, &vacant) && vacant.side == 1 && vacant.slot == 0);
    assert(battle_replace(&b, vacant, 2, &log));
    assert(!battle_pending_replacement(&b, NULL));
    assert(!battle_at(&b, (Position){1, 1}));
    assert(!battle_replace(&b, (Position){1, 1}, 2, &log));
}

static void test_triple_center(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    make_multi(&r, BATTLE_FORMAT_TRIPLE);
    r.version = BATTLE_REQUEST_VERSION_ROSTER;
    assert(battle_init(&b, &r, 12) == BI_OK);
    set_fainted(&b, 0, BPOS_CENTER);
    set_fainted(&b, 0, BPOS_RIGHT);
    set_fainted(&b, 1, BPOS_LEFT);
    set_fainted(&b, 1, BPOS_CENTER);
    battle_run_turn(&b, NULL, 0, &log);
    assert(event_count(&log, EV_SHIFT) == 2);
    assert(battle_at(&b, (Position){0, BPOS_CENTER})->party_slot == r.player[0].record.slot);
    assert(battle_at(&b, (Position){1, BPOS_CENTER})->party_slot == r.opponent[2].record.slot);
    assert(!battle_at(&b, (Position){0, BPOS_LEFT}));
}

static void test_rotation_replacement(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    Position vacant;
    make_multi(&r, BATTLE_FORMAT_ROTATION);
    r.version = BATTLE_REQUEST_VERSION_ROSTER;
    add_reserve(&r, 1);
    r.opponent[0].record.hp = 1;
    assert(battle_init(&b, &r, 13) == BI_OK);
    assert(battle_active(&b, 1)->party_slot == r.opponent[0].record.slot);
    const Action attack = move(0, BPOS_CENTER, BPOS_CENTER);
    battle_run_turn(&b, &attack, 1, &log);
    assert(battle_pending_replacement(&b, &vacant) && vacant.side == 1 && vacant.slot == BPOS_CENTER);
    assert(!battle_replace(&b, vacant, 1, &log)); // A wing is already on the field.
    assert(battle_replace(&b, vacant, 3, &log));
    assert(battle_at(&b, (Position){1, BPOS_LEFT})->party_slot == r.opponent[1].record.slot);
    set_fainted(&b, 1, 3);
    battle_run_turn(&b, NULL, 0, &log);
    assert(!battle_pending_replacement(&b, NULL) && !battle_active(&b, 1));
    Action rotate = move(1, BPOS_CENTER, BPOS_CENTER);
    rotate.rotation = BROT_LEFT;
    assert(battle_validate_action(&b, &rotate));
    b.sides[1].battlers[1].stat_stages[0] = 2;
    battle_run_turn(&b, &rotate, 1, &log);
    assert(log.events[0].kind == EV_ROTATE && !event_count(&log, EV_SEND_OUT));
    assert(battle_active(&b, 1)->stat_stages[0] == 2);
}

static void test_voluntary_switch(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    make_multi(&r, BATTLE_FORMAT_DOUBLE);
    add_reserve(&r, 0);
    add_reserve(&r, 0);
    r.player[0].record.status = 16;
    r.opponent[0].mon.moves[0].priority = 3;
    Action actions[] = {
        {.kind = ACT_SWITCH, .user = {0, 0}, .reserve = 2},
        {.kind = ACT_SWITCH, .user = {0, 1}, .reserve = 2},
        move(1, 0, 0),
    };
    r.version = BATTLE_REQUEST_VERSION_ROSTER;
    assert(battle_init(&b, &r, 91) == BI_OK && !battle_validate_action(&b, &actions[0]));
    r.version = BATTLE_REQUEST_VERSION_SWITCH;
    assert(battle_init(&b, &r, 91) == BI_OK);
    assert(battle_switch_mask(&b, actions[0].user) == 12);
    assert(battle_validate_action(&b, &actions[0]));
    b.sides[0].battlers[0].stat_stages[2] = 4;
    battle_run_turn(&b, actions, 3, &log);
    assert(log.events[0].kind == EV_WITHDRAW && log.events[1].kind == EV_SEND_OUT);
    assert(log.events[2].kind == EV_MOVE_USED && log.events[2].user.side == 1);
    assert(event_count(&log, EV_WITHDRAW) == 1); // The second command repeated the first reserve.
    assert(b.sides[0].active[0] == 2 && b.sides[0].active[1] == 1);
    const Battler *old = &b.sides[0].battlers[0], *entrant = &b.sides[0].battlers[2];
    assert(old->hp == 200 && old->pp[0] == 20 && old->status == 16 && old->stat_stages[2] == 0);
    assert(entrant->hp < 200 && entrant->pp[0] == 20);

    actions[1].reserve = 3;
    assert(battle_init(&b, &r, 91) == BI_OK);
    battle_run_turn(&b, actions, 3, &log);
    assert(event_count(&log, EV_WITHDRAW) == 2 && b.sides[0].active[0] == 2 && b.sides[0].active[1] == 3);
    const uint16_t hp = b.sides[0].battlers[2].hp;
    const uint32_t rng = b.rng;
    assert(battle_init(&b, &r, 91) == BI_OK);
    battle_run_turn(&b, actions, 3, &log);
    assert(b.sides[0].battlers[2].hp == hp && b.rng == rng); // Replay with the same seed.

    make_multi(&r, BATTLE_FORMAT_SINGLE);
    add_reserve(&r, 0);
    r.version = BATTLE_REQUEST_VERSION_SWITCH;
    assert(battle_init(&b, &r, 91) == BI_OK);
    const Action single = {.kind = ACT_SWITCH, .user = {0, 0}, .reserve = 1};
    assert(battle_validate_action(&b, &single));
    battle_run_turn(&b, &single, 1, &log);
    assert(battle_active(&b, 0)->party_slot == r.player[1].record.slot);

    make_multi(&r, BATTLE_FORMAT_TRIPLE);
    add_reserve(&r, 0);
    r.version = BATTLE_REQUEST_VERSION_SWITCH;
    assert(battle_init(&b, &r, 91) == BI_OK);
    const Action triple[] = {
        {.kind = ACT_SHIFT, .user = {0, BPOS_LEFT}},
        {.kind = ACT_SWITCH, .user = {0, BPOS_CENTER}, .reserve = 3},
    };
    b.sides[0].battlers[0].stat_stages[0] = 1;
    battle_run_turn(&b, triple, 2, &log);
    assert(log.events[0].kind == EV_WITHDRAW && log.events[1].kind == EV_SEND_OUT && log.events[2].kind == EV_SHIFT);
    assert(b.sides[0].active[BPOS_LEFT] == 3 && b.sides[0].active[BPOS_CENTER] == 0);
    assert(b.sides[0].battlers[0].stat_stages[0] == 1);

    make_multi(&r, BATTLE_FORMAT_ROTATION);
    add_reserve(&r, 0);
    r.version = BATTLE_REQUEST_VERSION_SWITCH;
    assert(battle_init(&b, &r, 91) == BI_OK);
    Action rot_switch = {.kind = ACT_SWITCH, .user = {0, BPOS_CENTER}, .reserve = 3};
    assert(battle_validate_action(&b, &rot_switch));
    rot_switch.rotation = BROT_LEFT;
    assert(!battle_validate_action(&b, &rot_switch));
    rot_switch.rotation = BROT_KEEP;
    rot_switch.user.slot = BPOS_LEFT;
    assert(!battle_validate_action(&b, &rot_switch));
    rot_switch.user.slot = BPOS_CENTER;
    battle_run_turn(&b, &rot_switch, 1, &log);
    assert(b.sides[0].active[BPOS_CENTER] == 3 && b.sides[0].active[BPOS_LEFT] == 1);
    assert(log.events[0].kind == EV_WITHDRAW && log.events[1].kind == EV_SEND_OUT);
    assert(!event_count(&log, EV_ROTATE));
}

static void test_switch_wire(void) {
    const BattleMultiAction action = {.token = 90, .action = BACTION_SWITCH, .target = 5};
    uint8_t bytes[BATTLE_MULTI_ACTION_BYTES];
    BattleMultiAction decoded;
    battle_multi_action_encode(&action, bytes);
    assert(battle_switch_action_decode(bytes, sizeof(bytes), &decoded) && decoded.target == 5);
    assert(!battle_roster_action_decode(bytes, sizeof(bytes), &decoded));
    assert(!battle_multi_action_decode(bytes, sizeof(bytes), &decoded));
    bytes[6] = BROT_LEFT;
    assert(!battle_switch_action_decode(bytes, sizeof(bytes), &decoded));
}

static void enable_struggle(BattleRequest *r) {
    r->version = BATTLE_REQUEST_VERSION_STRUGGLE;
    r->struggle_power = 50;
    r->struggle_accuracy = 0;
    r->struggle_recoil = BSTRUGGLE_MAX_HP_NEAREST;
}

static void test_struggle_wire(void) {
    BattleRequest r, decoded;
    make_multi(&r, BATTLE_FORMAT_SINGLE);
    enable_struggle(&r);
    uint8_t bytes[2048];
    const unsigned length = battle_request_encode(&r, bytes, sizeof(bytes));
    assert(length && bytes[0] == 4 && bytes[21] == 50 && bytes[22] == 0 && bytes[23] == BSTRUGGLE_MAX_HP_NEAREST);
    assert(battle_request_decode(bytes, length, &decoded) && decoded.struggle_power == 50);
    bytes[23] = 255;
    assert(!battle_request_decode(bytes, length, &decoded));

    const BattleRosterState state = {
        .field_state = {.phase = BPHASE_CHOOSE, .token = 1}, .struggle_mask = 1,
    };
    BattleRosterState reply;
    const unsigned n = battle_struggle_state_encode(&state, bytes);
    assert(n == BATTLE_STRUGGLE_STATE_BYTES);
    assert(battle_struggle_state_decode(bytes, n, &reply) && reply.struggle_mask == 1);
    assert(!battle_roster_state_decode(bytes, n, &reply));
    bytes[n - 1] = 8;
    assert(!battle_struggle_state_decode(bytes, n, &reply));

    const BattleMultiAction action = {.token = 1, .action = BACTION_STRUGGLE, .rotation = BROT_RIGHT};
    BattleMultiAction ack;
    const unsigned size = battle_multi_action_encode(&action, bytes);
    assert(battle_struggle_action_decode(bytes, size, &ack) && ack.rotation == BROT_RIGHT);
    assert(!battle_switch_action_decode(bytes, size, &ack));
    bytes[5] = 1;
    assert(!battle_struggle_action_decode(bytes, size, &ack));
}

static void test_struggle(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    const Action struggle = {.kind = ACT_STRUGGLE, .user = {0, 0}};
    make_multi(&r, BATTLE_FORMAT_SINGLE);
    enable_struggle(&r);
    r.player[0].record.hp = r.player[0].record.max_hp = 202;
    r.opponent[0].mon.types[0] = T_GHOST;
    r.player[0].mon.moves[0].unsupported = UNSUP_SECONDARY;
    assert(battle_init(&b, &r, 77) == BI_OK);
    assert(!battle_validate_action(&b, &struggle)); // Unsupported with PP is not PP exhaustion.
    r.player[0].record.pp[0] = 0;
    assert(battle_init(&b, &r, 77) == BI_OK && battle_validate_action(&b, &struggle));
    Action ai;
    battle_choose_ai(&b, 0, &ai);
    assert(ai.kind == ACT_STRUGGLE && battle_validate_action(&b, &ai));
    battle_run_turn(&b, &struggle, 1, &log);
    assert(log.events[0].move_id == BATTLE_MOVE_STRUGGLE && log.events[1].kind == EV_DAMAGE);
    assert(log.events[1].effectiveness == 4);
    assert(log.events[2].kind == EV_RECOIL && log.events[2].amount == 51);
    assert(battle_active(&b, 0)->hp == 151 && battle_active(&b, 0)->pp[0] == 0);

    r.struggle_recoil = BSTRUGGLE_MAX_HP_FLOOR;
    assert(battle_init(&b, &r, 77) == BI_OK);
    battle_run_turn(&b, &struggle, 1, &log);
    assert(log.events[2].amount == 50);

    r.player[0].record.hp = r.opponent[0].record.hp = 1;
    assert(battle_init(&b, &r, 77) == BI_OK);
    battle_run_turn(&b, &struggle, 1, &log);
    assert(b.outcome == OUTCOME_DRAW && event_count(&log, EV_FAINT) == 2);
    assert(!battle_at(&b, (Position){0, 0}) && !battle_at(&b, (Position){1, 0}));

    make_multi(&r, BATTLE_FORMAT_DOUBLE);
    enable_struggle(&r);
    r.player[0].record.pp[0] = 0;
    assert(battle_init(&b, &r, 77) == BI_OK);
    battle_run_turn(&b, &struggle, 1, &log);
    assert(event_count(&log, EV_DAMAGE) == 1 && log.events[1].target.side == 1);

    make_multi(&r, BATTLE_FORMAT_TRIPLE);
    enable_struggle(&r);
    r.player[0].record.pp[0] = 0;
    assert(battle_init(&b, &r, 77) == BI_OK);
    battle_run_turn(&b, &struggle, 1, &log);
    assert(event_count(&log, EV_DAMAGE) == 1 && log.events[1].target.slot != BPOS_RIGHT);

    make_multi(&r, BATTLE_FORMAT_ROTATION);
    enable_struggle(&r);
    r.player[1].record.pp[0] = 0;
    assert(battle_init(&b, &r, 77) == BI_OK);
    const Action rotate = {.kind = ACT_STRUGGLE, .user = {0, BPOS_CENTER}, .rotation = BROT_LEFT};
    assert(battle_validate_action(&b, &rotate));
    battle_run_turn(&b, &rotate, 1, &log);
    assert(log.events[0].kind == EV_ROTATE && log.events[1].move_id == BATTLE_MOVE_STRUGGLE);
    assert(b.sides[0].battlers[0].hp == 200 && b.sides[0].battlers[1].hp < 200);
}

static void test_drag(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    make_multi(&r, BATTLE_FORMAT_DOUBLE);
    enable_struggle(&r);
    r.version = BATTLE_REQUEST_VERSION_DRAG;
    add_reserve(&r, 1);
    const BattleMove roar = {.target = BTARGET_SINGLE, .priority = -6, .effect = BME_FORCE_OUT};
    r.player[0].mon.moves[0] = roar;
    r.opponent[0].mon.moves[0].priority = -7;
    assert(battle_init(&b, &r, 17) == BI_OK);
    const Action turn[] = {move(0, 0, 0), move(1, 0, 0)};
    b.sides[1].battlers[0].stat_stages[0] = 3;
    battle_run_turn(&b, turn, 2, &log);
    assert(event_count(&log, EV_MOVE_USED) == 1 && event_count(&log, EV_DRAG_OUT) == 1);
    assert(log.events[1].kind == EV_DRAG_OUT && log.events[2].kind == EV_SEND_OUT);
    assert(b.sides[1].active[0] == 2 && b.sides[1].battlers[0].pp[0] == 20);
    assert(b.sides[1].battlers[0].stat_stages[0] == 0 && b.sides[0].battlers[0].pp[0] == 19);

    r.player[0].mon.moves[0] = hit(40, T_NORMAL, BCAT_PHYSICAL);
    r.player[0].mon.moves[0].effect = BME_FORCE_OUT_ON_HIT;
    r.opponent[0].mon.types[0] = T_GHOST;
    assert(battle_init(&b, &r, 17) == BI_OK);
    battle_run_turn(&b, turn, 1, &log);
    assert(!event_count(&log, EV_DRAG_OUT) && event_count(&log, EV_NO_EFFECT) == 1);
    r.opponent[0].mon.types[0] = T_NORMAL;
    assert(battle_init(&b, &r, 17) == BI_OK);
    battle_run_turn(&b, turn, 1, &log);
    assert(event_count(&log, EV_DAMAGE) == 1 && event_count(&log, EV_DRAG_OUT) == 1);

    r.opponent[0].record.hp = 1;
    assert(battle_init(&b, &r, 17) == BI_OK);
    battle_run_turn(&b, turn, 1, &log);
    assert(!event_count(&log, EV_DRAG_OUT) && battle_pending_replacement(&b, NULL));

    r.opponent_count = 2;
    r.player[0].mon.moves[0] = roar;
    assert(battle_init(&b, &r, 17) == BI_OK);
    battle_run_turn(&b, turn, 1, &log);
    assert(event_count(&log, EV_NO_EFFECT) == 1 && b.sides[0].battlers[0].pp[0] == 19);

    make_multi(&r, BATTLE_FORMAT_SINGLE);
    enable_struggle(&r);
    r.version = BATTLE_REQUEST_VERSION_DRAG;
    add_reserve(&r, 1);
    r.player[0].mon.moves[0] = roar;
    assert(battle_init(&b, &r, 17) == BI_OK);
    battle_run_turn(&b, turn, 1, &log);
    assert(battle_active(&b, 1)->party_slot == r.opponent[1].record.slot);
    assert(event_count(&log, EV_DRAG_OUT) == 1);

    make_multi(&r, BATTLE_FORMAT_TRIPLE);
    enable_struggle(&r);
    r.version = BATTLE_REQUEST_VERSION_DRAG;
    add_reserve(&r, 1);
    r.player[0].mon.moves[0] = roar;
    assert(battle_init(&b, &r, 17) == BI_OK);
    const Action too_far = move(0, BPOS_LEFT, BPOS_RIGHT);
    assert(!battle_validate_action(&b, &too_far));
    const Action near = move(0, BPOS_LEFT, BPOS_CENTER);
    battle_run_turn(&b, &near, 1, &log);
    assert(b.sides[1].active[BPOS_CENTER] == 3 && event_count(&log, EV_DRAG_OUT) == 1);

    make_multi(&r, BATTLE_FORMAT_ROTATION);
    enable_struggle(&r);
    r.version = BATTLE_REQUEST_VERSION_DRAG;
    add_reserve(&r, 1);
    r.player[1].mon.moves[0] = roar;
    assert(battle_init(&b, &r, 17) == BI_OK);
    Action rotations[] = {
        move(0, BPOS_CENTER, BPOS_CENTER),
        {.kind = ACT_SWITCH, .user = {1, BPOS_CENTER}, .reserve = 3},
    };
    rotations[0].rotation = BROT_LEFT;
    battle_run_turn(&b, rotations, 2, &log);
    assert(log.events[0].kind == EV_WITHDRAW && log.events[1].kind == EV_SEND_OUT && log.events[2].kind == EV_ROTATE);
    assert(log.events[3].kind == EV_MOVE_USED && log.events[4].kind == EV_DRAG_OUT);
    assert(b.sides[1].active[BPOS_CENTER] == 0 && b.sides[1].active[BPOS_LEFT] == 1);

    BattleRequest decoded;
    uint8_t buffer[2048];
    const unsigned size = battle_request_encode(&r, buffer, sizeof(buffer));
    assert(size && battle_request_decode(buffer, size, &decoded));
    assert(decoded.player[1].mon.moves[0].effect == BME_FORCE_OUT);
    const unsigned offset = BATTLE_REQUEST_HEADER + BATTLE_ENTRY_BYTES + PKMN_RECORD_BYTES + 4;
    buffer[offset] = 0xff;
    assert(!battle_request_decode(buffer, size, &decoded));
    buffer[0] = BATTLE_REQUEST_VERSION_STRUGGLE;
    assert(battle_request_decode(buffer, size, &decoded) && decoded.player[1].mon.moves[0].effect == BME_HIT);
}

static void test_pivot(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    Position pos;
    make_multi(&r, BATTLE_FORMAT_DOUBLE);
    enable_struggle(&r);
    r.version = BATTLE_REQUEST_VERSION_PIVOT;
    add_reserve(&r, 0);
    r.player[0].record.speed = 110;
    r.player[0].mon.moves[0].effect = BME_PIVOT;
    const Action actions[] = {move(0, 0, 0), move(1, 0, 0), move(0, 1, 1), move(1, 1, 0)};
    assert(battle_init(&b, &r, 301) == BI_OK);
    battle_run_turn(&b, actions, 4, &log);
    assert(battle_pending_pivot(&b, &pos) && pos.side == 0 && pos.slot == 0 && b.turn == 0);
    assert(log.count == 2 && log.events[0].kind == EV_MOVE_USED);
    assert(b.sides[0].battlers[0].pp[0] == 19 && b.sides[1].battlers[0].pp[0] == 20);
    const uint16_t hit_hp = b.sides[1].battlers[0].hp;
    battle_run_turn(&b, actions, 4, &log);
    assert(!log.count && b.sides[1].battlers[0].hp == hit_hp);
    assert(!battle_resume_pivot(&b, 1, &log));
    assert(battle_resume_pivot(&b, 2, &log) && b.turn == 1);
    assert(log.events[0].kind == EV_WITHDRAW && log.events[1].kind == EV_SEND_OUT);
    assert(!battle_pending_pivot(&b, NULL) && !battle_resume_pivot(&b, 2, &log));
    assert(event_count(&log, EV_MOVE_USED) == 3);
    assert(b.sides[1].battlers[0].hp == hit_hp);
    assert(b.sides[0].battlers[0].hp == 200 && b.sides[0].battlers[2].hp < 200);
    assert(b.sides[0].battlers[0].pp[0] == 19 && b.sides[0].battlers[2].pp[0] == 20);

    assert(battle_init(&b, &r, 301) == BI_OK);
    battle_run_turn(&b, actions, 4, &log);
    const Action stop = {.kind = ACT_FORFEIT, .user = {0, 0}};
    battle_run_turn(&b, &stop, 1, &log);
    assert(b.outcome == OUTCOME_CANCELLED && !battle_pending_pivot(&b, NULL));
    assert(!battle_resume_pivot(&b, 2, &log));

    r.opponent[0].mon.types[0] = T_GHOST;
    assert(battle_init(&b, &r, 301) == BI_OK);
    battle_run_turn(&b, actions, 1, &log);
    assert(!battle_pending_pivot(&b, NULL) && event_count(&log, EV_NO_EFFECT) == 1);

    r.opponent[0].mon.types[0] = T_NORMAL;
    r.player_count = 2;
    assert(battle_init(&b, &r, 301) == BI_OK);
    battle_run_turn(&b, actions, 1, &log);
    assert(!battle_pending_pivot(&b, NULL) && event_count(&log, EV_DAMAGE) == 1);

    make_multi(&r, BATTLE_FORMAT_SINGLE);
    enable_struggle(&r);
    r.version = BATTLE_REQUEST_VERSION_PIVOT;
    add_reserve(&r, 0);
    add_reserve(&r, 1);
    r.opponent[0].mon.moves[0].effect = BME_PIVOT;
    const Action ai = move(1, 0, 0);
    assert(battle_init(&b, &r, 301) == BI_OK);
    battle_run_turn(&b, &ai, 1, &log);
    assert(!battle_pending_pivot(&b, NULL) && b.turn == 1 && b.sides[1].active[0] == 1);
    assert(event_count(&log, EV_WITHDRAW) == 1);
    r.player[0].mon.moves[0].effect = BME_PIVOT;
    r.opponent[0].record.hp = r.opponent[1].record.hp = 1;
    r.opponent_count = 1;
    assert(battle_init(&b, &r, 301) == BI_OK);
    battle_run_turn(&b, actions, 1, &log);
    assert(b.outcome == OUTCOME_WIN && !battle_pending_pivot(&b, NULL));

    make_multi(&r, BATTLE_FORMAT_ROTATION);
    enable_struggle(&r);
    r.version = BATTLE_REQUEST_VERSION_PIVOT;
    add_reserve(&r, 0);
    r.player[1].mon.moves[0].effect = BME_PIVOT;
    assert(battle_init(&b, &r, 301) == BI_OK);
    Action rotate = move(0, BPOS_CENTER, BPOS_CENTER);
    rotate.rotation = BROT_LEFT;
    battle_run_turn(&b, &rotate, 1, &log);
    assert(log.events[0].kind == EV_ROTATE);
    assert(battle_pending_pivot(&b, &pos) && pos.slot == BPOS_CENTER);
    assert(battle_resume_pivot(&b, 3, &log) && b.sides[0].active[BPOS_CENTER] == 3);
    assert(b.sides[0].active[BPOS_RIGHT] == 0 && b.sides[0].active[BPOS_LEFT] == 2);

    make_multi(&r, BATTLE_FORMAT_TRIPLE);
    enable_struggle(&r);
    r.version = BATTLE_REQUEST_VERSION_PIVOT;
    const Action triple[] = {{.kind = ACT_SHIFT, .user = {0, BPOS_LEFT}}, move(1, BPOS_CENTER, BPOS_LEFT)};
    assert(battle_init(&b, &r, 301) == BI_OK);
    battle_run_turn(&b, triple, 2, &log);
    assert(log.events[0].kind == EV_MOVE_USED && log.events[2].kind == EV_SHIFT);

    add_reserve(&r, 0);
    r.player[0].mon.moves[0].effect = BME_PIVOT;
    assert(battle_init(&b, &r, 301) == BI_OK);
    const Action far = move(0, BPOS_LEFT, BPOS_RIGHT), near = move(0, BPOS_LEFT, BPOS_CENTER);
    assert(!battle_validate_action(&b, &far) && battle_validate_action(&b, &near));
    battle_run_turn(&b, &near, 1, &log);
    assert(battle_pending_pivot(&b, &pos) && pos.slot == BPOS_LEFT);
    assert(battle_resume_pivot(&b, 3, &log) && b.sides[0].active[BPOS_LEFT] == 3);
}

static void test_pivot_wire(void) {
    BattleRosterState state = {
        .field_state = {.phase = BPHASE_PIVOT, .token = 2},
        .reserves = 4, .player = {[2] = {0x82, 10, 20}},
    }, decoded;
    uint8_t bytes[BATTLE_STRUGGLE_STATE_BYTES];
    unsigned n = battle_struggle_state_encode(&state, bytes);
    assert(battle_pivot_state_decode(bytes, n, &decoded));
    assert(!battle_struggle_state_decode(bytes, n, &decoded));
    const BattleMultiAction action = {.action = BACTION_PIVOT, .token = 2, .target = 2};
    BattleMultiAction ack;
    n = battle_multi_action_encode(&action, bytes);
    assert(battle_pivot_action_decode(bytes, n, &ack) && ack.token == 2 && ack.target == 2);
    assert(!battle_struggle_action_decode(bytes, n, &ack));
}

static void make_conditions(BattleRequest *r, uint8_t format) {
    make_multi(r, format);
    enable_struggle(r);
    r->version = BATTLE_REQUEST_VERSION_CONDITIONS;
    r->conditions = (BattleConditionRules){
        .flags = BCR_ELECTRIC_IMMUNE | BCR_POISON_TOXIC_ALWAYS_HITS | BCR_THAW_ON_FIRE_HIT | BCR_DYNAMIC_SPEED,
        .poison_type = T_GHOST, .steel_type = T_GHOST, .fire_type = T_FIRE, .electric_type = T_WATER,
        .ice_type = T_WATER, .paralysis_speed_divisor = 2, .paralysis_block_divisor = 4,
        .burn_residual_divisor = 16, .sleep_min = 2, .sleep_max = 4, .thaw_percent = 20,
    };
}

static BattleMove condition_move(uint8_t effect, uint8_t target) {
    return (BattleMove){.effect = effect, .target = target, .category = BCAT_STATUS};
}

static void test_condition_wire(void) {
    BattleRequest r, decoded;
    make_conditions(&r, BATTLE_FORMAT_DOUBLE);
    set_move(&r.player[0], 1, 31, condition_move(BATTLE_STAGE_EFFECT(BSTAT_EVA, 2), BTARGET_SELF), 12);
    r.opponent[0].record.status = BATTLE_CONDITION(BST_TOXIC, 7);
    uint8_t bytes[2048];
    unsigned n = battle_request_encode(&r, bytes, sizeof(bytes));
    assert(n == BATTLE_REQUEST_HEADER + 4 * BATTLE_ENTRY_BYTES + NTYPES * NTYPES + BATTLE_CONDITION_RULE_BYTES);
    assert(battle_request_decode(bytes, n, &decoded) && decoded.conditions.sleep_max == 4);
    assert(decoded.opponent[0].record.status == BATTLE_CONDITION(BST_TOXIC, 7));
    assert(decoded.player[0].mon.moves[1].effect == BATTLE_STAGE_EFFECT(BSTAT_EVA, 2));
    bytes[n - 1] = 101;
    assert(!battle_request_decode(bytes, n, &decoded));
    bytes[n - 1] = 20;
    bytes[BATTLE_REQUEST_HEADER + PKMN_RECORD_BYTES + 5] = 0xff;
    assert(!battle_request_decode(bytes, n, &decoded));
    Battle b;
    r.opponent[0].record.status = BATTLE_CONDITION(BST_TOXIC, 16);
    assert(battle_init(&b, &r, 1) == BI_CONDITIONS);

    const BattleRosterState state = {
        .field_state = {.phase = BPHASE_CHOOSE, .token = 18},
        .status = {[1][1] = BST_BURN},
        .stages = {[0][0][BSTAT_ACC] = -6, [1][1][BSTAT_DEF] = 6},
    };
    BattleRosterState reply;
    n = battle_condition_state_encode(&state, bytes);
    assert(n == BATTLE_CONDITION_STATE_BYTES && n + APP_REQUEST_HEADER <= WIRE_PAYLOAD);
    assert(battle_condition_state_decode(bytes, n, &reply));
    assert(reply.field_state.token == 18 && reply.status[1][1] == BST_BURN);
    assert(reply.stages[0][0][BSTAT_ACC] == -6 && reply.stages[1][1][BSTAT_DEF] == 6);
    assert(!battle_condition_state_decode(bytes, n - 1, &reply));
    bytes[n - 1] = 7;
    assert(!battle_condition_state_decode(bytes, n, &reply));
}

static void test_stat_stages(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    const Action player = move(0, 0, 0), foe = move(1, 0, 0);
    make_conditions(&r, BATTLE_FORMAT_SINGLE);
    set_move(&r.player[0], 0, 31, condition_move(BATTLE_STAGE_EFFECT(BSTAT_ATK, 2), BTARGET_SELF), 20);
    set_move(&r.opponent[0], 0, 32, condition_move(BATTLE_STAGE_EFFECT(BSTAT_DEF, -2), BTARGET_SINGLE), 20);
    assert(battle_init(&b, &r, 1) == BI_OK);
    Battler *p = battle_active(&b, 0), *o = battle_active(&b, 1);
    for (unsigned i = 0; i < 4; ++i) {
        const Action actions[] = {player, foe};
        battle_run_turn(&b, actions, 2, &log);
        assert(event_count(&log, EV_STAGE) == (i == 3 ? 0 : 2));
    }
    assert(p->stat_stages[BSTAT_ATK] == 6 && p->stat_stages[BSTAT_DEF] == -6);

    uint8_t eff;
    const BattleMove physical = hit(40, T_NORMAL, BCAT_PHYSICAL), special = hit(40, T_NORMAL, BCAT_SPECIAL);
    p->stat_stages[BSTAT_ATK] = 2;
    assert(battle_damage(&b, p, &physical, o, 100, &eff) == 55 && eff == 4);
    o->stat_stages[BSTAT_DEF] = 2;
    assert(battle_damage(&b, p, &physical, o, 100, &eff) == 28);
    p->stat_stages[BSTAT_SPA] = 2;
    o->stat_stages[BSTAT_SPD] = 2;
    assert(battle_damage(&b, p, &special, o, 100, &eff) == 28);
    o->stat_stages[BSTAT_SPD] = 0;
    assert(battle_damage(&b, p, &special, o, 100, &eff) == 55);
    p->status = BST_BURN;
    assert(battle_damage(&b, p, &physical, o, 100, &eff) == 14);
    assert(battle_damage(&b, p, &special, o, 100, &eff) == 55);

    make_conditions(&r, BATTLE_FORMAT_SINGLE);
    r.player[0].mon.moves[0].accuracy = 100;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_active(&b, 0)->stat_stages[BSTAT_ACC] = -3;
    battle_active(&b, 1)->stat_stages[BSTAT_EVA] = 3;
    battle_run_turn(&b, &player, 1, &log);
    assert(event_count(&log, EV_MISS) == 1);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_active(&b, 0)->stat_stages[BSTAT_ACC] = 3;
    battle_active(&b, 1)->stat_stages[BSTAT_EVA] = 3;
    battle_run_turn(&b, &player, 1, &log);
    assert(event_count(&log, EV_DAMAGE) == 1);

    // The fast ally slows down the next actor. Dynamic speed recalculates
    // within the same priority bracket before the next move.
    make_conditions(&r, BATTLE_FORMAT_DOUBLE);
    r.player[0].record.speed = 100;
    r.opponent[0].record.speed = 90;
    r.player[1].record.speed = 60;
    set_move(&r.player[0], 0, 31, condition_move(BATTLE_STAGE_EFFECT(BSTAT_SPE, -2), BTARGET_SINGLE), 20);
    const Action double_actions[] = {player, foe, move(0, 1, 0)};
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, double_actions, 3, &log);
    assert(log.events[1].kind == EV_STAGE && log.events[1].stat == BSTAT_SPE);
    assert(log.events[2].kind == EV_MOVE_USED && log.events[2].user.side == 0 && log.events[2].user.slot == 1);
    r.conditions.flags &= ~BCR_DYNAMIC_SPEED;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, double_actions, 3, &log);
    assert(log.events[2].kind == EV_MOVE_USED && log.events[2].user.side == 1);
}

static void test_major_status(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    const Action player = move(0, 0, 0), foe = move(1, 0, 0);
    const Action both[] = {player, foe};
    make_conditions(&r, BATTLE_FORMAT_SINGLE);
    r.player[0].record.status = BST_PARALYSIS;
    r.player[0].record.speed = 100;
    r.opponent[0].record.speed = 80;
    assert(battle_init(&b, &r, 4) == BI_OK);
    battle_run_turn(&b, both, 2, &log);
    assert(log.events[0].kind == EV_MOVE_USED && log.events[0].user.side == 1);
    assert(event_count(&log, EV_STATUS_BLOCK) == 1 && battle_active(&b, 0)->pp[0] == 20);

    r.player[0].record.status = BATTLE_CONDITION(BST_SLEEP, 2);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, &player, 1, &log);
    assert(event_count(&log, EV_STATUS_BLOCK) == 1 && battle_active(&b, 0)->pp[0] == 20);
    battle_run_turn(&b, &player, 1, &log);
    assert(log.events[0].kind == EV_CURE && battle_active(&b, 0)->pp[0] == 19);

    r.player[0].record.status = BST_FREEZE;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, &player, 1, &log);
    assert(event_count(&log, EV_STATUS_BLOCK) == 1 && battle_active(&b, 0)->status == BST_FREEZE);
    assert(battle_init(&b, &r, 3) == BI_OK);
    battle_run_turn(&b, &player, 1, &log);
    assert(log.events[0].kind == EV_CURE && battle_active(&b, 0)->status == BST_NONE);
    r.opponent[0].mon.moves[0].type = T_FIRE;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, &foe, 1, &log);
    assert(log.events[2].kind == EV_CURE && battle_active(&b, 0)->status == BST_NONE);

    r.player[0].record.status = BST_NONE;
    set_move(&r.player[0], 0, 31, condition_move(BATTLE_STATUS_EFFECT(BST_TOXIC), BTARGET_SINGLE), 20);
    assert(battle_init(&b, &r, 4) == BI_OK);
    battle_run_turn(&b, &player, 1, &log);
    assert(log.events[1].kind == EV_STATUS && log.events[2].amount == 12);
    assert(battle_active(&b, 1)->status == BATTLE_CONDITION(BST_TOXIC, 1));
    battle_run_turn(&b, &player, 1, &log);
    assert(log.events[1].kind == EV_NO_EFFECT && log.events[2].amount == 24);
    battle_active(&b, 1)->status = BATTLE_CONDITION(BST_TOXIC, 15);
    battle_run_turn(&b, &player, 1, &log);
    assert(log.events[2].amount == 180 && log.events[3].kind == EV_FAINT && b.outcome == OUTCOME_WIN);

    r.opponent[0].mon.types[0] = T_GHOST;
    assert(battle_init(&b, &r, 4) == BI_OK);
    battle_run_turn(&b, &player, 1, &log);
    assert(log.events[1].kind == EV_NO_EFFECT && !battle_active(&b, 1)->status);
    set_move(&r.player[0], 0, 31, condition_move(BME_REFRESH, BTARGET_SELF), 20);
    r.player[0].record.status = BST_BURN;
    assert(battle_init(&b, &r, 4) == BI_OK);
    battle_run_turn(&b, &player, 1, &log);
    assert(log.events[1].kind == EV_CURE && !event_count(&log, EV_RESIDUAL));
}

static void test_condition_application(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    const Action player = move(0, 0, 0), foe = move(1, 0, 0);
    make_conditions(&r, BATTLE_FORMAT_SINGLE);
    set_move(&r.player[0], 0, 31, condition_move(BATTLE_STATUS_EFFECT(BST_SLEEP), BTARGET_SINGLE), 20);
    assert(battle_init(&b, &r, 4) == BI_OK);
    battle_run_turn(&b, &player, 1, &log);
    assert(log.events[1].kind == EV_STATUS && log.events[1].condition == BST_SLEEP);
    assert(BATTLE_CONDITION_COUNTER(battle_active(&b, 1)->status) >= r.conditions.sleep_min);
    battle_run_turn(&b, &foe, 1, &log);
    assert(event_count(&log, EV_STATUS_BLOCK) == 1);

    set_move(&r.player[0], 0, 31, condition_move(BME_ELECTRIC_PARALYSIS, BTARGET_SINGLE), 20);
    r.player[0].mon.moves[0].type = T_WATER;
    r.opponent[0].mon.types[0] = T_WATER;
    assert(battle_init(&b, &r, 4) == BI_OK);
    battle_run_turn(&b, &player, 1, &log);
    assert(log.events[1].kind == EV_NO_EFFECT);
    r.opponent[0].mon.types[0] = T_NORMAL;
    r.type_chart[T_WATER * NTYPES + T_NORMAL] = 0;
    assert(battle_init(&b, &r, 4) == BI_OK);
    battle_run_turn(&b, &player, 1, &log);
    assert(log.events[1].kind == EV_NO_EFFECT);
    r.type_chart[T_WATER * NTYPES + T_NORMAL] = 4;
    assert(battle_init(&b, &r, 4) == BI_OK);
    battle_run_turn(&b, &player, 1, &log);
    assert(log.events[1].kind == EV_STATUS && battle_active(&b, 1)->status == BST_PARALYSIS);

    r.player[0].record.status = BST_FROSTBITE;
    assert(battle_init(&b, &r, 4) == BI_CONDITIONS);
    r.conditions.flags |= BCR_FROSTBITE;
    assert(battle_init(&b, &r, 4) == BI_OK);
    uint8_t eff;
    const BattleMove special = hit(40, T_NORMAL, BCAT_SPECIAL);
    assert(battle_damage(&b, battle_active(&b, 0), &special, battle_active(&b, 1), 100, &eff) == 14);
    battle_run_turn(&b, NULL, 0, &log);
    assert(log.events[0].kind == EV_RESIDUAL && log.events[0].condition == BST_FROSTBITE && log.events[0].amount == 12);
}

static void test_pivot_then_residual(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    make_conditions(&r, BATTLE_FORMAT_DOUBLE);
    add_reserve(&r, 0);
    r.player[0].mon.moves[0].effect = BME_PIVOT;
    r.player[0].record.status = BST_TOXIC;
    r.player[1].record.status = BST_POISON;
    r.player[1].record.speed = 10;
    assert(battle_init(&b, &r, 4) == BI_OK);
    const Action actions[] = {move(0, 0, 0), move(0, 1, 0)};
    battle_run_turn(&b, actions, 2, &log);
    assert(battle_pending_pivot(&b, NULL) && !b.turn && !event_count(&log, EV_RESIDUAL));
    assert(battle_resume_pivot(&b, 2, &log));
    assert(b.turn == 1 && !battle_pending_pivot(&b, NULL) && event_count(&log, EV_RESIDUAL) == 1);
    assert(log.events[log.count - 1].target.slot == 1 && b.sides[0].battlers[0].status == BST_TOXIC);
}

static void test_residual_lifecycle(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    make_conditions(&r, BATTLE_FORMAT_DOUBLE);
    add_reserve(&r, 0);
    set_move(&r.player[0], 0, 31, condition_move(BATTLE_STAGE_EFFECT(BSTAT_ATK, -1), BTARGET_ALL_FOES), 20);
    r.opponent[0].record.status = BST_POISON;
    r.opponent[1].record.status = BST_BURN;
    r.player[0].record.status = BST_BURN;
    r.player[1].record.status = BST_POISON;
    r.player[0].record.hp = r.player[1].record.hp = 1;
    assert(battle_init(&b, &r, 1) == BI_OK);
    const Action player = move(0, 0, 0);
    battle_run_turn(&b, &player, 1, &log);
    assert(event_count(&log, EV_STAGE) == 2 && event_count(&log, EV_RESIDUAL) == 4);
    assert(log.events[3].condition == BST_POISON && log.events[3].target.side == 0 && log.events[3].target.slot == 1);
    assert(log.events[4].kind == EV_FAINT);
    assert(log.events[5].condition == BST_POISON && log.events[5].amount == 25);
    assert(log.events[6].condition == BST_BURN && log.events[6].target.side == 1 && log.events[6].amount == 12);
    assert(log.events[7].condition == BST_BURN && log.events[8].kind == EV_FAINT);
    Position pos;
    assert(battle_pending_replacement(&b, &pos) && pos.side == 0 && pos.slot == 0);
    assert(battle_replace(&b, pos, 2, &log));
    assert(!battle_pending_replacement(&b, NULL) && !battle_at(&b, (Position){0, 1}));
    assert(!battle_replace(&b, (Position){0, 1}, 2, &log) && battle_validate_action(&b, &player));

    make_conditions(&r, BATTLE_FORMAT_TRIPLE);
    add_reserve(&r, 0);
    for (unsigned i = 0; i < 3; ++i) {
        r.player[i].record.status = BST_POISON;
        r.opponent[i].record.status = BST_BURN;
    }
    r.player[BPOS_RIGHT].record.hp = 1;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, NULL, 0, &log);
    assert(event_count(&log, EV_RESIDUAL) == 6);
    assert(battle_pending_replacement(&b, &pos) && pos.slot == BPOS_RIGHT);
    assert(battle_replace(&b, pos, 3, &log));

    make_conditions(&r, BATTLE_FORMAT_SINGLE);
    r.player[0].record.status = BST_BURN;
    r.opponent[0].record.status = BST_POISON;
    r.player[0].record.hp = r.opponent[0].record.hp = 1;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, NULL, 0, &log);
    assert(b.outcome == OUTCOME_DRAW && log.events[4].kind == EV_END && !battle_pending_replacement(&b, NULL));
}

static void test_switch_rotation_conditions(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    make_conditions(&r, BATTLE_FORMAT_SINGLE);
    add_reserve(&r, 0);
    r.player[0].record.status = BATTLE_CONDITION(BST_TOXIC, 5);
    assert(battle_init(&b, &r, 1) == BI_OK);
    Battler *original = battle_active(&b, 0);
    original->stat_stages[BSTAT_ATK] = 4;
    Action swap = {.kind = ACT_SWITCH, .user = {0, 0}, .reserve = 1};
    battle_run_turn(&b, &swap, 1, &log);
    assert(original->status == BST_TOXIC && original->stat_stages[BSTAT_ATK] == 0);
    assert(original->hp == 200 && !event_count(&log, EV_RESIDUAL));

    make_conditions(&r, BATTLE_FORMAT_ROTATION);
    r.player[0].record.status = BATTLE_CONDITION(BST_TOXIC, 3);
    r.player[1].record.status = BST_BURN;
    r.player[1].record.speed = 30;
    r.opponent[0].record.speed = 100;
    assert(battle_init(&b, &r, 1) == BI_OK);
    original = battle_active(&b, 0);
    original->stat_stages[BSTAT_SPE] = 4;
    Battler *wing = &b.sides[0].battlers[1];
    wing->stat_stages[BSTAT_SPE] = 6;
    Action rotate = move(0, BPOS_CENTER, BPOS_CENTER);
    rotate.rotation = BROT_LEFT;
    const Action both[] = {rotate, move(1, BPOS_CENTER, BPOS_CENTER)};
    battle_run_turn(&b, both, 2, &log);
    assert(battle_active(&b, 0) == wing && wing->stat_stages[BSTAT_SPE] == 6);
    assert(log.events[0].kind == EV_ROTATE && log.events[1].kind == EV_MOVE_USED && log.events[1].user.side == 0);
    assert(wing->status == BST_BURN && event_count(&log, EV_RESIDUAL) == 1);
    assert(original->hp == 200 && original->status == BATTLE_CONDITION(BST_TOXIC, 3));
    assert(original->stat_stages[BSTAT_SPE] == 4);
    set_fainted(&b, 0, 2);
    rotate.rotation = BROT_LEFT;
    assert(!battle_validate_action(&b, &rotate));
}


// ---------------------------------------------------------------- V8 move effects

static BattleEffect fx(uint8_t kind, uint8_t who, uint8_t chance, uint8_t arg0, int8_t arg1, uint8_t arg2) {
    return (BattleEffect){.kind = kind, .who = who, .chance = chance, .arg0 = arg0, .arg1 = arg1, .arg2 = arg2};
}
#define DAMAGE1 fx(BEFF_DAMAGE, BWHO_TARGET, 0, 1, 0, 1)

static void push(BattleMove *m, BattleEffect e) {
    assert(m->effect_count < BATTLE_MAX_EFFECTS);
    m->effects[m->effect_count++] = e;
}

static BattleMove status_move(uint8_t target, uint8_t accuracy) {
    return (BattleMove){.category = BCAT_STATUS, .target = target, .accuracy = accuracy};
}

static BattleMove damaging(uint16_t power, uint8_t type, uint8_t accuracy) {
    BattleMove m = hit(power, type, BCAT_PHYSICAL);
    m.accuracy = accuracy;
    push(&m, DAMAGE1);
    return m;
}

// V7 condition request upgraded to V8: every damaging move gets a DAMAGE effect.
static void make_effects(BattleRequest *r, uint8_t format) {
    make_conditions(r, format);
    r->version = BATTLE_REQUEST_VERSION_EFFECTS;
    for (unsigned side = 0; side < 2; ++side) {
        BattleEntry *entries = side ? r->opponent : r->player;
        const unsigned count = side ? r->opponent_count : r->player_count;
        for (unsigned i = 0; i < count; ++i)
            for (unsigned m = 0; m < 4; ++m) {
                BattleMove *move = &entries[i].mon.moves[m];
                if (move->unsupported || move->effect_count) continue;
                move->effect = BME_HIT;
                if (move->power) push(move, DAMAGE1);
            }
    }
}

static const BattleEvent *find(const EventLog *log, EventKind kind) {
    for (unsigned i = 0; i < log->count; ++i)
        if (log->events[i].kind == kind) return &log->events[i];
    return NULL;
}

static void test_effects_wire(void) {
    static BattleRequest r, decoded;
    static uint8_t bytes[4096];
    make_effects(&r, BATTLE_FORMAT_DOUBLE);
    BattleMove thunder = damaging(90, T_WATER, 100);
    thunder.category = BCAT_SPECIAL;
    thunder.source_effect = 0x1234;
    thunder.move_flags = BMF_IGNORES_PROTECT;
    push(&thunder, fx(BEFF_STATUS, BWHO_TARGET, 10, BST_PARALYSIS, 0, 0));
    push(&thunder, fx(BEFF_STAT, BWHO_USER, 100, BSTAT_SPA, -2, 0));
    set_move(&r.player[0], 1, 85, thunder, 15);
    const uint32_t n = battle_request_encode(&r, bytes, sizeof(bytes));
    // 4 entries x 4 moves: headers + 1 DAMAGE for each plain move, 3 effects for the new one.
    const uint32_t effects = 16 * BATTLE_MOVE_EFFECTS_HEADER + (4 + 3) * BATTLE_EFFECT_BYTES;
    assert(n == BATTLE_REQUEST_HEADER + 4 * BATTLE_ENTRY_BYTES + NTYPES * NTYPES + BATTLE_CONDITION_RULE_BYTES + effects);
    assert(n == battle_request_size(&r));
    assert(battle_request_decode(bytes, n, &decoded));
    const BattleMove *d = &decoded.player[0].mon.moves[1];
    assert(d->effect_count == 3 && d->source_effect == 0x1234 && d->move_flags == BMF_IGNORES_PROTECT);
    assert(d->effects[1].kind == BEFF_STATUS && d->effects[1].chance == 10 && d->effects[1].arg0 == BST_PARALYSIS);
    assert(d->effects[2].who == BWHO_USER && d->effects[2].arg1 == -2);
    assert(decoded.opponent[1].mon.moves[0].effects[0].kind == BEFF_DAMAGE);
    // Malformed input is rejected: truncation, trailing data, reserved bytes, invalid descriptors.
    assert(!battle_request_decode(bytes, n - 1, &decoded));
    bytes[n] = 0;
    assert(!battle_request_decode(bytes, n + 1, &decoded));
    const unsigned fixed = BATTLE_REQUEST_HEADER + 4 * BATTLE_ENTRY_BYTES + NTYPES * NTYPES + BATTLE_CONDITION_RULE_BYTES;
    const unsigned second = fixed + BATTLE_MOVE_EFFECTS_HEADER + BATTLE_EFFECT_BYTES;  // player 0, move 1
    uint8_t saved = bytes[second + BATTLE_MOVE_EFFECTS_HEADER + 7];
    bytes[second + BATTLE_MOVE_EFFECTS_HEADER + 7] = 1;   // reserved descriptor byte
    assert(!battle_request_decode(bytes, n, &decoded));
    bytes[second + BATTLE_MOVE_EFFECTS_HEADER + 7] = saved;
    saved = bytes[second + BATTLE_MOVE_EFFECTS_HEADER + 2 * BATTLE_EFFECT_BYTES + 4];
    bytes[second + BATTLE_MOVE_EFFECTS_HEADER + 2 * BATTLE_EFFECT_BYTES + 4] = 0;  // stat delta 0
    assert(!battle_request_decode(bytes, n, &decoded));
    bytes[second + BATTLE_MOVE_EFFECTS_HEADER + 2 * BATTLE_EFFECT_BYTES + 4] = saved;
    bytes[second] = BATTLE_MAX_EFFECTS + 1;
    assert(!battle_request_decode(bytes, n, &decoded));
    bytes[second] = 3;
    assert(battle_request_decode(bytes, n, &decoded));
    bytes[BATTLE_REQUEST_HEADER + PKMN_RECORD_BYTES + 4] = 1;  // V8 keeps the legacy effect byte reserved
    assert(!battle_request_decode(bytes, n, &decoded));
    bytes[BATTLE_REQUEST_HEADER + PKMN_RECORD_BYTES + 4] = 0;
    bytes[0] = BATTLE_REQUEST_VERSION_EFFECTS + 1;
    assert(!battle_request_decode(bytes, n, &decoded));

    // Structural rules shared by both sides.
    BattleMove m = status_move(BTARGET_SELF, 0);
    assert(!battle_move_effects_valid(&m));                      // empty list
    push(&m, fx(BEFF_STAT, BWHO_TARGET, 0, BSTAT_ATK, 2, 0));
    assert(battle_move_effects_valid(&m));
    m.effects[0].chance = 30;
    assert(!battle_move_effects_valid(&m));                      // chances only on hits
    m = damaging(40, T_NORMAL, 100);
    m.effects[0].arg1 = BHITS_2TO5_GEN5;
    assert(!battle_move_effects_valid(&m));                      // 2-5 rule needs 2..5
    m.effects[0].arg0 = 2;
    m.effects[0].arg2 = 5;
    assert(battle_move_effects_valid(&m));
    m = hit(40, T_NORMAL, BCAT_PHYSICAL);
    push(&m, fx(BEFF_STAT, BWHO_USER, 0, BSTAT_ATK, 1, 0));
    assert(!battle_move_effects_valid(&m));                      // damaging move without DAMAGE
    m = status_move(BTARGET_SELF, 0);
    push(&m, fx(BEFF_PROTECT, BWHO_USER, 0, 3, 0, 0));
    push(&m, fx(BEFF_STAT, BWHO_TARGET, 0, BSTAT_ATK, 1, 0));
    assert(!battle_move_effects_valid(&m));                      // Protect stands alone
}

static void run1(Battle *b, Action player, Action foe, EventLog *log) {
    const Action actions[] = {player, foe};
    battle_run_turn(b, actions, 2, log);
}

static void test_effect_status_and_stats(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    const Action player = move(0, 0, 0), foe = move(1, 0, 0);
    // Swords Dance (+2 Atk, self) and Screech (-2 Def, target) through descriptors.
    make_effects(&r, BATTLE_FORMAT_SINGLE);
    BattleMove swords = status_move(BTARGET_SELF, 0), screech = status_move(BTARGET_SINGLE, 0);
    push(&swords, fx(BEFF_STAT, BWHO_TARGET, 0, BSTAT_ATK, 2, 0));
    push(&screech, fx(BEFF_STAT, BWHO_TARGET, 0, BSTAT_DEF, -2, 0));
    set_move(&r.player[0], 0, 14, swords, 20);
    set_move(&r.opponent[0], 0, 103, screech, 40);
    assert(battle_init(&b, &r, 1) == BI_OK);
    for (unsigned i = 0; i < 4; ++i) {
        run1(&b, player, foe, &log);
        assert(event_count(&log, EV_STAGE) == (i == 3 ? 0 : 2) && event_count(&log, EV_NO_EFFECT) == (i == 3 ? 2 : 0));
    }
    assert(battle_active(&b, 0)->stat_stages[BSTAT_ATK] == 6 && battle_active(&b, 0)->stat_stages[BSTAT_DEF] == -6);

    // Dragon-Dance-style lists apply every stat; one change is enough to count.
    make_effects(&r, BATTLE_FORMAT_SINGLE);
    BattleMove dance = status_move(BTARGET_SELF, 0);
    push(&dance, fx(BEFF_STAT, BWHO_TARGET, 0, BSTAT_ATK, 1, 0));
    push(&dance, fx(BEFF_STAT, BWHO_TARGET, 0, BSTAT_SPE, 1, 0));
    set_move(&r.player[0], 0, 349, dance, 20);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_active(&b, 0)->stat_stages[BSTAT_ATK] = 6;
    battle_run_turn(&b, &player, 1, &log);
    assert(event_count(&log, EV_STAGE) == 1 && !find(&log, EV_NO_EFFECT));
    assert(battle_active(&b, 0)->stat_stages[BSTAT_SPE] == 1);

    // Will-O-Wisp: a Fire type is immune; Thunder Wave follows the type chart.
    make_effects(&r, BATTLE_FORMAT_SINGLE);
    BattleMove wisp = status_move(BTARGET_SINGLE, 0), wave = status_move(BTARGET_SINGLE, 0);
    wisp.type = T_FIRE;
    push(&wisp, fx(BEFF_STATUS, BWHO_TARGET, 0, BST_BURN, 0, 0));
    wave.type = T_NORMAL;
    push(&wave, fx(BEFF_STATUS, BWHO_TARGET, 0, BST_PARALYSIS, 0, 0));
    wave.effects[0].flags = BEFL_TYPE_CHART;
    set_move(&r.player[0], 0, 261, wisp, 15);
    set_move(&r.player[0], 1, 86, wave, 20);
    r.opponent[0].mon.types[0] = r.opponent[0].mon.types[1] = T_FIRE;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, &player, 1, &log);
    assert(find(&log, EV_NO_EFFECT) && !find(&log, EV_STATUS));
    r.opponent[0].mon.types[0] = r.opponent[0].mon.types[1] = T_GHOST;   // Normal has no effect on Ghost
    assert(battle_init(&b, &r, 1) == BI_OK);
    Action wave_action = move(0, 0, 0);
    wave_action.move_slot = 1;
    battle_run_turn(&b, &wave_action, 1, &log);
    assert(find(&log, EV_NO_EFFECT) && !find(&log, EV_STATUS));
    r.player[0].mon.moves[1].effects[0].flags = 0;                      // without the flag it lands
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, &wave_action, 1, &log);
    assert(find(&log, EV_STATUS) && find(&log, EV_STATUS)->condition == BST_PARALYSIS);

    // Hypnosis: sleep counter within the rules' range; Toxic on an already statused target fails.
    make_effects(&r, BATTLE_FORMAT_SINGLE);
    BattleMove hypnosis = status_move(BTARGET_SINGLE, 0), toxic = status_move(BTARGET_SINGLE, 0);
    push(&hypnosis, fx(BEFF_STATUS, BWHO_TARGET, 0, BST_SLEEP, 0, 0));
    push(&toxic, fx(BEFF_STATUS, BWHO_TARGET, 0, BST_TOXIC, 0, 0));
    set_move(&r.player[0], 0, 95, hypnosis, 20);
    set_move(&r.player[0], 1, 92, toxic, 10);
    for (uint32_t seed = 1; seed < 40; ++seed) {
        assert(battle_init(&b, &r, seed) == BI_OK);
        battle_run_turn(&b, &player, 1, &log);
        const unsigned counter = BATTLE_CONDITION_COUNTER(battle_active(&b, 1)->status);
        assert(BATTLE_CONDITION_KIND(battle_active(&b, 1)->status) == BST_SLEEP && counter >= 2 && counter <= 4);
        Action toxic_action = player;
        toxic_action.move_slot = 1;
        battle_run_turn(&b, &toxic_action, 1, &log);
        assert(find(&log, EV_NO_EFFECT) && BATTLE_CONDITION_KIND(battle_active(&b, 1)->status) == BST_SLEEP);
    }
}

static void test_effect_heal_drain_recoil(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    const Action player = move(0, 0, 0);
    // Recover: floor(max / 2), capped; at full HP it fails.
    make_effects(&r, BATTLE_FORMAT_SINGLE);
    BattleMove recover = status_move(BTARGET_SELF, 0);
    push(&recover, fx(BEFF_HEAL, BWHO_USER, 0, 1, 0, 2));
    set_move(&r.player[0], 0, 105, recover, 5);
    r.player[0].record.max_hp = 201;
    r.player[0].record.hp = 201;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, &player, 1, &log);
    assert(find(&log, EV_FAIL) && !find(&log, EV_HEAL));
    battle_active(&b, 0)->hp = 20;
    battle_run_turn(&b, &player, 1, &log);
    assert(find(&log, EV_HEAL)->amount == 100 && battle_active(&b, 0)->hp == 120);
    battle_active(&b, 0)->hp = 180;
    battle_run_turn(&b, &player, 1, &log);
    assert(find(&log, EV_HEAL)->amount == 21 && battle_active(&b, 0)->hp == 201);

    // Giga Drain: half of the HP the target actually lost, at least 1.
    make_effects(&r, BATTLE_FORMAT_SINGLE);
    BattleMove drain = damaging(75, T_NORMAL, 0);
    drain.category = BCAT_SPECIAL;
    push(&drain, fx(BEFF_DRAIN, BWHO_USER, 0, 50, 0, 0));
    set_move(&r.player[0], 0, 202, drain, 10);
    assert(battle_init(&b, &r, 3) == BI_OK);
    battle_active(&b, 0)->hp = 10;
    battle_active(&b, 1)->hp = 9;   // overkill: only 9 HP is lost
    battle_run_turn(&b, &player, 1, &log);
    assert(find(&log, EV_DRAIN)->amount == 4 && battle_active(&b, 0)->hp == 14);
    assert(b.outcome == OUTCOME_WIN);
    assert(battle_init(&b, &r, 3) == BI_OK);
    battle_run_turn(&b, &player, 1, &log);   // full HP: nothing to drain into
    assert(!find(&log, EV_DRAIN) && find(&log, EV_DAMAGE));

    // Double-Edge: 33% of the HP lost by the target; recoil can knock the user out.
    make_effects(&r, BATTLE_FORMAT_SINGLE);
    BattleMove edge = damaging(120, T_NORMAL, 0);
    push(&edge, fx(BEFF_RECOIL, BWHO_USER, 0, 33, 0, 0));
    set_move(&r.player[0], 0, 38, edge, 15);
    assert(battle_init(&b, &r, 5) == BI_OK);
    battle_run_turn(&b, &player, 1, &log);
    const BattleEvent *dmg = find(&log, EV_DAMAGE), *recoil = find(&log, EV_RECOIL);
    assert(dmg && recoil && recoil->amount == dmg->amount * 33 / 100);
    assert(battle_init(&b, &r, 5) == BI_OK);
    battle_active(&b, 0)->hp = 1;
    battle_active(&b, 1)->hp = 1;
    battle_run_turn(&b, &player, 1, &log);
    assert(find(&log, EV_RECOIL)->amount == 1 && b.outcome == OUTCOME_DRAW);
}

static void test_effect_secondary(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    const Action player = move(0, 0, 0);
    // Thunderbolt-style 10% paralysis: over many seeds roughly 10% of hits, never on a fainted target.
    make_effects(&r, BATTLE_FORMAT_SINGLE);
    BattleMove bolt = damaging(20, T_NORMAL, 0);
    push(&bolt, fx(BEFF_STATUS, BWHO_TARGET, 10, BST_PARALYSIS, 0, 0));
    set_move(&r.player[0], 0, 85, bolt, 15);
    unsigned paralyzed = 0;
    for (uint32_t seed = 1; seed <= 2000; ++seed) {
        assert(battle_init(&b, &r, seed) == BI_OK);
        battle_run_turn(&b, &player, 1, &log);
        paralyzed += find(&log, EV_STATUS) != NULL;
    }
    assert(paralyzed > 140 && paralyzed < 260);
    r.player[0].mon.moves[0].effects[1].chance = 100;
    assert(battle_init(&b, &r, 9) == BI_OK);
    battle_active(&b, 1)->hp = 1;
    battle_run_turn(&b, &player, 1, &log);
    assert(find(&log, EV_FAINT) && !find(&log, EV_STATUS));
    // A self effect still applies after the target fainted (Close-Combat style).
    r.player[0].mon.moves[0].effects[1] = fx(BEFF_STAT, BWHO_USER, 0, BSTAT_DEF, -1, 0);
    assert(battle_init(&b, &r, 9) == BI_OK);
    battle_active(&b, 1)->hp = 1;
    battle_run_turn(&b, &player, 1, &log);
    assert(find(&log, EV_STAGE) && battle_active(&b, 0)->stat_stages[BSTAT_DEF] == -1);
    // Flamethrower-style burn cannot land on a Fire type; Ice Beam freeze respects the rules.
    r.player[0].mon.moves[0].effects[1] = fx(BEFF_STATUS, BWHO_TARGET, 100, BST_BURN, 0, 0);
    r.opponent[0].mon.types[0] = r.opponent[0].mon.types[1] = T_FIRE;
    assert(battle_init(&b, &r, 9) == BI_OK);
    battle_run_turn(&b, &player, 1, &log);
    assert(!find(&log, EV_STATUS));
    BattleMove frostbite = damaging(20, T_NORMAL, 0);
    push(&frostbite, fx(BEFF_STATUS, BWHO_TARGET, 10, BST_FROSTBITE, 0, 0));
    assert(!battle_move_supported(&r, &frostbite));   // the rules use freeze, not frostbite
}

static void test_effect_multi_hit(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    const Action player = move(0, 0, 0);
    make_effects(&r, BATTLE_FORMAT_SINGLE);
    BattleMove swipes = damaging(10, T_NORMAL, 0);
    swipes.effects[0] = fx(BEFF_DAMAGE, BWHO_TARGET, 0, 2, BHITS_2TO5_GEN5, 5);
    set_move(&r.player[0], 0, 154, swipes, 15);
    r.opponent[0].record.hp = r.opponent[0].record.max_hp = 999;
    unsigned counts[6] = {0};
    for (uint32_t seed = 1; seed <= 4000; ++seed) {
        assert(battle_init(&b, &r, seed) == BI_OK);
        battle_run_turn(&b, &player, 1, &log);
        const BattleEvent *multi = find(&log, EV_MULTI_HIT);
        assert(multi && multi->amount == event_count(&log, EV_DAMAGE));
        ++counts[multi->amount];
    }
    assert(counts[2] > 1200 && counts[2] < 1600 && counts[3] > 1200 && counts[3] < 1600);
    assert(counts[4] > 450 && counts[4] < 750 && counts[5] > 450 && counts[5] < 750);
    // Accuracy is checked once; hits stop when the target faints.
    r.player[0].mon.moves[0].effects[0] = fx(BEFF_DAMAGE, BWHO_TARGET, 0, 5, 0, 5);
    r.opponent[0].record.hp = 1;
    assert(battle_init(&b, &r, 2) == BI_OK);
    battle_run_turn(&b, &player, 1, &log);
    assert(event_count(&log, EV_DAMAGE) == 1 && find(&log, EV_MULTI_HIT)->amount == 1 && find(&log, EV_FAINT));
}

static void test_effect_priority_protect(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    // Quick Attack (+1) goes before a faster foe.
    make_effects(&r, BATTLE_FORMAT_SINGLE);
    BattleMove quick = damaging(40, T_NORMAL, 0);
    quick.priority = 1;
    set_move(&r.player[0], 0, 98, quick, 30);
    r.player[0].record.speed = 10;
    r.opponent[0].record.speed = 200;
    assert(battle_init(&b, &r, 1) == BI_OK);
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    assert(log.events[0].kind == EV_MOVE_USED && log.events[0].user.side == 0);

    // Protect (+4) blocks the foe's attack and status move.
    make_effects(&r, BATTLE_FORMAT_SINGLE);
    BattleMove guard = status_move(BTARGET_SELF, 0);
    guard.priority = 4;
    push(&guard, fx(BEFF_PROTECT, BWHO_USER, 0, 3, 0, 0));
    set_move(&r.player[0], 0, 182, guard, 10);
    BattleMove growl = status_move(BTARGET_SINGLE, 0);
    push(&growl, fx(BEFF_STAT, BWHO_TARGET, 0, BSTAT_ATK, -1, 0));
    set_move(&r.opponent[0], 1, 45, growl, 40);
    assert(battle_init(&b, &r, 1) == BI_OK);
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    assert(find(&log, EV_PROTECT) && find(&log, EV_PROTECTED) && !find(&log, EV_DAMAGE));
    // Consecutive uses succeed with 1/3, 1/9, ...; a Protect chain resets after another move.
    unsigned second = 0;
    for (uint32_t seed = 1; seed <= 900; ++seed) {
        assert(battle_init(&b, &r, seed) == BI_OK);
        run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
        run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
        second += find(&log, EV_PROTECT) != NULL;
        if (find(&log, EV_PROTECT)) assert(battle_active(&b, 0)->vol.protect_uses == 2);
        else assert(find(&log, EV_FAIL) && battle_active(&b, 0)->vol.protect_uses == 0);
    }
    assert(second > 240 && second < 360);
    // Protect fails when the user is the last to move.
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){move(0, 0, 0)}, 1, &log);
    assert(find(&log, EV_FAIL) && !battle_active(&b, 0)->vol.protected_turn);
    // A move that ignores Protect goes through; the flag is gone next turn.
    r.opponent[0].mon.moves[0].move_flags = BMF_IGNORES_PROTECT;
    assert(battle_init(&b, &r, 1) == BI_OK);
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    assert(find(&log, EV_PROTECT) && find(&log, EV_DAMAGE));
    battle_run_turn(&b, (Action[]){move(1, 0, 0)}, 1, &log);
    assert(!battle_active(&b, 0)->vol.protected_turn && find(&log, EV_DAMAGE));
}

static void test_effect_formats(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    // Double: Growl lowers both foes; a protected foe is skipped by a spread hit.
    make_effects(&r, BATTLE_FORMAT_DOUBLE);
    BattleMove growl = status_move(BTARGET_ALL_FOES, 100);
    push(&growl, fx(BEFF_STAT, BWHO_TARGET, 0, BSTAT_ATK, -1, 0));
    set_move(&r.player[0], 1, 45, growl, 40);
    Action g = move(0, 0, 0);
    g.move_slot = 1;
    assert(battle_init(&b, &r, 4) == BI_OK);
    battle_run_turn(&b, &g, 1, &log);
    assert(event_count(&log, EV_STAGE) == 2 && battle_at(&b, (Position){1, 1})->stat_stages[BSTAT_ATK] == -1);
    BattleMove surf = damaging(90, T_WATER, 0);
    surf.category = BCAT_SPECIAL;
    surf.target = BTARGET_ALL_ADJACENT;
    push(&surf, fx(BEFF_STAT, BWHO_TARGET, 100, BSTAT_SPE, -1, 0));
    set_move(&r.player[0], 2, 57, surf, 15);
    BattleMove guard = status_move(BTARGET_SELF, 0);
    guard.priority = 4;
    push(&guard, fx(BEFF_PROTECT, BWHO_USER, 0, 3, 0, 0));
    set_move(&r.opponent[0], 1, 182, guard, 10);
    assert(battle_init(&b, &r, 4) == BI_OK);
    Action s = move(0, 0, 0), p = move(1, 0, 0);
    s.move_slot = 2;
    p.move_slot = 1;
    const Action actions[] = {s, p};
    battle_run_turn(&b, actions, 2, &log);
    assert(event_count(&log, EV_PROTECTED) == 1 && event_count(&log, EV_DAMAGE) == 2);   // ally and the other foe
    assert(battle_at(&b, (Position){0, 1})->stat_stages[BSTAT_SPE] == -1 &&
           battle_at(&b, (Position){1, 0})->stat_stages[BSTAT_SPE] == 0);

    // Triple: a single-target status effect respects distance from an edge.
    make_effects(&r, BATTLE_FORMAT_TRIPLE);
    BattleMove screech = status_move(BTARGET_SINGLE, 0);
    push(&screech, fx(BEFF_STAT, BWHO_TARGET, 0, BSTAT_DEF, -2, 0));
    set_move(&r.player[0], 1, 103, screech, 40);
    assert(battle_init(&b, &r, 1) == BI_OK);
    assert(battle_command_targets(&b, (Position){0, 0}, 1) == 0x3);   // left reaches left/center only
    Action far = move(0, 0, 2);
    far.move_slot = 1;
    assert(!battle_validate_action(&b, &far));

    // Rotation: a raised stage stays with the battler while it is a wing.
    make_effects(&r, BATTLE_FORMAT_ROTATION);
    BattleMove swords = status_move(BTARGET_SELF, 0);
    push(&swords, fx(BEFF_STAT, BWHO_TARGET, 0, BSTAT_ATK, 2, 0));
    set_move(&r.player[0], 1, 14, swords, 20);
    assert(battle_init(&b, &r, 1) == BI_OK);
    Action sd = move(0, BPOS_CENTER, BPOS_CENTER);
    sd.move_slot = 1;
    battle_run_turn(&b, &sd, 1, &log);
    const uint8_t front = b.sides[0].active[BPOS_CENTER];
    assert(b.sides[0].battlers[front].stat_stages[BSTAT_ATK] == 2);
    Action rotate_left = move(0, BPOS_CENTER, BPOS_CENTER);
    rotate_left.rotation = BROT_LEFT;
    battle_run_turn(&b, &rotate_left, 1, &log);
    assert(b.sides[0].active[BPOS_CENTER] != front && b.sides[0].battlers[front].stat_stages[BSTAT_ATK] == 2);
}

static void test_effect_lifecycle(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    // U-turn and Dragon Tail as descriptor lists reuse the pivot / forced-switch lifecycle.
    make_effects(&r, BATTLE_FORMAT_SINGLE);
    add_reserve(&r, 0);
    add_reserve(&r, 1);
    BattleMove uturn = damaging(70, T_NORMAL, 0), tail = damaging(60, T_NORMAL, 0);
    push(&uturn, fx(BEFF_PIVOT, BWHO_TARGET, 0, 0, 0, 0));
    tail.priority = -6;
    push(&tail, fx(BEFF_FORCE_OUT, BWHO_TARGET, 0, 0, 0, 0));
    set_move(&r.player[0], 0, 369, uturn, 20);
    set_move(&r.player[1], 0, 369, uturn, 20);
    set_move(&r.opponent[0], 0, 525, tail, 10);
    set_move(&r.opponent[1], 0, 525, tail, 10);
    assert(battle_init(&b, &r, 2) == BI_OK);
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    Position pivot;
    assert(battle_pending_pivot(&b, &pivot) && pivot.side == 0);
    assert(battle_resume_pivot(&b, 1, &log));
    assert(find(&log, EV_WITHDRAW) && find(&log, EV_DRAG_OUT));   // the entrant is dragged out by Dragon Tail
    // Unsupported lists are not usable: no silent fallback to a plain hit.
    make_effects(&r, BATTLE_FORMAT_SINGLE);
    BattleMove bad = hit(80, T_NORMAL, BCAT_PHYSICAL);           // damaging move without a DAMAGE effect
    set_move(&r.player[0], 1, 44, bad, 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    assert(!(battle_usable_moves(&b, battle_active(&b, 0)) & 2));
    r.player[0].mon.moves[1].unsupported = UNSUP_SECONDARY;
    push(&r.player[0].mon.moves[1], DAMAGE1);
    assert(battle_init(&b, &r, 1) == BI_OK);
    assert(!(battle_usable_moves(&b, battle_active(&b, 0)) & 2));

    // Same seed, same inputs: identical event logs.
    EventLog first, again;
    make_effects(&r, BATTLE_FORMAT_DOUBLE);
    BattleMove swipes = damaging(15, T_NORMAL, 80);
    swipes.effects[0] = fx(BEFF_DAMAGE, BWHO_TARGET, 0, 2, BHITS_2TO5_GEN5, 5);
    push(&swipes, fx(BEFF_STATUS, BWHO_TARGET, 30, BST_POISON, 0, 0));
    set_move(&r.player[0], 0, 154, swipes, 15);
    const Action turn[] = {move(0, 0, 1), move(1, 0, 0), move(0, 1, 0), move(1, 1, 1)};
    assert(battle_init(&b, &r, 77) == BI_OK);
    battle_run_turn(&b, turn, 4, &first);
    assert(battle_init(&b, &r, 77) == BI_OK);
    battle_run_turn(&b, turn, 4, &again);
    assert(first.count == again.count && first.count > 4);
    for (unsigned i = 0; i < first.count; ++i) {
        const BattleEvent *x = &first.events[i], *y = &again.events[i];
        assert(x->kind == y->kind && x->amount == y->amount && x->hp_after == y->hp_after &&
               x->target.side == y->target.side && x->target.slot == y->target.slot && x->condition == y->condition);
    }
}

// ---------------------------------------------------------------- V9 volatiles and multi-turn actions

static void make_volatiles(BattleRequest *r, uint8_t format) {
    make_effects(r, format);
    r->version = BATTLE_REQUEST_VERSION_VOLATILES;
    r->volatiles = (BattleVolatileRules){
        .flags = BVR_SUBSTITUTE_MULTI_HIT_CONTINUES, .confusion_hit_percent = 33,
        .confusion_min = 2, .confusion_max = 5, .confusion_power = 40, .substitute_divisor = 4,
    };
}

static BattleMove effect_move(uint8_t target, uint8_t accuracy, BattleEffect e) {
    BattleMove m = status_move(target, accuracy);
    push(&m, e);
    return m;
}

static BattleMove charge_move(uint16_t power, uint8_t semi) {
    BattleMove m = damaging(power, T_NORMAL, 0);
    m.effects[0] = fx(BEFF_CHARGE, BWHO_USER, 0, semi, 0, 0);
    push(&m, DAMAGE1);
    return m;
}

static Action slot_move(uint8_t side, uint8_t source, uint8_t target, uint8_t slot) {
    Action a = move(side, source, target);
    a.move_slot = slot;
    return a;
}

static void test_volatile_wire(void) {
    static BattleRequest r, d, bad_rules;
    static uint8_t bytes[4096];
    make_volatiles(&r, BATTLE_FORMAT_DOUBLE);
    BattleMove fly = charge_move(90, BSEMI_AIR);
    fly.move_flags = BMF_IGNORES_SUBSTITUTE | BMF_DOUBLE_AIRBORNE;
    set_move(&r.player[0], 1, 19, fly, 15);
    const uint32_t n = battle_request_encode(&r, bytes, sizeof(bytes));
    assert(n && n == battle_request_size(&r) && battle_request_decode(bytes, n, &d));
    assert(d.volatiles.confusion_max == 5 && d.volatiles.substitute_divisor == 4 && d.volatiles.confusion_power == 40);
    const BattleMove *dm = &d.player[0].mon.moves[1];
    assert(dm->effects[0].kind == BEFF_CHARGE && dm->effects[0].arg0 == BSEMI_AIR && dm->effects[1].kind == BEFF_DAMAGE);
    assert(dm->move_flags == (BMF_IGNORES_SUBSTITUTE | BMF_DOUBLE_AIRBORNE));
    // Rules: reserved bytes and out-of-range values are refused, as is truncation.
    bytes[n - 1] = 1;
    assert(!battle_request_decode(bytes, n, &d));
    bytes[n - 1] = 0;
    bytes[n - 3] = 1;                                   // substitute divisor 1
    assert(!battle_request_decode(bytes, n, &d));
    bytes[n - 3] = 4;
    bytes[n - 6] = 6;                                   // confusion min 6 > max 5
    assert(!battle_request_decode(bytes, n, &d));
    bytes[n - 6] = 2;
    assert(battle_request_decode(bytes, n, &d));
    assert(!battle_request_decode(bytes, n - 1, &d));
    bad_rules = r;
    bad_rules.volatiles.confusion_hit_percent = 0;
    Battle b;
    assert(battle_init(&b, &bad_rules, 1) == BI_CONDITIONS);
    // Structure: CHARGE leads and is followed by DAMAGE; Substitute stands alone on a status move.
    BattleMove bad = damaging(120, T_NORMAL, 100);
    push(&bad, fx(BEFF_CHARGE, BWHO_USER, 0, BSEMI_NONE, 0, 0));
    assert(!battle_move_effects_valid(&bad));
    bad = effect_move(BTARGET_SELF, 0, fx(BEFF_SUBSTITUTE, BWHO_USER, 0, 0, 0, 0));
    assert(battle_move_effects_valid(&bad));
    push(&bad, fx(BEFF_STAT, BWHO_TARGET, 0, BSTAT_ATK, 1, 0));
    assert(!battle_move_effects_valid(&bad));
    bad = effect_move(BTARGET_SINGLE, 0, fx(BEFF_FLINCH, BWHO_TARGET, 0, 0, 0, 0));
    assert(!battle_move_effects_valid(&bad));           // flinch only rides on a hit
    // A V8 request cannot use V9 kinds or flags: such moves are not usable.
    make_effects(&r, BATTLE_FORMAT_SINGLE);
    BattleMove bite = damaging(60, T_NORMAL, 0);
    push(&bite, fx(BEFF_FLINCH, BWHO_TARGET, 30, 0, 0, 0));
    set_move(&r.player[0], 1, 44, bite, 25);
    BattleMove gust = damaging(40, T_NORMAL, 0);
    gust.move_flags = BMF_DOUBLE_AIRBORNE;
    set_move(&r.player[0], 2, 16, gust, 35);
    assert(battle_init(&b, &r, 1) == BI_OK);
    assert((battle_usable_moves(&b, battle_active(&b, 0)) & 6) == 0);
    // On the wire such a V8 request is malformed (audit P-03); a plain V8
    // request still encodes and decodes unchanged (no rules block).
    uint32_t v8 = battle_request_encode(&r, bytes, sizeof(bytes));
    assert(v8 == battle_request_size(&r) && !battle_request_decode(bytes, v8, &d));
    make_effects(&r, BATTLE_FORMAT_SINGLE);
    v8 = battle_request_encode(&r, bytes, sizeof(bytes));
    assert(v8 == battle_request_size(&r) && battle_request_decode(bytes, v8, &d) && d.version == BATTLE_REQUEST_VERSION_EFFECTS);
    // State: one volatile byte per position.
    BattleRosterState s = {.field_state = {.phase = BPHASE_CHOOSE, .token = 9},
                           .volatiles = {[0][1] = BVF_CHARGING | BVF_LOCKED, [1][0] = BVF_SUBSTITUTE}};
    BattleRosterState back;
    const unsigned len = battle_volatile_state_encode(&s, bytes);
    assert(len == BATTLE_VOLATILE_STATE_BYTES && len + APP_REQUEST_HEADER <= WIRE_PAYLOAD);
    assert(battle_volatile_state_decode(bytes, len, &back) && back.volatiles[0][1] == (BVF_CHARGING | BVF_LOCKED) &&
           back.volatiles[1][0] == BVF_SUBSTITUTE && back.field_state.token == 9);
    assert(!battle_volatile_state_decode(bytes, len - 1, &back));
    bytes[len - 1] = 0x80;                              // unknown flag
    assert(!battle_volatile_state_decode(bytes, len, &back));
}

static void test_flinch(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    make_volatiles(&r, BATTLE_FORMAT_SINGLE);       // the foe (70) is faster than the player (60)
    BattleMove bite = damaging(10, T_NORMAL, 0);
    push(&bite, fx(BEFF_FLINCH, BWHO_TARGET, 100, 0, 0, 0));
    set_move(&r.opponent[0], 0, 44, bite, 25);
    assert(battle_init(&b, &r, 1) == BI_OK);
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    assert(event_count(&log, EV_FLINCH) == 1 && event_count(&log, EV_MOVE_USED) == 1);
    assert(!battle_active(&b, 0)->vol.flinched && battle_active(&b, 0)->pp[0] == 20);   // cleared; no PP for a lost turn
    // A 1% roll rarely flinches.
    r.opponent[0].mon.moves[0].effects[1].chance = 1;
    unsigned flinches = 0;
    for (uint32_t seed = 1; seed <= 300; ++seed) {
        assert(battle_init(&b, &r, seed) == BI_OK);
        run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
        flinches += event_count(&log, EV_FLINCH);
    }
    assert(flinches < 12);
    // A slower user cannot flinch a target that already acted.
    make_volatiles(&r, BATTLE_FORMAT_SINGLE);
    set_move(&r.player[0], 0, 44, bite, 25);
    assert(battle_init(&b, &r, 1) == BI_OK);
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    assert(!event_count(&log, EV_FLINCH) && event_count(&log, EV_MOVE_USED) == 2);
    // Double: the spread hit flinches only the foe that has not moved yet.
    // Speeds: foe 1 (90) > player 1 (80) > foe 0 (70) > player 0 (60).
    make_volatiles(&r, BATTLE_FORMAT_DOUBLE);
    BattleMove slide = damaging(10, T_NORMAL, 0);
    slide.target = BTARGET_ALL_FOES;
    push(&slide, fx(BEFF_FLINCH, BWHO_TARGET, 100, 0, 0, 0));
    set_move(&r.player[1], 0, 157, slide, 10);
    assert(battle_init(&b, &r, 2) == BI_OK);
    const Action turn[] = {move(0, 0, 0), move(0, 1, 0), move(1, 0, 0), move(1, 1, 0)};
    battle_run_turn(&b, turn, 4, &log);
    const BattleEvent *f = find(&log, EV_FLINCH);
    assert(event_count(&log, EV_FLINCH) == 1 && f->user.side == 1 && f->user.slot == 0);
    assert(event_count(&log, EV_MOVE_USED) == 3);
}

static void test_confusion(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    make_volatiles(&r, BATTLE_FORMAT_SINGLE);
    set_move(&r.player[0], 1, 109, effect_move(BTARGET_SINGLE, 0, fx(BEFF_CONFUSE, BWHO_TARGET, 0, 0, 0, 0)), 10);
    const Action ray = slot_move(0, 0, 0, 1), tackle = move(1, 0, 0);
    unsigned hits = 0, counters[6] = {0};
    for (uint32_t seed = 1; seed <= 600; ++seed) {
        assert(battle_init(&b, &r, seed) == BI_OK);
        battle_run_turn(&b, &ray, 1, &log);
        const uint8_t counter = battle_active(&b, 1)->vol.confusion;
        assert(find(&log, EV_CONFUSE) && counter >= 2 && counter <= 5);
        ++counters[counter];
        battle_run_turn(&b, &ray, 1, &log);           // already confused: no effect
        assert(find(&log, EV_NO_EFFECT) && battle_active(&b, 1)->vol.confusion == counter);
        const uint8_t pp = battle_active(&b, 1)->pp[0];
        const uint16_t hp = battle_active(&b, 1)->hp;
        battle_run_turn(&b, &tackle, 1, &log);
        assert(find(&log, EV_CONFUSED) && battle_active(&b, 1)->vol.confusion == counter - 1);
        if (find(&log, EV_CONFUSION_HIT)) {
            ++hits;
            // The self-hit replaces the move: no PP, no move event.
            assert(battle_active(&b, 1)->pp[0] == pp && !find(&log, EV_MOVE_USED));
            // Level 50, power 40, 60/60: (22*40*60/60)/50 + 2 = 19.
            assert(find(&log, EV_CONFUSION_HIT)->amount == 19 && battle_active(&b, 1)->hp == hp - 19);
        } else {
            assert(battle_active(&b, 1)->pp[0] == pp - 1 && find(&log, EV_MOVE_USED));
        }
    }
    assert(hits > 150 && hits < 250);
    for (unsigned c = 2; c <= 5; ++c) assert(counters[c] > 100);
    // The counter runs out: the confusion ends and the Pokemon acts.
    assert(battle_init(&b, &r, 3) == BI_OK);
    battle_active(&b, 1)->vol.confusion = 1;
    battle_run_turn(&b, &tackle, 1, &log);
    assert(find(&log, EV_CONFUSION_END) && find(&log, EV_MOVE_USED) && !battle_active(&b, 1)->vol.confusion);
    // The self-hit ignores the user's own substitute and is halved by burn only under the rule.
    r.volatiles.confusion_hit_percent = 100;
    r.volatiles.flags |= BVR_CONFUSION_BURN;
    assert(battle_init(&b, &r, 3) == BI_OK);
    Battler *foe = battle_active(&b, 1);
    foe->vol.confusion = 5;
    foe->vol.substitute_hp = 50;
    foe->status = BATTLE_CONDITION(BST_BURN, 0);
    battle_run_turn(&b, &tackle, 1, &log);
    assert(find(&log, EV_CONFUSION_HIT)->amount == 9 && foe->vol.substitute_hp == 50);
    r.volatiles.flags &= (uint8_t)~BVR_CONFUSION_BURN;
    assert(battle_init(&b, &r, 3) == BI_OK);
    foe = battle_active(&b, 1);
    foe->vol.confusion = 5;
    foe->status = BATTLE_CONDITION(BST_BURN, 0);
    battle_run_turn(&b, &tackle, 1, &log);
    assert(find(&log, EV_CONFUSION_HIT)->amount == 19);
    // A switch clears it; Rotation keeps it on a wing.
    make_volatiles(&r, BATTLE_FORMAT_ROTATION);
    assert(battle_init(&b, &r, 3) == BI_OK);
    const uint8_t front = b.sides[0].active[BPOS_CENTER];
    b.sides[0].battlers[front].vol.confusion = 4;
    Action rotate_left = move(0, BPOS_CENTER, BPOS_CENTER);
    rotate_left.rotation = BROT_LEFT;
    battle_run_turn(&b, &rotate_left, 1, &log);
    assert(b.sides[0].active[BPOS_CENTER] != front && b.sides[0].battlers[front].vol.confusion == 4);
    make_volatiles(&r, BATTLE_FORMAT_SINGLE);
    add_reserve(&r, 0);
    assert(battle_init(&b, &r, 3) == BI_OK);
    battle_active(&b, 0)->vol.confusion = 4;
    const Action sw = {.kind = ACT_SWITCH, .user = {0, 0}, .reserve = 1};
    battle_run_turn(&b, &sw, 1, &log);
    assert(find(&log, EV_WITHDRAW) && b.sides[0].battlers[0].vol.confusion == 0);
}

static void test_substitute(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    make_volatiles(&r, BATTLE_FORMAT_SINGLE);
    set_move(&r.player[0], 1, 164, effect_move(BTARGET_SELF, 0, fx(BEFF_SUBSTITUTE, BWHO_USER, 0, 0, 0, 0)), 10);
    const Action sub = slot_move(0, 0, 0, 1);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, &sub, 1, &log);
    Battler *p = battle_active(&b, 0);
    assert(find(&log, EV_SUBSTITUTE)->amount == 50 && p->hp == 150 && p->vol.substitute_hp == 50);   // 200 / 4
    assert(battle_volatile_flags(&b, (Position){0, 0}) == BVF_SUBSTITUTE);
    battle_run_turn(&b, &sub, 1, &log);                      // only one at a time
    assert(find(&log, EV_FAIL) && p->hp == 150 && p->pp[1] == 8);
    p->vol.substitute_hp = 0;
    p->hp = 50;                                               // HP must exceed the cost
    battle_run_turn(&b, &sub, 1, &log);
    assert(find(&log, EV_FAIL) && p->hp == 50 && !p->vol.substitute_hp);

    // Damage goes to the substitute; status and stat moves fail against it
    // unless the move bypasses substitutes (sound moves in Gen 6+, as data).
    make_volatiles(&r, BATTLE_FORMAT_SINGLE);
    BattleMove growl = effect_move(BTARGET_SINGLE, 0, fx(BEFF_STAT, BWHO_TARGET, 0, BSTAT_ATK, -1, 0));
    set_move(&r.opponent[0], 1, 45, growl, 40);
    growl.move_flags = BMF_IGNORES_SUBSTITUTE;
    set_move(&r.opponent[0], 2, 48, growl, 40);
    BattleMove poison = effect_move(BTARGET_SINGLE, 0, fx(BEFF_STATUS, BWHO_TARGET, 0, BST_POISON, 0, 0));
    set_move(&r.opponent[0], 3, 77, poison, 35);
    set_move(&r.player[0], 2, 14, effect_move(BTARGET_SELF, 0, fx(BEFF_STAT, BWHO_TARGET, 0, BSTAT_ATK, 2, 0)), 20);
    assert(battle_init(&b, &r, 1) == BI_OK);
    p = battle_active(&b, 0);
    p->vol.substitute_hp = 30;
    battle_run_turn(&b, (Action[]){slot_move(1, 0, 0, 1)}, 1, &log);
    assert(find(&log, EV_FAIL) && p->stat_stages[BSTAT_ATK] == 0);
    battle_run_turn(&b, (Action[]){slot_move(1, 0, 0, 3)}, 1, &log);
    assert(find(&log, EV_FAIL) && !p->status);
    battle_run_turn(&b, (Action[]){slot_move(1, 0, 0, 2)}, 1, &log);
    assert(find(&log, EV_STAGE) && p->stat_stages[BSTAT_ATK] == -1);
    const uint16_t hp = p->hp;
    battle_run_turn(&b, (Action[]){move(1, 0, 0)}, 1, &log);
    const BattleEvent *taken = find(&log, EV_SUBSTITUTE_HIT);
    assert(taken && !find(&log, EV_DAMAGE) && p->hp == hp && p->vol.substitute_hp == 30 - taken->amount);
    // The user's own self-targeting moves are not blocked by its substitute.
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 2)}, 1, &log);
    assert(find(&log, EV_STAGE) && p->stat_stages[BSTAT_ATK] == 1);

    // Multi-hit: with the rule set, hits after the break strike the Pokemon;
    // without it the move stops at the break.
    make_volatiles(&r, BATTLE_FORMAT_SINGLE);
    BattleMove kicks = damaging(40, T_NORMAL, 0);
    kicks.effects[0] = fx(BEFF_DAMAGE, BWHO_TARGET, 0, 5, 0, 5);
    set_move(&r.opponent[0], 0, 24, kicks, 30);
    assert(battle_init(&b, &r, 4) == BI_OK);
    p = battle_active(&b, 0);
    p->vol.substitute_hp = 1;
    battle_run_turn(&b, (Action[]){move(1, 0, 0)}, 1, &log);
    assert(find(&log, EV_SUBSTITUTE_BREAK) && event_count(&log, EV_DAMAGE) == 4 && !p->vol.substitute_hp);
    assert(find(&log, EV_MULTI_HIT)->amount == 5);
    r.volatiles.flags = 0;
    assert(battle_init(&b, &r, 4) == BI_OK);
    battle_active(&b, 0)->vol.substitute_hp = 1;
    battle_run_turn(&b, (Action[]){move(1, 0, 0)}, 1, &log);
    assert(find(&log, EV_SUBSTITUTE_BREAK) && !event_count(&log, EV_DAMAGE) && find(&log, EV_MULTI_HIT)->amount == 1);

    // Drain and recoil count what the substitute lost; a flinch needs the Pokemon itself.
    make_volatiles(&r, BATTLE_FORMAT_SINGLE);
    BattleMove drain = damaging(60, T_NORMAL, 0);
    push(&drain, fx(BEFF_DRAIN, BWHO_USER, 0, 50, 0, 0));
    push(&drain, fx(BEFF_FLINCH, BWHO_TARGET, 100, 0, 0, 0));
    set_move(&r.player[0], 0, 202, drain, 10);
    BattleMove edge = damaging(60, T_NORMAL, 0);
    push(&edge, fx(BEFF_RECOIL, BWHO_USER, 0, 50, 0, 0));
    set_move(&r.player[0], 1, 38, edge, 10);
    r.player[0].record.speed = 200;                           // acts first: a flinch could land
    assert(battle_init(&b, &r, 5) == BI_OK);
    battle_active(&b, 0)->hp = 150;
    battle_active(&b, 1)->vol.substitute_hp = 4;
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    assert(find(&log, EV_SUBSTITUTE_BREAK) && find(&log, EV_DRAIN)->amount == 2);   // 50% of the 4 HP it had
    assert(!find(&log, EV_FLINCH) && event_count(&log, EV_MOVE_USED) == 2);
    battle_active(&b, 1)->vol.substitute_hp = 6;
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(find(&log, EV_RECOIL)->amount == 3 && !find(&log, EV_DAMAGE));

    // Dragon Tail does not drag a Pokemon whose substitute took the hit; U-turn still pivots,
    // and the switch clears the user's own substitute while the foe keeps its.
    make_volatiles(&r, BATTLE_FORMAT_SINGLE);
    add_reserve(&r, 0);
    add_reserve(&r, 1);
    BattleMove tail = damaging(10, T_NORMAL, 0), uturn = damaging(10, T_NORMAL, 0);
    push(&tail, fx(BEFF_FORCE_OUT, BWHO_TARGET, 0, 0, 0, 0));
    push(&uturn, fx(BEFF_PIVOT, BWHO_TARGET, 0, 0, 0, 0));
    set_move(&r.player[0], 0, 525, tail, 10);
    set_move(&r.player[0], 1, 369, uturn, 10);
    assert(battle_init(&b, &r, 6) == BI_OK);
    battle_active(&b, 1)->vol.substitute_hp = 50;
    battle_run_turn(&b, (Action[]){move(0, 0, 0)}, 1, &log);
    assert(find(&log, EV_SUBSTITUTE_HIT) && !find(&log, EV_DRAG_OUT));
    b.sides[0].battlers[0].vol.substitute_hp = 10;
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(find(&log, EV_SUBSTITUTE_HIT) && battle_pending_pivot(&b, NULL));
    assert(battle_resume_pivot(&b, 1, &log));
    assert(b.sides[0].battlers[0].vol.substitute_hp == 0 && battle_active(&b, 1)->vol.substitute_hp > 0);
}

static void test_charge(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    const Position me = {0, 0};
    // Solar Beam: turn 1 charges (PP spent once), turn 2 is forced and fires at the stored target.
    make_volatiles(&r, BATTLE_FORMAT_SINGLE);
    set_move(&r.player[0], 1, 76, charge_move(120, BSEMI_NONE), 10);
    const Action solar = slot_move(0, 0, 0, 1);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, &solar, 1, &log);
    Battler *p = battle_active(&b, 0);
    assert(find(&log, EV_CHARGE) && !find(&log, EV_DAMAGE) && p->pp[1] == 9);
    assert(battle_action_state(&b, me) == BAS_CHARGING);
    assert(battle_volatile_flags(&b, me) == (BVF_CHARGING | BVF_LOCKED));
    Action forced, chosen;
    assert(battle_forced_action(&b, me, &forced) && forced.kind == ACT_MOVE && forced.move_slot == 1);
    battle_choose_at(&b, me, &chosen);                        // the AI path returns the locked move too
    assert(chosen.kind == ACT_MOVE && chosen.move_slot == 1);
    battle_run_turn(&b, (Action[]){move(0, 0, 0)}, 1, &log);   // a different command is ignored
    assert(find(&log, EV_DAMAGE) && find(&log, EV_MOVE_USED)->move_id == 76 && p->pp[1] == 9 && p->pp[0] == 20);
    assert(battle_action_state(&b, me) == BAS_READY && !battle_forced_action(&b, me, &forced));
    // No command at all still runs the locked move.
    battle_run_turn(&b, &solar, 1, &log);
    battle_run_turn(&b, NULL, 0, &log);
    assert(find(&log, EV_DAMAGE) && battle_action_state(&b, me) == BAS_READY);
    // Interrupted on the release turn (flinch, sleep): the charge is lost, no PP refund.
    battle_run_turn(&b, &solar, 1, &log);
    p->vol.flinched = true;
    battle_run_turn(&b, &solar, 1, &log);
    assert(find(&log, EV_FLINCH) && find(&log, EV_CHARGE_END) && !find(&log, EV_DAMAGE));
    assert(battle_action_state(&b, me) == BAS_READY && p->pp[1] == 7);
    battle_run_turn(&b, &solar, 1, &log);
    p->status = BATTLE_CONDITION(BST_SLEEP, 3);
    battle_run_turn(&b, &solar, 1, &log);
    assert(find(&log, EV_CHARGE_END) && battle_action_state(&b, me) == BAS_READY);

    // Fly: out of reach except for moves flagged for the air (Gust doubles).
    make_volatiles(&r, BATTLE_FORMAT_SINGLE);
    set_move(&r.player[0], 1, 19, charge_move(90, BSEMI_AIR), 15);
    BattleMove gust = damaging(40, T_NORMAL, 0);
    gust.move_flags = BMF_DOUBLE_AIRBORNE;
    set_move(&r.opponent[0], 1, 16, gust, 35);
    r.opponent[0].record.speed = 1;
    const Action f = slot_move(0, 0, 0, 1), g = slot_move(1, 0, 0, 1);
    assert(battle_init(&b, &r, 2) == BI_OK);
    run1(&b, f, move(1, 0, 0), &log);
    assert(find(&log, EV_CHARGE)->condition == BSEMI_AIR && find(&log, EV_MISS) && battle_active(&b, 0)->hp == 200);
    assert(battle_action_state(&b, me) == BAS_SEMI_INVULNERABLE);
    assert(battle_volatile_flags(&b, me) == (BVF_CHARGING | BVF_SEMI_INVULNERABLE | BVF_LOCKED));
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);             // lands, then Tackle hits normally
    assert(event_count(&log, EV_DAMAGE) == 2 && battle_action_state(&b, me) == BAS_READY);
    assert(battle_init(&b, &r, 2) == BI_OK);
    run1(&b, f, g, &log);
    const BattleEvent *hit_in_air = find(&log, EV_DAMAGE);
    uint8_t eff;
    const Battler *foe = battle_active(&b, 1), *mon = battle_active(&b, 0);
    assert(hit_in_air && hit_in_air->target.side == 0);
    assert(hit_in_air->amount >= 2 * battle_damage(&b, foe, &gust, mon, 85, &eff) &&
           hit_in_air->amount <= 2 * battle_damage(&b, foe, &gust, mon, 100, &eff));
    // Dig: Earthquake-style moves reach and double; Gust does not reach underground.
    make_volatiles(&r, BATTLE_FORMAT_SINGLE);
    BattleMove quake = damaging(100, T_NORMAL, 0);
    quake.move_flags = BMF_DOUBLE_UNDERGROUND;
    set_move(&r.player[0], 1, 91, charge_move(80, BSEMI_UNDERGROUND), 10);
    set_move(&r.opponent[0], 1, 89, quake, 10);
    set_move(&r.opponent[0], 2, 16, gust, 35);
    r.opponent[0].record.speed = 1;
    assert(battle_init(&b, &r, 3) == BI_OK);
    run1(&b, slot_move(0, 0, 0, 1), slot_move(1, 0, 0, 2), &log);
    assert(find(&log, EV_MISS) && battle_action_state(&b, me) == BAS_SEMI_INVULNERABLE);
    assert(battle_init(&b, &r, 3) == BI_OK);
    run1(&b, slot_move(0, 0, 0, 1), slot_move(1, 0, 0, 1), &log);
    assert(find(&log, EV_DAMAGE) && find(&log, EV_DAMAGE)->target.side == 0);
    // A Poison type's Toxic is a sure hit and reaches a semi-invulnerable target (rule).
    BattleMove toxic = effect_move(BTARGET_SINGLE, 90, fx(BEFF_STATUS, BWHO_TARGET, 0, BST_TOXIC, 0, 0));
    set_move(&r.opponent[0], 3, 92, toxic, 10);
    r.opponent[0].mon.types[0] = r.opponent[0].mon.types[1] = T_GHOST;   // poison_type in these rules
    assert(battle_init(&b, &r, 3) == BI_OK);
    run1(&b, slot_move(0, 0, 0, 1), slot_move(1, 0, 0, 3), &log);
    assert(find(&log, EV_STATUS) && BATTLE_CONDITION_KIND(battle_active(&b, 0)->status) == BST_TOXIC);
    r.opponent[0].mon.types[0] = r.opponent[0].mon.types[1] = T_NORMAL;
    assert(battle_init(&b, &r, 3) == BI_OK);
    run1(&b, slot_move(0, 0, 0, 1), slot_move(1, 0, 0, 3), &log);
    assert(find(&log, EV_MISS) && !battle_active(&b, 0)->status);

    // Double: the stored target fainted meanwhile -> the other foe is hit.
    make_volatiles(&r, BATTLE_FORMAT_DOUBLE);
    set_move(&r.player[0], 1, 76, charge_move(120, BSEMI_NONE), 10);
    assert(battle_init(&b, &r, 4) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(battle_action_state(&b, me) == BAS_CHARGING);
    set_fainted(&b, 1, b.sides[1].active[0]);
    battle_run_turn(&b, NULL, 0, &log);
    const BattleEvent *used = find(&log, EV_MOVE_USED), *dmg = find(&log, EV_DAMAGE);
    assert(used && used->move_id == 76 && dmg && dmg->target.side == 1 && dmg->target.slot == 1);

    // Triple: a locked position ignores its command and the others still act.
    make_volatiles(&r, BATTLE_FORMAT_TRIPLE);
    set_move(&r.player[1], 1, 19, charge_move(90, BSEMI_AIR), 15);
    assert(battle_init(&b, &r, 5) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 1, 1, 1)}, 1, &log);
    assert(battle_action_state(&b, (Position){0, 1}) == BAS_SEMI_INVULNERABLE);
    const Action triple[] = {move(0, 0, 0), move(0, 1, 2), move(0, 2, 2)};
    battle_run_turn(&b, triple, 3, &log);
    unsigned center_moves = 0;
    for (unsigned i = 0; i < log.count; ++i)
        if (log.events[i].kind == EV_MOVE_USED && log.events[i].user.side == 0 && log.events[i].user.slot == 1) {
            ++center_moves;
            assert(log.events[i].move_id == 19);
        }
    assert(center_moves == 1 && event_count(&log, EV_MOVE_USED) == 3);
}

static void test_recharge(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    const Position me = {0, 0};
    make_volatiles(&r, BATTLE_FORMAT_SINGLE);
    r.opponent[0].record.hp = r.opponent[0].record.max_hp = 999;
    BattleMove beam = damaging(150, T_NORMAL, 0);
    push(&beam, fx(BEFF_RECHARGE, BWHO_USER, 0, 0, 0, 0));
    set_move(&r.player[0], 1, 63, beam, 5);
    const Action hb = slot_move(0, 0, 0, 1);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, &hb, 1, &log);
    assert(find(&log, EV_DAMAGE) && battle_action_state(&b, me) == BAS_RECHARGING);
    assert(battle_volatile_flags(&b, me) == (BVF_RECHARGING | BVF_LOCKED));
    Action forced;
    assert(battle_forced_action(&b, me, &forced) && forced.kind == ACT_RECHARGE);
    battle_run_turn(&b, &hb, 1, &log);                          // forced recharge, no PP
    assert(find(&log, EV_RECHARGE) && !find(&log, EV_MOVE_USED) && battle_active(&b, 0)->pp[1] == 4);
    assert(battle_action_state(&b, me) == BAS_READY);
    battle_run_turn(&b, &hb, 1, &log);
    assert(find(&log, EV_DAMAGE));
    // The recharge turn comes first: a flinch on it is not reported on top.
    battle_active(&b, 0)->vol.flinched = true;
    battle_run_turn(&b, NULL, 0, &log);
    assert(find(&log, EV_RECHARGE) && !find(&log, EV_FLINCH) && battle_action_state(&b, me) == BAS_READY);
    // A miss means no recharge.
    r.player[0].mon.moves[1].accuracy = 1;
    bool missed = false;
    for (uint32_t seed = 1; seed <= 20 && !missed; ++seed) {
        assert(battle_init(&b, &r, seed) == BI_OK);
        battle_run_turn(&b, &hb, 1, &log);
        missed = find(&log, EV_MISS) != NULL;
    }
    assert(missed && battle_action_state(&b, me) == BAS_READY);
    // Knocking the target out: recharge by default, none under the Gen 1 rule.
    r.player[0].mon.moves[1].accuracy = 0;
    r.opponent[0].record.hp = 1;
    add_reserve(&r, 1);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, &hb, 1, &log);
    assert(find(&log, EV_FAINT) && battle_active(&b, 0)->vol.recharge);
    r.volatiles.flags |= BVR_RECHARGE_SKIP_ON_KO;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, &hb, 1, &log);
    assert(find(&log, EV_FAINT) && !battle_active(&b, 0)->vol.recharge);
    // Rotation: a recharging front cannot rotate; the rotation request is ignored.
    make_volatiles(&r, BATTLE_FORMAT_ROTATION);
    assert(battle_init(&b, &r, 1) == BI_OK);
    const uint8_t front = b.sides[0].active[BPOS_CENTER];
    b.sides[0].battlers[front].vol.recharge = 1;
    Action rot = move(0, BPOS_CENTER, BPOS_CENTER);
    rot.rotation = BROT_LEFT;
    battle_run_turn(&b, &rot, 1, &log);
    assert(find(&log, EV_RECHARGE) && !find(&log, EV_ROTATE) && b.sides[0].active[BPOS_CENTER] == front);
    battle_run_turn(&b, &rot, 1, &log);
    assert(find(&log, EV_ROTATE) && b.sides[0].active[BPOS_CENTER] != front);
}

static void test_volatile_lifecycle(void) {
    static BattleRequest r;
    Battle b;
    EventLog log;
    // Triple Shift keeps volatiles; faint clears them; a fresh init (reconnect) starts clean.
    make_volatiles(&r, BATTLE_FORMAT_TRIPLE);
    assert(battle_init(&b, &r, 1) == BI_OK);
    Battler *left = (Battler *)battle_at(&b, (Position){0, BPOS_LEFT});
    left->vol.confusion = 3;
    left->vol.substitute_hp = 20;
    battle_run_turn(&b, (Action[]){{.kind = ACT_SHIFT, .user = {0, BPOS_LEFT}}}, 1, &log);
    assert(battle_at(&b, (Position){0, BPOS_CENTER}) == left && left->vol.substitute_hp == 20 && left->vol.confusion == 3);
    left->vol.substitute_hp = 0;
    left->hp = 1;
    battle_run_turn(&b, (Action[]){move(1, BPOS_CENTER, BPOS_CENTER)}, 1, &log);
    assert(left->fainted && !left->vol.confusion && left->vol.charge_slot == BATTLE_SLOT_NONE);
    assert(battle_init(&b, &r, 1) == BI_OK);
    for (unsigned side = 0; side < 2; ++side)
        for (unsigned i = 0; i < b.sides[side].count; ++i) {
            const BattlerVolatiles *v = &b.sides[side].battlers[i].vol;
            assert(!v->confusion && !v->substitute_hp && !v->recharge && !v->flinched &&
                   v->charge_slot == BATTLE_SLOT_NONE && !v->semi_invulnerable);
        }
    // Forfeit while locked ends the battle cleanly.
    make_volatiles(&r, BATTLE_FORMAT_SINGLE);
    set_move(&r.player[0], 1, 76, charge_move(120, BSEMI_NONE), 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    battle_run_turn(&b, (Action[]){{.kind = ACT_FORFEIT}}, 1, &log);
    assert(b.outcome == OUTCOME_CANCELLED);
    // Same seed and inputs, same log, across 25 volatile-heavy turns.
    make_volatiles(&r, BATTLE_FORMAT_DOUBLE);
    for (unsigned i = 0; i < 2; ++i) {
        r.player[i].record.hp = r.player[i].record.max_hp = 900;
        r.opponent[i].record.hp = r.opponent[i].record.max_hp = 900;
    }
    BattleMove hurricane = damaging(30, T_NORMAL, 70);
    push(&hurricane, fx(BEFF_CONFUSE, BWHO_TARGET, 30, 0, 0, 0));
    push(&hurricane, fx(BEFF_FLINCH, BWHO_TARGET, 30, 0, 0, 0));
    set_move(&r.player[0], 0, 542, hurricane, 40);
    set_move(&r.opponent[0], 0, 542, hurricane, 40);
    set_move(&r.player[1], 1, 19, charge_move(30, BSEMI_AIR), 40);
    BattleMove beam = damaging(30, T_NORMAL, 0);
    push(&beam, fx(BEFF_RECHARGE, BWHO_USER, 0, 0, 0, 0));
    set_move(&r.opponent[1], 1, 63, beam, 40);
    set_move(&r.opponent[1], 2, 164, effect_move(BTARGET_SELF, 0, fx(BEFF_SUBSTITUTE, BWHO_USER, 0, 0, 0, 0)), 40);
    static EventLog first[25], again[25];
    const Action turn[] = {move(0, 0, 1), slot_move(0, 1, 0, 1), move(1, 0, 0), slot_move(1, 1, 1, 1)};
    const Action turn2[] = {move(0, 0, 1), move(0, 1, 1), move(1, 0, 1), slot_move(1, 1, 1, 2)};
    unsigned played = 0;
    for (unsigned pass = 0; pass < 2; ++pass) {
        assert(battle_init(&b, &r, 91) == BI_OK);
        unsigned t = 0;
        for (; t < 25 && !b.outcome; ++t) battle_run_turn(&b, t % 2 ? turn2 : turn, 4, pass ? &again[t] : &first[t]);
        if (!pass) played = t;
        else assert(t == played);
    }
    assert(played >= 20);
    unsigned kinds[EV_RECHARGE + 1] = {0};
    for (unsigned t = 0; t < played; ++t) {
        assert(first[t].count == again[t].count);
        for (unsigned i = 0; i < first[t].count; ++i) {
            const BattleEvent *x = &first[t].events[i], *y = &again[t].events[i];
            assert(x->kind == y->kind && x->amount == y->amount && x->hp_after == y->hp_after &&
                   x->target.side == y->target.side && x->target.slot == y->target.slot);
            if (x->kind <= EV_RECHARGE) ++kinds[x->kind];
        }
    }
    assert(kinds[EV_CONFUSE] && kinds[EV_FLINCH] && kinds[EV_CHARGE] && kinds[EV_RECHARGE] && kinds[EV_SUBSTITUTE]);
}

// ---------------------------------------------------------------- V10 effect application units (audit B-01)

static void make_timing(BattleRequest *r, uint8_t format) {
    make_volatiles(r, format);
    r->version = BATTLE_REQUEST_VERSION_TIMING;
}

// Clanging Scales / Scale Noise: hits all foes, then the user's Defense -1 once.
static BattleMove scale_noise(uint8_t timing) {
    BattleMove m = damaging(20, T_NORMAL, 0);
    m.target = BTARGET_ALL_FOES;
    BattleEffect drop = fx(BEFF_STAT, BWHO_USER, 0, BSTAT_DEF, -1, 0);
    drop.flags = BEFL_TIMING(timing);
    push(&m, drop);
    return m;
}

static unsigned index_of(const EventLog *log, EventKind kind, unsigned nth) {
    for (unsigned i = 0; i < log->count; ++i)
        if (log->events[i].kind == kind && !nth--) return i;
    return log->count;
}

static void test_effect_timing_spread_self(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    const Action noise = slot_move(0, 0, 0, 1);
    // Double, both foes hit: one drop, after both hits.
    make_timing(&r, BATTLE_FORMAT_DOUBLE);
    set_move(&r.player[0], 1, 789, scale_noise(BETIME_AFTER_MOVE), 5);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, &noise, 1, &log);
    assert(event_count(&log, EV_DAMAGE) == 2 && event_count(&log, EV_STAGE) == 1);
    assert(battle_active(&b, 0)->stat_stages[BSTAT_DEF] == -1);
    assert(index_of(&log, EV_STAGE, 0) > index_of(&log, EV_DAMAGE, 1));
    // One foe left standing: one hit, one drop.
    assert(battle_init(&b, &r, 1) == BI_OK);
    set_fainted(&b, 1, b.sides[1].active[1]);
    battle_run_turn(&b, &noise, 1, &log);
    assert(event_count(&log, EV_DAMAGE) == 1 && event_count(&log, EV_STAGE) == 1);
    // One foe protected, the other immune (Ghost): nothing landed, no drop.
    BattleMove guard = status_move(BTARGET_SELF, 0);
    guard.priority = 4;
    push(&guard, fx(BEFF_PROTECT, BWHO_USER, 0, 3, 0, 0));
    set_move(&r.opponent[0], 1, 182, guard, 10);
    r.opponent[1].mon.types[0] = r.opponent[1].mon.types[1] = T_GHOST;
    assert(battle_init(&b, &r, 1) == BI_OK);
    const Action both[] = {noise, slot_move(1, 0, 0, 1)};
    battle_run_turn(&b, both, 2, &log);
    assert(find(&log, EV_PROTECTED) && find(&log, EV_NO_EFFECT) && !find(&log, EV_STAGE));
    assert(battle_active(&b, 0)->stat_stages[BSTAT_DEF] == 0);
    // Protect on one foe, the other hit: one drop.
    r.opponent[1].mon.types[0] = r.opponent[1].mon.types[1] = T_NORMAL;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, both, 2, &log);
    assert(find(&log, EV_PROTECTED) && event_count(&log, EV_DAMAGE) == 1 && event_count(&log, EV_STAGE) == 1);
    // A substitute taking one hit still counts as landing.
    make_timing(&r, BATTLE_FORMAT_DOUBLE);
    set_move(&r.player[0], 1, 789, scale_noise(BETIME_AFTER_MOVE), 5);
    assert(battle_init(&b, &r, 1) == BI_OK);
    ((Battler *)battle_at(&b, (Position){1, 0}))->vol.substitute_hp = 99;
    battle_run_turn(&b, &noise, 1, &log);
    assert(find(&log, EV_SUBSTITUTE_HIT) && event_count(&log, EV_DAMAGE) == 1 && event_count(&log, EV_STAGE) == 1);

    // Triple: the center reaches all three foes; still one drop.
    make_timing(&r, BATTLE_FORMAT_TRIPLE);
    set_move(&r.player[1], 1, 789, scale_noise(BETIME_AFTER_MOVE), 5);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 1, 1, 1)}, 1, &log);
    assert(event_count(&log, EV_DAMAGE) == 3 && event_count(&log, EV_STAGE) == 1);
    assert(battle_at(&b, (Position){0, 1})->stat_stages[BSTAT_DEF] == -1);

    // V9 keeps the per-target behaviour (two drops); timing bits need V10.
    make_volatiles(&r, BATTLE_FORMAT_DOUBLE);
    set_move(&r.player[0], 1, 789, scale_noise(BETIME_DEFAULT), 5);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, &noise, 1, &log);
    assert(event_count(&log, EV_STAGE) == 2 && battle_active(&b, 0)->stat_stages[BSTAT_DEF] == -2);
    set_move(&r.player[0], 1, 789, scale_noise(BETIME_AFTER_MOVE), 5);
    assert(battle_init(&b, &r, 1) == BI_OK && !(battle_usable_moves(&b, battle_active(&b, 0)) & 2));
    static uint8_t bytes[4096];
    static BattleRequest d;
    make_timing(&r, BATTLE_FORMAT_DOUBLE);
    set_move(&r.player[0], 1, 789, scale_noise(BETIME_AFTER_MOVE), 5);
    uint32_t n = battle_request_encode(&r, bytes, sizeof(bytes));
    assert(n && battle_request_decode(bytes, n, &d));
    assert(BATTLE_EFFECT_TIMING(&d.player[0].mon.moves[1].effects[1]) == BETIME_AFTER_MOVE);
    bytes[0] = BATTLE_REQUEST_VERSION_VOLATILES;                     // the same list as V9: refused
    assert(!battle_request_decode(bytes, n, &d));
}

static void test_effect_timing_units(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    // Structure: timing only on secondaries of a hit; before/after-move timings only on the user.
    BattleMove m = scale_noise(BETIME_AFTER_MOVE);
    assert(battle_move_effects_valid(&m));
    m.effects[1].who = BWHO_TARGET;
    assert(!battle_move_effects_valid(&m));
    m = scale_noise(BETIME_COUNT);
    assert(!battle_move_effects_valid(&m));
    m = scale_noise(BETIME_PER_HIT);
    m.effects[0].flags = BEFL_TIMING(BETIME_PER_HIT);              // DAMAGE has fixed handling
    assert(!battle_move_effects_valid(&m));
    BattleMove growl = effect_move(BTARGET_SINGLE, 0, fx(BEFF_STAT, BWHO_TARGET, 0, BSTAT_ATK, -1, 0));
    growl.effects[0].flags = BEFL_TIMING(BETIME_PER_TARGET);
    assert(!battle_move_effects_valid(&growl));                   // status moves stay per target
    BattleMove drain = damaging(40, T_NORMAL, 0);
    push(&drain, fx(BEFF_DRAIN, BWHO_USER, 0, 50, 0, 0));
    drain.effects[1].flags = BEFL_TIMING(BETIME_AFTER_MOVE);
    assert(!battle_move_effects_valid(&drain));

    // Per hit vs per target on a two-strike move (Defense -1 on the target, 100%).
    make_timing(&r, BATTLE_FORMAT_SINGLE);
    BattleMove kick = damaging(10, T_NORMAL, 0);
    kick.effects[0] = fx(BEFF_DAMAGE, BWHO_TARGET, 0, 2, 0, 2);
    BattleEffect lower = fx(BEFF_STAT, BWHO_TARGET, 100, BSTAT_DEF, -1, 0);
    lower.flags = BEFL_TIMING(BETIME_PER_HIT);
    push(&kick, lower);
    set_move(&r.player[0], 1, 24, kick, 30);
    const Action k = slot_move(0, 0, 0, 1);
    assert(battle_init(&b, &r, 2) == BI_OK);
    battle_run_turn(&b, &k, 1, &log);
    assert(event_count(&log, EV_DAMAGE) == 2 && battle_active(&b, 1)->stat_stages[BSTAT_DEF] == -2);
    assert(index_of(&log, EV_STAGE, 0) < index_of(&log, EV_DAMAGE, 1));   // between the hits
    r.player[0].mon.moves[1].effects[1].flags = BEFL_TIMING(BETIME_PER_TARGET);
    assert(battle_init(&b, &r, 2) == BI_OK);
    battle_run_turn(&b, &k, 1, &log);
    assert(event_count(&log, EV_DAMAGE) == 2 && battle_active(&b, 1)->stat_stages[BSTAT_DEF] == -1);
    assert(index_of(&log, EV_STAGE, 0) > index_of(&log, EV_DAMAGE, 1));
    // Per-hit effects only when the Pokemon itself is struck (not its substitute).
    r.player[0].mon.moves[1].effects[1].flags = BEFL_TIMING(BETIME_PER_HIT);
    r.volatiles.flags = 0;                                        // the move stops at the break
    assert(battle_init(&b, &r, 2) == BI_OK);
    battle_active(&b, 1)->vol.substitute_hp = 50;
    battle_run_turn(&b, &k, 1, &log);
    assert(!find(&log, EV_STAGE) && battle_active(&b, 1)->stat_stages[BSTAT_DEF] == 0);

    // On damage vs per target for a user effect: hitting only a substitute.
    make_timing(&r, BATTLE_FORMAT_SINGLE);
    BattleMove charge = damaging(10, T_NORMAL, 0);
    BattleEffect boost = fx(BEFF_STAT, BWHO_USER, 0, BSTAT_SPA, 1, 0);
    boost.flags = BEFL_TIMING(BETIME_ON_DAMAGE);
    push(&charge, boost);
    set_move(&r.player[0], 1, 451, charge, 10);
    assert(battle_init(&b, &r, 3) == BI_OK);
    battle_active(&b, 1)->vol.substitute_hp = 50;
    battle_run_turn(&b, &k, 1, &log);
    assert(find(&log, EV_SUBSTITUTE_HIT) && !find(&log, EV_STAGE));
    battle_active(&b, 1)->vol.substitute_hp = 0;
    battle_run_turn(&b, &k, 1, &log);
    assert(find(&log, EV_DAMAGE) && battle_active(&b, 0)->stat_stages[BSTAT_SPA] == 1);
    r.player[0].mon.moves[1].effects[1].flags = BEFL_TIMING(BETIME_PER_TARGET);
    assert(battle_init(&b, &r, 3) == BI_OK);
    battle_active(&b, 1)->vol.substitute_hp = 50;
    battle_run_turn(&b, &k, 1, &log);
    assert(find(&log, EV_SUBSTITUTE_HIT) && battle_active(&b, 0)->stat_stages[BSTAT_SPA] == 1);

    // Per use: once, as soon as the first target is hit (before the second one).
    make_timing(&r, BATTLE_FORMAT_DOUBLE);
    BattleMove rush = scale_noise(BETIME_PER_USE);
    rush.effects[1] = fx(BEFF_STAT, BWHO_USER, 0, BSTAT_SPE, 1, 0);
    rush.effects[1].flags = BEFL_TIMING(BETIME_PER_USE);
    set_move(&r.player[0], 1, 7, rush, 10);
    assert(battle_init(&b, &r, 4) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(event_count(&log, EV_STAGE) == 1 && battle_active(&b, 0)->stat_stages[BSTAT_SPE] == 1);
    assert(index_of(&log, EV_STAGE, 0) > index_of(&log, EV_DAMAGE, 0) &&
           index_of(&log, EV_STAGE, 0) < index_of(&log, EV_DAMAGE, 1));

    // Drain, recoil and a target secondary next to an after-move user effect.
    make_timing(&r, BATTLE_FORMAT_DOUBLE);
    BattleMove mix = scale_noise(BETIME_AFTER_MOVE);
    push(&mix, fx(BEFF_DRAIN, BWHO_USER, 0, 50, 0, 0));
    push(&mix, fx(BEFF_STATUS, BWHO_TARGET, 100, BST_BURN, 0, 0));
    set_move(&r.player[0], 1, 1, mix, 10);
    assert(battle_init(&b, &r, 5) == BI_OK);
    battle_active(&b, 0)->hp = 50;
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(event_count(&log, EV_DRAIN) == 2 && event_count(&log, EV_STATUS) == 2 && event_count(&log, EV_STAGE) == 1);
    BattleMove edge = scale_noise(BETIME_AFTER_MOVE);
    push(&edge, fx(BEFF_RECOIL, BWHO_USER, 0, 25, 0, 0));
    set_move(&r.player[0], 2, 38, edge, 10);
    assert(battle_init(&b, &r, 5) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 2)}, 1, &log);
    assert(event_count(&log, EV_RECOIL) == 1 && event_count(&log, EV_STAGE) == 1);
}

// ---------------------------------------------------------------- Struggle through the shared hit checks (audit B-02)

static void test_struggle_shared_checks(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    const Action struggle = {.kind = ACT_STRUGGLE, .user = {0, 0}};
    make_volatiles(&r, BATTLE_FORMAT_SINGLE);    // the foe (speed 70) acts before the player (60)
    r.player[0].record.pp[0] = 0;
    BattleMove guard = status_move(BTARGET_SELF, 0);
    guard.priority = 4;
    push(&guard, fx(BEFF_PROTECT, BWHO_USER, 0, 3, 0, 0));
    set_move(&r.opponent[0], 1, 182, guard, 10);
    set_move(&r.opponent[0], 2, 19, charge_move(90, BSEMI_AIR), 15);
    set_move(&r.opponent[0], 3, 164, effect_move(BTARGET_SELF, 0, fx(BEFF_SUBSTITUTE, BWHO_USER, 0, 0, 0, 0)), 10);
    // Protect blocks Struggle: no damage and no recoil (the audit's case: 200 -> 179 HP before).
    assert(battle_init(&b, &r, 1) == BI_OK && battle_validate_action(&b, &struggle));
    battle_run_turn(&b, (Action[]){struggle, slot_move(1, 0, 0, 1)}, 2, &log);
    assert(find(&log, EV_PROTECT) && find(&log, EV_PROTECTED) && !find(&log, EV_DAMAGE) && !find(&log, EV_RECOIL));
    assert(battle_active(&b, 0)->hp == 200 && battle_active(&b, 1)->hp == 200);
    assert(find(&log, EV_MOVE_USED)->move_id == 182);
    // A Pokemon in the air is out of reach: a miss, no recoil.
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){struggle, slot_move(1, 0, 0, 2)}, 2, &log);
    assert(find(&log, EV_CHARGE) && find(&log, EV_MISS) && !find(&log, EV_RECOIL) && battle_active(&b, 0)->hp == 200);
    // A substitute takes the hit; the recoil still follows (max HP / 4, nearest).
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){struggle, slot_move(1, 0, 0, 3)}, 2, &log);
    assert(find(&log, EV_SUBSTITUTE) && find(&log, EV_SUBSTITUTE_HIT) && !find(&log, EV_DAMAGE));
    assert(find(&log, EV_RECOIL)->amount == 50 && battle_active(&b, 0)->hp == 150 && battle_active(&b, 1)->hp == 150);
    // Still typeless (hits a Ghost type, neutral), no PP, recoil after a normal hit.
    r.opponent[0].mon.types[0] = r.opponent[0].mon.types[1] = T_GHOST;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, &struggle, 1, &log);
    const BattleEvent *hit_event = find(&log, EV_DAMAGE);
    assert(hit_event && hit_event->effectiveness == 4 && find(&log, EV_RECOIL)->amount == 50);
    assert(battle_active(&b, 0)->pp[0] == 0);
    // Double: the random target choice is kept; a protected foe still blocks it.
    make_volatiles(&r, BATTLE_FORMAT_DOUBLE);
    r.player[0].record.pp[0] = 0;
    set_move(&r.opponent[0], 1, 182, guard, 10);
    set_move(&r.opponent[1], 1, 182, guard, 10);
    unsigned blocked = 0, left = 0;
    for (uint32_t seed = 1; seed <= 60; ++seed) {
        assert(battle_init(&b, &r, seed) == BI_OK);
        const Action turn[] = {struggle, slot_move(1, 0, 0, 1)};
        battle_run_turn(&b, turn, 2, &log);
        const BattleEvent *used = NULL;
        for (unsigned i = 0; i < log.count; ++i)
            if (log.events[i].kind == EV_MOVE_USED && log.events[i].move_id == BATTLE_MOVE_STRUGGLE) used = &log.events[i];
        assert(used);
        left += used->target.slot == 0;
        if (used->target.slot == 0) {
            assert(find(&log, EV_PROTECTED) && !find(&log, EV_RECOIL));
            ++blocked;
        } else {
            assert(find(&log, EV_DAMAGE) && find(&log, EV_RECOIL));
        }
    }
    assert(left > 10 && left < 50 && blocked == left);
}

// ---------------------------------------------------------------- refused turns change nothing (audit B-03)

static void test_refused_turn_keeps_state(void) {
    static BattleRequest r;
    static Battle b, before;
    EventLog log;
    make_volatiles(&r, BATTLE_FORMAT_DOUBLE);
    add_reserve(&r, 0);
    BattleMove guard = status_move(BTARGET_SELF, 0);
    guard.priority = 4;
    push(&guard, fx(BEFF_PROTECT, BWHO_USER, 0, 3, 0, 0));
    set_move(&r.player[0], 1, 182, guard, 10);
    BattleMove uturn = damaging(10, T_NORMAL, 0);
    push(&uturn, fx(BEFF_PIVOT, BWHO_TARGET, 0, 0, 0, 0));
    set_move(&r.player[1], 1, 369, uturn, 20);
    assert(battle_init(&b, &r, 3) == BI_OK);
    const Action turn[] = {slot_move(0, 0, 0, 1), slot_move(0, 1, 0, 1), move(1, 0, 0), move(1, 1, 1)};
    battle_run_turn(&b, turn, 4, &log);
    Position pivot;
    assert(battle_pending_pivot(&b, &pivot) && pivot.slot == 1);
    assert(battle_active(&b, 0)->vol.protected_turn && battle_active(&b, 0)->vol.acted);
    before = b;
    battle_run_turn(&b, NULL, 0, &log);                    // the audit's call: refused
    assert(!log.count && !memcmp(&before, &b, sizeof(b)));
    battle_run_turn(&b, turn, 4, &log);                    // a full turn is refused as well
    assert(!log.count && !memcmp(&before, &b, sizeof(b)));
    assert(battle_resume_pivot(&b, 2, &log));
    assert(find(&log, EV_WITHDRAW) && find(&log, EV_SEND_OUT));
    // The rest of the turn ran with Protect still up: the slower foe's attack on slot 0 is blocked.
    assert(find(&log, EV_PROTECTED) && find(&log, EV_PROTECTED)->target.slot == 0);
    // Pending replacement after a faint: refused turns keep the state too.
    make_volatiles(&r, BATTLE_FORMAT_SINGLE);
    add_reserve(&r, 0);
    r.player[0].record.hp = 1;
    assert(battle_init(&b, &r, 3) == BI_OK);
    battle_active(&b, 0)->vol.confusion = 3;
    battle_run_turn(&b, (Action[]){move(1, 0, 0)}, 1, &log);
    Position replace;
    assert(find(&log, EV_FAINT) && battle_pending_replacement(&b, &replace));
    before = b;
    battle_run_turn(&b, (Action[]){move(1, 0, 0)}, 1, &log);
    assert(!log.count && !memcmp(&before, &b, sizeof(b)));
}

// ---------------------------------------------------------------- automatic targets ignore the raw field (audit B-04)

static void test_auto_target_normalized(void) {
    BattleRequest r;
    Battle b;
    EventLog log;
    make_volatiles(&r, BATTLE_FORMAT_DOUBLE);
    BattleMove growl = effect_move(BTARGET_ALL_FOES, 0, fx(BEFF_STAT, BWHO_TARGET, 0, BSTAT_ATK, -1, 0));
    set_move(&r.player[0], 1, 45, growl, 40);
    set_move(&r.player[0], 2, 14, effect_move(BTARGET_SELF, 0, fx(BEFF_STAT, BWHO_TARGET, 0, BSTAT_ATK, 2, 0)), 20);
    assert(battle_init(&b, &r, 1) == BI_OK);
    // Core API caller (AI, replay) leaves garbage in the target of an automatic-target move.
    Action g = {.kind = ACT_MOVE, .user = {0, 0}, .move_slot = 1, .target = {{7, 200}}};
    assert(battle_validate_action(&b, &g));
    battle_run_turn(&b, &g, 1, &log);
    assert(event_count(&log, EV_STAGE) == 2);
    for (unsigned i = 0; i < log.count; ++i)
        assert(log.events[i].target.side <= 1 && log.events[i].target.slot < BC_MAX_ACTIVE &&
               log.events[i].user.side <= 1 && log.events[i].user.slot < BC_MAX_ACTIVE);
    Action sd = {.kind = ACT_MOVE, .user = {0, 0}, .move_slot = 2, .target = {{255, 255}}};
    assert(battle_validate_action(&b, &sd));
    battle_run_turn(&b, &sd, 1, &log);
    assert(find(&log, EV_STAGE)->target.side == 0 && find(&log, EV_STAGE)->target.slot == 0);
    for (unsigned i = 0; i < log.count; ++i)
        assert(log.events[i].target.side <= 1 && log.events[i].target.slot < BC_MAX_ACTIVE);
    // No target left: both foes fainted earlier this turn (they still have a reserve).
    make_volatiles(&r, BATTLE_FORMAT_DOUBLE);
    add_reserve(&r, 1);
    r.opponent[0].record.hp = r.opponent[1].record.hp = 1;
    BattleMove surf = damaging(40, T_NORMAL, 0);
    surf.target = BTARGET_ALL_FOES;
    set_move(&r.player[1], 1, 57, surf, 15);                       // player 1 (speed 80) moves first
    set_move(&r.player[0], 1, 45, growl, 40);
    assert(battle_init(&b, &r, 1) == BI_OK);
    const Action late[] = {{.kind = ACT_MOVE, .user = {0, 0}, .move_slot = 1, .target = {{7, 200}}},
                           slot_move(0, 1, 0, 1)};
    battle_run_turn(&b, late, 2, &log);
    assert(event_count(&log, EV_FAINT) == 2 && b.outcome == OUTCOME_NONE);
    const BattleEvent *none = NULL;
    for (unsigned i = 0; i < log.count; ++i)
        if (log.events[i].kind == EV_NO_EFFECT && log.events[i].move_id == 45) none = &log.events[i];
    assert(none && none->target.side == 1 && none->target.slot < BC_MAX_ACTIVE);
    // An explicit single target is still checked against side, slot and reach.
    Action bad = {.kind = ACT_MOVE, .user = {0, 0}, .move_slot = 0, .target = {{0, 1}}};   // own ally
    assert(!battle_validate_action(&b, &bad));
    bad.target.pos = (Position){1, 3};
    assert(!battle_validate_action(&b, &bad));
}

// ---------------------------------------------------------------- older states leave newer fields empty (audit P-01)

static void test_state_decode_defaults(void) {
    static BattleRosterState s, out;
    static uint8_t bytes[256];
    memset(&s, 0, sizeof(s));
    s.field_state = (BattleMultiState){.phase = BPHASE_CHOOSE, .token = 5};
    for (unsigned side = 0; side < 2; ++side)
        for (unsigned slot = 0; slot < BATTLE_FIELD_SLOTS; ++slot) s.field_state.field[side][slot].party_slot = BATTLE_SLOT_NONE;
    for (unsigned i = 0; i < BATTLE_MAX_PER_SIDE; ++i) s.player[i].party_slot = BATTLE_SLOT_NONE;
    s.status[1][0] = BST_BURN;
    s.stages[0][1][BSTAT_ATK] = 2;
    unsigned n;
    // Decode each older layout into a receiver full of stale bytes (the GBA's stack variable).
    memset(&out, 0xA5, sizeof(out));
    n = battle_condition_state_encode(&s, bytes);                           // V7 / V8
    assert(battle_condition_state_decode(bytes, n, &out));
    assert(out.status[1][0] == BST_BURN && out.stages[0][1][BSTAT_ATK] == 2);
    for (unsigned side = 0; side < 2; ++side)
        for (unsigned slot = 0; slot < BATTLE_FIELD_SLOTS; ++slot) assert(out.volatiles[side][slot] == 0);
    memset(&out, 0xA5, sizeof(out));
    n = battle_struggle_state_encode(&s, bytes);                            // V4-V6
    assert(battle_pivot_state_decode(bytes, n, &out));
    for (unsigned side = 0; side < 2; ++side)
        for (unsigned slot = 0; slot < BATTLE_FIELD_SLOTS; ++slot) {
            assert(!out.status[side][slot] && !out.volatiles[side][slot]);
            for (unsigned k = 0; k < BSTAT_COUNT; ++k) assert(!out.stages[side][slot][k]);
        }
    memset(&out, 0xA5, sizeof(out));
    n = battle_roster_state_encode(&s, bytes);                              // V2-V3
    assert(battle_roster_state_decode(bytes, n, &out));
    assert(!out.struggle_mask && !out.status[1][0] && !out.volatiles[0][0]);
}

// ---------------------------------------------------------------- later-version fields in an older request (audit P-03)

static void test_version_schema(void) {
    static BattleRequest r, d;
    static uint8_t bytes[4096];
    make_effects(&r, BATTLE_FORMAT_SINGLE);                      // V8
    BattleMove bite = damaging(60, T_NORMAL, 0);
    push(&bite, fx(BEFF_FLINCH, BWHO_TARGET, 30, 0, 0, 0));
    set_move(&r.player[0], 1, 44, bite, 25);
    uint32_t n = battle_request_encode(&r, bytes, sizeof(bytes));
    assert(n && !battle_request_decode(bytes, n, &d));            // a V9 kind in V8: malformed
    make_effects(&r, BATTLE_FORMAT_SINGLE);
    BattleMove gust = damaging(40, T_NORMAL, 0);
    gust.move_flags = BMF_DOUBLE_AIRBORNE;
    set_move(&r.player[0], 1, 16, gust, 35);
    n = battle_request_encode(&r, bytes, sizeof(bytes));
    assert(n && !battle_request_decode(bytes, n, &d));            // a V9 move flag in V8
    r.version = BATTLE_REQUEST_VERSION_VOLATILES;                 // the same move is fine in V9
    r.volatiles = (BattleVolatileRules){.confusion_hit_percent = 33, .confusion_min = 2, .confusion_max = 5,
                                        .confusion_power = 40, .substitute_divisor = 4};
    n = battle_request_encode(&r, bytes, sizeof(bytes));
    assert(n && battle_request_decode(bytes, n, &d) && d.player[0].mon.moves[1].move_flags == BMF_DOUBLE_AIRBORNE);
    bytes[0] = BATTLE_REQUEST_VERSION_TIMING + 1;                 // a future version
    assert(!battle_request_decode(bytes, n, &d));
}

// ------------------------------------------------------------- V11 abilities (Milestone 4-A)

static void make_abilities(BattleRequest *r, uint8_t format) {
    make_timing(r, format);
    r->version = BATTLE_REQUEST_VERSION_ABILITIES;
    r->rules &= (uint16_t)~RULE_IGNORE_ABILITIES;
}

// One-hook ability (a V11 descriptor); NONE / UNSUPPORTED have no hooks.
static BattleAbility ab(uint8_t kind, uint8_t arg0, int8_t arg1, uint8_t arg2, uint8_t chance, uint8_t percent,
                        uint16_t source) {
    if (kind == BABIL_NONE || kind == BABIL_UNSUPPORTED)
        return (BattleAbility){.cls = kind == BABIL_NONE ? BACLASS_NONE : BACLASS_UNSUPPORTED, .source = source};
    return (BattleAbility){.cls = BACLASS_HOOKS, .count = 1, .source = source,
                           .hooks = {{kind, {{arg0, arg1, arg2, chance, (uint8_t)(chance ? 100 : 0), percent}}, 0}}};
}
#define HOOK0(e) ((e).mon.ability.hooks[0])
static bool hook_ok(BattleAbilityHook h) { return battle_ability_hook_valid(&h, NTYPES, BATTLE_REQUEST_VERSION_ABILITIES); }

static void give(BattleEntry *e, BattleAbility a) {
    e->record.ability = a.source;
    e->mon.ability = a;
}

static Battler *at_pos(Battle *b, uint8_t side, uint8_t slot) {
    return (Battler *)battle_at(b, (Position){side, slot});
}

static BattleMove contact(uint16_t power, uint8_t hits) {
    BattleMove m = damaging(power, T_NORMAL, 0);
    m.effects[0] = fx(BEFF_DAMAGE, BWHO_TARGET, 0, hits, 0, hits);
    m.move_flags |= BMF_CONTACT;
    return m;
}

#define HUGE_POWER ab(BABIL_ATTACK_MULT, BCAT_PHYSICAL, 0, 0, 0, 200, 37)
#define TECHNICIAN ab(BABIL_LOW_POWER, 60, 0, 0, 0, 150, 101)
#define THICK_FAT ab(BABIL_RESIST, T_FIRE, 0, T_WATER, 0, 50, 47)
#define BLAZE ab(BABIL_PINCH, T_FIRE, 0, 3, 0, 150, 66)
#define LEVITATE ab(BABIL_TYPE_IMMUNE, T_FIRE, 0, 0, 0, 0, 26)
#define VOLT_ABSORB ab(BABIL_TYPE_ABSORB, T_WATER, 0, 0, 0, 25, 10)
#define LIMBER ab(BABIL_STATUS_IMMUNE, 1u << BST_PARALYSIS, 0, 0, 0, 0, 7)
#define INNER_FOCUS ab(BABIL_VOLATILE_IMMUNE, BABIL_VOL_FLINCH | BABIL_VOL_INTIMIDATE, 0, 0, 0, 0, 39)
#define OWN_TEMPO ab(BABIL_VOLATILE_IMMUNE, BABIL_VOL_CONFUSION | BABIL_VOL_INTIMIDATE, 0, 0, 0, 0, 20)
#define CLEAR_BODY ab(BABIL_STAT_DROP_IMMUNE, 0x7f, 0, 0, 0, 0, 29)
#define HYPER_CUTTER ab(BABIL_STAT_DROP_IMMUNE, 1u << BSTAT_ATK, 0, 0, 0, 0, 52)
#define KEEN_EYE ab(BABIL_STAT_DROP_IMMUNE, 1u << BSTAT_ACC, 0, BABIL_IGNORE_EVASION, 0, 0, 51)
#define COMPOUND_EYES ab(BABIL_ACCURACY, 0, 0, 0, 0, 130, 14)
#define NO_GUARD ab(BABIL_NO_GUARD, 0, 0, 0, 0, 0, 99)
#define STATIC ab(BABIL_CONTACT_STATUS, BST_PARALYSIS, 0, 0, 100, 0, 9)
#define ROUGH_SKIN ab(BABIL_CONTACT_DAMAGE, 0, 0, 8, 0, 0, 24)
#define INTIMIDATE ab(BABIL_ENTRY_STAT, BSTAT_ATK, -1, 0, 0, 0, 22)
#define SPEED_BOOST ab(BABIL_END_TURN_STAT, BSTAT_SPE, 1, 0, 0, 0, 3)
#define STURDY ab(BABIL_STURDY, 0, 0, 0, 0, 0, 5)
#define ROCK_HEAD ab(BABIL_NO_RECOIL, 0, 0, 0, 0, 0, 69)
#define SHIELD_DUST ab(BABIL_SHIELD_DUST, 0, 0, 0, 0, 0, 19)
#define SERENE_GRACE ab(BABIL_SECONDARY_CHANCE, 0, 0, 0, 0, 200, 32)
#define INFILTRATOR ab(BABIL_INFILTRATOR, 0, 0, 0, 0, 0, 151)
#define UNSUPPORTED(id) ab(BABIL_UNSUPPORTED, 0, 0, 0, 0, 0, id)

static void test_ability_wire(void) {
    static BattleRequest r, d;
    static uint8_t bytes[4096];
    make_abilities(&r, BATTLE_FORMAT_DOUBLE);
    give(&r.player[0], INTIMIDATE);
    give(&r.player[1], HUGE_POWER);
    give(&r.opponent[0], UNSUPPORTED(104));
    give(&r.opponent[1], THICK_FAT);
    r.player[0].mon.moves[0].move_flags |= BMF_CONTACT;
    uint32_t n = battle_request_encode(&r, bytes, sizeof(bytes));
    assert(n == battle_request_size(&r) && battle_request_decode(bytes, n, &d));
    assert(HOOK0(d.player[0]).kind == BABIL_ENTRY_STAT && HOOK0(d.player[0]).arg1 == -1 &&
           d.player[0].mon.ability.source == 22);
    assert(HOOK0(d.player[1]).percent == 200 && d.opponent[0].mon.ability.cls == BACLASS_UNSUPPORTED &&
           HOOK0(d.opponent[1]).arg2 == T_WATER && (d.player[0].mon.moves[0].move_flags & BMF_CONTACT));
    // The block is 8 bytes per entry, between the effect lists and the volatile rules.
    static BattleRequest v10;
    v10 = r;
    v10.version = BATTLE_REQUEST_VERSION_TIMING;
    v10.player[0].mon.moves[0].move_flags &= (uint8_t)~BMF_CONTACT;
    assert(battle_request_size(&r) == battle_request_size(&v10) + 4 * BATTLE_ABILITY_BYTES);
    const uint32_t block = n - BATTLE_VOLATILE_RULE_BYTES - 4 * BATTLE_ABILITY_BYTES;
    assert(bytes[block] == BABIL_ENTRY_STAT && bytes[block + 6] == 22);
    // Source id must match the record, kinds and arguments must be exact.
    bytes[block + 6] = 23;
    assert(!battle_request_decode(bytes, n, &d));
    bytes[block + 6] = 22;
    bytes[block] = BABIL_COUNT;
    assert(!battle_request_decode(bytes, n, &d));
    bytes[block] = BABIL_ENTRY_STAT;
    bytes[block + 3] = 1;  // an argument ENTRY_STAT does not use
    assert(!battle_request_decode(bytes, n, &d));
    bytes[block + 3] = 0;
    assert(battle_request_decode(bytes, n, &d));
    assert(!battle_request_decode(bytes, n - 1, &d));
    // Later-version fields in an earlier request are malformed (audit P-03 rule).
    bytes[0] = BATTLE_REQUEST_VERSION_TIMING;
    assert(!battle_request_decode(bytes, n, &d));
    n = battle_request_encode(&v10, bytes, sizeof(bytes));
    assert(n && battle_request_decode(bytes, n, &d) && d.player[0].mon.ability.cls == BACLASS_NONE &&
           d.player[0].mon.ability.source == 0);
    v10.player[0].mon.moves[0].move_flags |= BMF_CONTACT;
    n = battle_request_encode(&v10, bytes, sizeof(bytes));
    assert(n && !battle_request_decode(bytes, n, &d));
    // Descriptor validation.
    const BattleAbility valid_static = STATIC;
    assert(battle_ability_valid(&valid_static, NTYPES, BATTLE_REQUEST_VERSION_ABILITIES));
    assert(!hook_ok((BattleAbilityHook){.kind = BABIL_CONTACT_STATUS, .arg0 = BST_PARALYSIS}));
    assert(!hook_ok((BattleAbilityHook){.kind = BABIL_TYPE_IMMUNE, .arg0 = NTYPES}));
    assert(!hook_ok((BattleAbilityHook){.kind = BABIL_ACCURACY}));
    assert(!battle_ability_valid(&(BattleAbility){.cls = BACLASS_NONE, .count = 1}, NTYPES,
                                 BATTLE_REQUEST_VERSION_ABILITIES));
    assert(!hook_ok((BattleAbilityHook){.kind = BABIL_ENTRY_STAT, .arg0 = BSTAT_ATK}));
    assert(strcmp(battle_ability_kind_name(BABIL_INFILTRATOR), "infiltrator") == 0);
}

static void test_ability_damage(void) {
    static BattleRequest r, plain;
    Battle b, p;
    uint8_t eff;
    make_abilities(&r, BATTLE_FORMAT_SINGLE);
    BattleMove fire = damaging(40, T_FIRE, 0), special = fire;
    special.category = BCAT_SPECIAL;
    BattleMove power61 = damaging(61, T_NORMAL, 0), power60 = damaging(60, T_NORMAL, 0);
    BattleMove power90 = damaging(90, T_NORMAL, 0), power40 = damaging(40, T_NORMAL, 0);
    plain = r;
    give(&r.player[0], HUGE_POWER);
    assert(battle_init(&b, &r, 1) == BI_OK && battle_init(&p, &plain, 1) == BI_OK);
    Battler *a = battle_active(&b, 0), *pa = battle_active(&p, 0);
    pa->attack = 120;
    assert(battle_damage(&b, a, &fire, battle_active(&b, 1), 100, &eff) ==
           battle_damage(&p, pa, &fire, battle_active(&p, 1), 100, &eff));
    pa->attack = 60;
    assert(battle_damage(&b, a, &special, battle_active(&b, 1), 100, &eff) ==
           battle_damage(&p, pa, &special, battle_active(&p, 1), 100, &eff));  // physical only
    // Technician: power <= 60 x1.5 (40 -> 60); 61 unchanged.
    give(&r.player[0], TECHNICIAN);
    assert(battle_init(&b, &r, 1) == BI_OK);
    a = battle_active(&b, 0);
    assert(battle_damage(&b, a, &power40, battle_active(&b, 1), 100, &eff) ==
           battle_damage(&p, pa, &power60, battle_active(&p, 1), 100, &eff));
    assert(battle_damage(&b, a, &power60, battle_active(&b, 1), 100, &eff) ==
           battle_damage(&p, pa, &power90, battle_active(&p, 1), 100, &eff));
    assert(battle_damage(&b, a, &power61, battle_active(&b, 1), 100, &eff) ==
           battle_damage(&p, pa, &power61, battle_active(&p, 1), 100, &eff));
    // Thick Fat (Fire, Water): the attacker's stat is halved; Normal is unchanged.
    give(&r.player[0], (BattleAbility){0});
    give(&r.opponent[0], THICK_FAT);
    assert(battle_init(&b, &r, 1) == BI_OK);
    a = battle_active(&b, 0);
    pa->attack = 30;
    assert(battle_damage(&b, a, &fire, battle_active(&b, 1), 100, &eff) ==
           battle_damage(&p, pa, &fire, battle_active(&p, 1), 100, &eff));
    pa->attack = 60;
    assert(battle_damage(&b, a, &power40, battle_active(&b, 1), 100, &eff) ==
           battle_damage(&p, pa, &power40, battle_active(&p, 1), 100, &eff));
    // Blaze: Fire moves x1.5 at or below a third of max HP.
    give(&r.opponent[0], (BattleAbility){0});
    give(&r.player[0], BLAZE);
    assert(battle_init(&b, &r, 1) == BI_OK);
    a = battle_active(&b, 0);
    a->hp = a->max_hp / 3 + 1;
    const uint16_t above = battle_damage(&b, a, &fire, battle_active(&b, 1), 100, &eff);
    assert(above == battle_damage(&p, pa, &fire, battle_active(&p, 1), 100, &eff));
    a->hp = a->max_hp / 3;
    pa->attack = 90;
    assert(battle_damage(&b, a, &fire, battle_active(&b, 1), 100, &eff) ==
           battle_damage(&p, pa, &fire, battle_active(&p, 1), 100, &eff));
    // The rule and older versions keep the ability inert.
    pa->attack = 60;
    r.rules |= RULE_IGNORE_ABILITIES;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_active(&b, 0)->hp = 1;
    assert(battle_damage(&b, battle_active(&b, 0), &fire, battle_active(&b, 1), 100, &eff) ==
           battle_damage(&p, pa, &fire, battle_active(&p, 1), 100, &eff));
}

static void test_ability_immunity(void) {
    static BattleRequest r;
    Battle b;
    EventLog log;
    make_abilities(&r, BATTLE_FORMAT_SINGLE);
    give(&r.opponent[0], LEVITATE);
    BattleMove fire = damaging(40, T_FIRE, 1);          // 1% accuracy: an accuracy check would miss
    set_move(&r.player[0], 1, 53, fire, 10);
    BattleMove growl = effect_move(BTARGET_SINGLE, 0, fx(BEFF_STAT, BWHO_TARGET, 0, BSTAT_ATK, -1, 0));
    growl.type = T_FIRE;                                // a status move of the type: not blocked
    set_move(&r.player[0], 2, 45, growl, 10);
    BattleMove wave = effect_move(BTARGET_SINGLE, 0, fx(BEFF_STATUS, BWHO_TARGET, 0, BST_PARALYSIS, 0, 0));
    wave.type = T_FIRE;
    wave.effects[0].flags = BEFL_TYPE_CHART;           // uses the type chart (Thunder Wave): blocked
    set_move(&r.player[0], 3, 86, wave, 10);
    for (uint32_t seed = 1; seed < 40; ++seed) {
        assert(battle_init(&b, &r, seed) == BI_OK);
        battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
        const BattleEvent *e = find(&log, EV_ABILITY_BLOCK);
        assert(e && e->user.side == 1 && e->ability == 26 && e->condition == BABIL_TYPE_IMMUNE);
        assert(!find(&log, EV_MISS) && !find(&log, EV_DAMAGE));
    }
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 2)}, 1, &log);
    assert(find(&log, EV_STAGE) && !find(&log, EV_ABILITY_BLOCK));
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 3)}, 1, &log);
    assert(find(&log, EV_ABILITY_BLOCK) && !find(&log, EV_STATUS) && !find(&log, EV_NO_EFFECT));

    // Volt Absorb (Water here): heals a quarter, once per target, even through a substitute.
    make_abilities(&r, BATTLE_FORMAT_SINGLE);
    give(&r.opponent[0], VOLT_ABSORB);
    BattleMove water = damaging(40, T_WATER, 0);
    water.effects[0] = fx(BEFF_DAMAGE, BWHO_TARGET, 0, 3, 0, 3);
    set_move(&r.player[0], 1, 61, water, 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    Battler *holder = battle_active(&b, 1);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(find(&log, EV_ABILITY_BLOCK) && !find(&log, EV_HEAL) && holder->hp == holder->max_hp);
    holder->hp = 10;
    holder->vol.substitute_hp = 30;
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(event_count(&log, EV_ABILITY) == 1 && event_count(&log, EV_HEAL) == 1);
    assert(holder->hp == 10 + holder->max_hp / 4 && holder->vol.substitute_hp == 30 && !find(&log, EV_DAMAGE));
    // Double spread: each holder once.
    make_abilities(&r, BATTLE_FORMAT_DOUBLE);
    give(&r.opponent[0], VOLT_ABSORB);
    give(&r.opponent[1], VOLT_ABSORB);
    water = damaging(40, T_WATER, 0);
    water.target = BTARGET_ALL_FOES;
    set_move(&r.player[0], 1, 57, water, 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    at_pos(&b, 1, 0)->hp = at_pos(&b, 1, 1)->hp = 50;
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(event_count(&log, EV_HEAL) == 2 && at_pos(&b, 1, 0)->hp == 100 && at_pos(&b, 1, 1)->hp == 100);
}

static void test_ability_accuracy(void) {
    static BattleRequest r;
    Battle b;
    EventLog log;
    make_abilities(&r, BATTLE_FORMAT_SINGLE);
    set_move(&r.player[0], 1, 12, damaging(40, T_NORMAL, 1), 10);
    set_move(&r.player[0], 2, 90, damaging(40, T_NORMAL, 77), 10);
    unsigned misses_plain = 0;
    for (uint32_t seed = 1; seed < 60; ++seed) {
        assert(battle_init(&b, &r, seed) == BI_OK);
        battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 2)}, 1, &log);
        misses_plain += find(&log, EV_MISS) != NULL;
    }
    assert(misses_plain > 0);
    // Compound Eyes: 77 x 1.3 = 100.
    give(&r.player[0], COMPOUND_EYES);
    for (uint32_t seed = 1; seed < 60; ++seed) {
        assert(battle_init(&b, &r, seed) == BI_OK);
        battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 2)}, 1, &log);
        assert(!find(&log, EV_MISS));
    }
    // No Guard on either side: a 1% move always hits, also a target in the air.
    for (unsigned side = 0; side < 2; ++side) {
        give(&r.player[0], side ? (BattleAbility){0} : NO_GUARD);
        give(&r.opponent[0], side ? NO_GUARD : (BattleAbility){0});
        for (uint32_t seed = 1; seed < 40; ++seed) {
            assert(battle_init(&b, &r, seed) == BI_OK);
            battle_active(&b, 1)->vol.semi_invulnerable = BSEMI_AIR;
            battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
            assert(!find(&log, EV_MISS) && find(&log, EV_DAMAGE));
        }
    }
    // Keen Eye ignores the target's evasion stages.
    give(&r.opponent[0], (BattleAbility){0});
    set_move(&r.player[0], 3, 33, damaging(40, T_NORMAL, 100), 10);
    unsigned misses = 0;
    for (uint32_t seed = 1; seed < 40; ++seed) {
        give(&r.player[0], seed & 1 ? KEEN_EYE : (BattleAbility){0});
        assert(battle_init(&b, &r, seed) == BI_OK);
        battle_active(&b, 1)->stat_stages[BSTAT_EVA] = 6;
        battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 3)}, 1, &log);
        if (seed & 1) assert(!find(&log, EV_MISS));
        else misses += find(&log, EV_MISS) != NULL;
    }
    assert(misses > 0);
}

static void test_ability_sturdy(void) {
    static BattleRequest r;
    Battle b;
    EventLog log;
    make_abilities(&r, BATTLE_FORMAT_SINGLE);
    give(&r.opponent[0], STURDY);
    r.opponent[0].record.hp = r.opponent[0].record.max_hp = 12;
    set_move(&r.player[0], 1, 88, damaging(120, T_NORMAL, 0), 10);
    set_move(&r.player[0], 2, 89, contact(40, 3), 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(battle_active(&b, 1)->hp == 1 && !find(&log, EV_FAINT));
    assert(index_of(&log, EV_ABILITY, 0) == index_of(&log, EV_DAMAGE, 0) + 1);
    assert(find(&log, EV_ABILITY)->condition == BABIL_STURDY);
    // Not at full HP: it faints.
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(b.sides[1].battlers[0].fainted && !find(&log, EV_ABILITY));
    // Multi-hit (per hit): the first hit leaves 1 HP, the second knocks it out.
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 2)}, 1, &log);
    assert(event_count(&log, EV_DAMAGE) == 2 && event_count(&log, EV_ABILITY) == 1 && b.sides[1].battlers[0].fainted);
    // A substitute taking the hit: no Sturdy, the Pokemon keeps full HP.
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_active(&b, 1)->vol.substitute_hp = 3;
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(find(&log, EV_SUBSTITUTE_BREAK) && !find(&log, EV_ABILITY) && battle_active(&b, 1)->hp == 12);
    // The confusion self-hit too.
    make_abilities(&r, BATTLE_FORMAT_SINGLE);
    r.volatiles.confusion_hit_percent = 100;
    r.volatiles.confusion_power = 250;
    give(&r.player[0], STURDY);
    r.player[0].record.hp = r.player[0].record.max_hp = 5;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_active(&b, 0)->vol.confusion = 5;
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 0)}, 1, &log);
    assert(find(&log, EV_CONFUSION_HIT) && battle_active(&b, 0)->hp == 1 && find(&log, EV_ABILITY));
}

static void test_ability_contact(void) {
    static BattleRequest r;
    Battle b;
    EventLog log;
    make_abilities(&r, BATTLE_FORMAT_SINGLE);
    give(&r.opponent[0], ROUGH_SKIN);
    // Three contact hits, each with a per-hit (V10) Defense drop: hit, drop, ability, damage per hit.
    BattleMove m = contact(10, 3);
    BattleEffect drop = fx(BEFF_STAT, BWHO_TARGET, 0, BSTAT_DEF, -1, 0);
    drop.flags = BEFL_TIMING(BETIME_PER_HIT);
    push(&m, drop);
    set_move(&r.player[0], 1, 24, m, 10);
    set_move(&r.player[0], 2, 25, damaging(10, T_NORMAL, 0), 10);  // no contact
    assert(battle_init(&b, &r, 1) == BI_OK);
    Battler *user = battle_active(&b, 0);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(event_count(&log, EV_ABILITY_DAMAGE) == 3 && user->hp == user->max_hp - 3 * (user->max_hp / 8));
    for (unsigned h = 0; h < 3; ++h) {
        const unsigned hit = index_of(&log, EV_DAMAGE, h);
        assert(index_of(&log, EV_STAGE, h) == hit + 1 && index_of(&log, EV_ABILITY, h) == hit + 2 &&
               index_of(&log, EV_ABILITY_DAMAGE, h) == hit + 3);
    }
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 2)}, 1, &log);
    assert(!find(&log, EV_ABILITY) && !find(&log, EV_ABILITY_DAMAGE));
    // Behind a substitute: the Pokemon was not struck, no contact ability.
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_active(&b, 1)->vol.substitute_hp = 200;
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(event_count(&log, EV_SUBSTITUTE_HIT) == 3 && !find(&log, EV_ABILITY_DAMAGE));
    // The attacker knocked out by Rough Skin: the multi-hit stops there.
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_active(&b, 0)->hp = 1;
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(event_count(&log, EV_DAMAGE) == 1 && b.sides[0].battlers[0].fainted);
    // Struggle makes contact in V11.
    assert(battle_init(&b, &r, 1) == BI_OK);
    user = battle_active(&b, 0);
    for (unsigned i = 0; i < 4; ++i) user->pp[i] = 0;
    battle_run_turn(&b, (Action[]){{.kind = ACT_STRUGGLE, .user = {0, 0}}}, 1, &log);
    assert(find(&log, EV_ABILITY_DAMAGE) && find(&log, EV_RECOIL));

    // Static (chance 100 here): the attacker is paralysed after the first contact hit only.
    give(&r.opponent[0], STATIC);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(event_count(&log, EV_STATUS) == 1 && BATTLE_CONDITION_KIND(battle_active(&b, 0)->status) == BST_PARALYSIS);
    assert(find(&log, EV_STATUS)->target.side == 0 && event_count(&log, EV_ABILITY) == 1);
    // An attacker that cannot be paralysed (Limber, or the Electric type under the rule) draws nothing.
    give(&r.player[0], LIMBER);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(!find(&log, EV_STATUS) && !find(&log, EV_ABILITY));
    give(&r.player[0], (BattleAbility){0});
    r.player[0].mon.types[0] = r.player[0].mon.types[1] = T_WATER;  // the rules' electric type
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(!find(&log, EV_STATUS));
}

static void test_ability_effect_level(void) {
    static BattleRequest r;
    Battle b;
    EventLog log;
    make_abilities(&r, BATTLE_FORMAT_SINGLE);
    // Shield Dust: a chance effect on the holder is skipped; the user's own effect still runs.
    BattleMove m = damaging(10, T_NORMAL, 0);
    push(&m, fx(BEFF_STAT, BWHO_TARGET, 100, BSTAT_DEF, -1, 0));
    push(&m, fx(BEFF_STAT, BWHO_USER, 100, BSTAT_ATK, 1, 0));
    set_move(&r.player[0], 1, 70, m, 10);
    give(&r.opponent[0], SHIELD_DUST);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(battle_active(&b, 1)->stat_stages[BSTAT_DEF] == 0 && battle_active(&b, 0)->stat_stages[BSTAT_ATK] == 1);
    // Serene Grace: 50% becomes 100%.
    give(&r.opponent[0], (BattleAbility){0});
    m = damaging(10, T_NORMAL, 0);
    push(&m, fx(BEFF_STAT, BWHO_TARGET, 50, BSTAT_DEF, -1, 0));
    set_move(&r.player[0], 1, 70, m, 10);
    unsigned plain = 0;
    for (uint32_t seed = 1; seed < 40; ++seed) {
        give(&r.player[0], seed & 1 ? SERENE_GRACE : (BattleAbility){0});
        assert(battle_init(&b, &r, seed) == BI_OK);
        battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
        if (seed & 1) assert(find(&log, EV_STAGE));
        else plain += find(&log, EV_STAGE) == NULL;
    }
    assert(plain > 0);
    // Limber: a status move is stopped with a message; a chance effect silently.
    give(&r.player[0], (BattleAbility){0});
    give(&r.opponent[0], LIMBER);
    set_move(&r.player[0], 1, 86, effect_move(BTARGET_SINGLE, 0, fx(BEFF_STATUS, BWHO_TARGET, 0, BST_PARALYSIS, 0, 0)), 10);
    m = damaging(10, T_NORMAL, 0);
    push(&m, fx(BEFF_STATUS, BWHO_TARGET, 100, BST_PARALYSIS, 0, 0));
    set_move(&r.player[0], 2, 34, m, 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(find(&log, EV_ABILITY_BLOCK) && !find(&log, EV_STATUS) && !find(&log, EV_NO_EFFECT));
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 2)}, 1, &log);
    assert(!find(&log, EV_ABILITY_BLOCK) && !find(&log, EV_STATUS) && !battle_active(&b, 1)->status);
    // ... and a holder entering with that status is cured.
    r.opponent[0].record.status = BST_PARALYSIS;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(find(&log, EV_ABILITY) && find(&log, EV_CURE) && !battle_active(&b, 1)->status);
    r.opponent[0].record.status = 0;
    // Inner Focus (flinch) and Own Tempo (confusion).
    give(&r.opponent[0], INNER_FOCUS);
    m = damaging(10, T_NORMAL, 0);
    m.priority = 1;
    push(&m, fx(BEFF_FLINCH, BWHO_TARGET, 0, 0, 0, 0));
    set_move(&r.player[0], 1, 252, m, 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1), slot_move(1, 0, 0, 0)}, 2, &log);
    assert(find(&log, EV_ABILITY_BLOCK) && !find(&log, EV_FLINCH) && event_count(&log, EV_MOVE_USED) == 2);
    give(&r.opponent[0], OWN_TEMPO);
    set_move(&r.player[0], 1, 48, effect_move(BTARGET_SINGLE, 0, fx(BEFF_CONFUSE, BWHO_TARGET, 0, 0, 0, 0)), 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(find(&log, EV_ABILITY_BLOCK) && !battle_active(&b, 1)->vol.confusion);
    // Clear Body stops others' drops, not the holder's own; Hyper Cutter only Attack.
    give(&r.opponent[0], CLEAR_BODY);
    set_move(&r.player[0], 1, 45, effect_move(BTARGET_SINGLE, 0, fx(BEFF_STAT, BWHO_TARGET, 0, BSTAT_ATK, -1, 0)), 10);
    m = damaging(10, T_NORMAL, 0);
    push(&m, fx(BEFF_STAT, BWHO_USER, 0, BSTAT_DEF, -1, 0));
    set_move(&r.opponent[0], 1, 370, m, 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1), slot_move(1, 0, 0, 1)}, 2, &log);
    assert(find(&log, EV_ABILITY_BLOCK) && battle_active(&b, 1)->stat_stages[BSTAT_ATK] == 0 &&
           battle_active(&b, 1)->stat_stages[BSTAT_DEF] == -1);
    // A raise from another battler (Swagger-like) is not a drop: it applies.
    set_move(&r.player[0], 3, 207, effect_move(BTARGET_SINGLE, 0, fx(BEFF_STAT, BWHO_TARGET, 0, BSTAT_ATK, 2, 0)), 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 3)}, 1, &log);
    assert(!find(&log, EV_ABILITY_BLOCK) && battle_active(&b, 1)->stat_stages[BSTAT_ATK] == 2);
    give(&r.opponent[0], HYPER_CUTTER);
    set_move(&r.player[0], 2, 39, effect_move(BTARGET_SINGLE, 0, fx(BEFF_STAT, BWHO_TARGET, 0, BSTAT_DEF, -1, 0)), 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 2)}, 1, &log);
    assert(battle_active(&b, 1)->stat_stages[BSTAT_ATK] == 0 && battle_active(&b, 1)->stat_stages[BSTAT_DEF] == -1);
    // Rock Head: no move recoil; Struggle recoil stays.
    give(&r.opponent[0], (BattleAbility){0});
    give(&r.player[0], ROCK_HEAD);
    m = damaging(60, T_NORMAL, 0);
    push(&m, fx(BEFF_RECOIL, BWHO_USER, 0, 33, 0, 0));
    set_move(&r.player[0], 1, 36, m, 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(find(&log, EV_DAMAGE) && !find(&log, EV_RECOIL));
    for (unsigned i = 0; i < 4; ++i) battle_active(&b, 0)->pp[i] = 0;
    battle_run_turn(&b, (Action[]){{.kind = ACT_STRUGGLE, .user = {0, 0}}}, 1, &log);
    assert(find(&log, EV_RECOIL));
    // Infiltrator: the target's substitute neither takes the hit nor stops a status move.
    give(&r.player[0], INFILTRATOR);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_active(&b, 1)->vol.substitute_hp = 50;
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 0)}, 1, &log);
    assert(find(&log, EV_DAMAGE) && !find(&log, EV_SUBSTITUTE_HIT) && battle_active(&b, 1)->vol.substitute_hp == 50);
}

static void test_ability_entry(void) {
    static BattleRequest r;
    Battle b;
    EventLog log;
    // Double: Intimidate at the battle start lowers both foes once; Clear Body and Inner Focus block.
    make_abilities(&r, BATTLE_FORMAT_DOUBLE);
    give(&r.player[0], INTIMIDATE);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(event_count(&log, EV_ABILITY) == 1 && event_count(&log, EV_STAGE) == 2);
    assert(at_pos(&b, 1, 0)->stat_stages[BSTAT_ATK] == -1 && at_pos(&b, 1, 1)->stat_stages[BSTAT_ATK] == -1);
    assert(at_pos(&b, 0, 1)->stat_stages[BSTAT_ATK] == 0);
    give(&r.opponent[0], CLEAR_BODY);
    give(&r.opponent[1], INNER_FOCUS);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(event_count(&log, EV_ABILITY_BLOCK) == 2 && !find(&log, EV_STAGE));
    // At -6 and behind a substitute it fails (switch-in of a reserve holder).
    make_abilities(&r, BATTLE_FORMAT_DOUBLE);
    add_reserve(&r, 0);
    give(&r.player[2], INTIMIDATE);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(log.count == 0);
    at_pos(&b, 1, 0)->stat_stages[BSTAT_ATK] = -6;
    at_pos(&b, 1, 1)->vol.substitute_hp = 10;
    battle_run_turn(&b, (Action[]){{.kind = ACT_SWITCH, .user = {0, 0}, .reserve = 2}}, 1, &log);
    assert(find(&log, EV_SEND_OUT) && event_count(&log, EV_ABILITY) == 1 && event_count(&log, EV_FAIL) == 2);
    assert(index_of(&log, EV_ABILITY, 0) > index_of(&log, EV_SEND_OUT, 0));
    // Replacement after a faint also triggers it.
    assert(battle_init(&b, &r, 1) == BI_OK);
    set_fainted(&b, 0, 0);
    b.sides[0].active[0] = BATTLE_SLOT_NONE;
    assert(battle_replace(&b, (Position){0, 0}, 2, &log));
    assert(event_count(&log, EV_STAGE) == 2);
    // Triple and Rotation: every standing foe on the field.
    make_abilities(&r, BATTLE_FORMAT_TRIPLE);
    give(&r.player[0], INTIMIDATE);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(event_count(&log, EV_STAGE) == 3);
    make_abilities(&r, BATTLE_FORMAT_ROTATION);
    give(&r.opponent[2], INTIMIDATE);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(event_count(&log, EV_STAGE) == 3 && find(&log, EV_ABILITY)->user.side == 1);
    // Leads in speed order: the faster holder acts first.
    make_abilities(&r, BATTLE_FORMAT_SINGLE);
    give(&r.player[0], INTIMIDATE);
    give(&r.opponent[0], INTIMIDATE);
    r.opponent[0].record.speed = 200;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(event_count(&log, EV_ABILITY) == 2 && log.events[index_of(&log, EV_ABILITY, 0)].user.side == 1);
    // Older versions: nothing at the start.
    make_timing(&r, BATTLE_FORMAT_SINGLE);
    give(&r.player[0], INTIMIDATE);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(log.count == 0);
}

static void test_ability_end_turn(void) {
    static BattleRequest r;
    Battle b;
    EventLog log;
    make_abilities(&r, BATTLE_FORMAT_SINGLE);
    add_reserve(&r, 0);
    give(&r.player[0], SPEED_BOOST);
    give(&r.player[1], SPEED_BOOST);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 0)}, 1, &log);
    assert(battle_active(&b, 0)->stat_stages[BSTAT_SPE] == 1 && find(&log, EV_ABILITY));
    assert(index_of(&log, EV_STAGE, 0) > index_of(&log, EV_DAMAGE, 0));
    // Switched in during the turn: no boost that turn, one the next.
    battle_run_turn(&b, (Action[]){{.kind = ACT_SWITCH, .user = {0, 0}, .reserve = 1}}, 1, &log);
    assert(battle_active(&b, 0) == &b.sides[0].battlers[1] && battle_active(&b, 0)->stat_stages[BSTAT_SPE] == 0);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 0)}, 1, &log);
    assert(battle_active(&b, 0)->stat_stages[BSTAT_SPE] == 1);
    // At +6 nothing happens.
    battle_active(&b, 0)->stat_stages[BSTAT_SPE] = 6;
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 0)}, 1, &log);
    assert(!find(&log, EV_ABILITY));
}

static void test_ability_unsupported(void) {
    static BattleRequest r;
    Battle b;
    EventLog log;
    make_abilities(&r, BATTLE_FORMAT_DOUBLE);
    add_reserve(&r, 1);
    give(&r.player[1], UNSUPPORTED(104));     // e.g. Mold Breaker
    give(&r.opponent[2], UNSUPPORTED(2));     // a reserve: still listed
    give(&r.opponent[0], (BattleAbility){.cls = BACLASS_NONE, .source = 50});  // no effect: silent
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(event_count(&log, EV_ABILITY_UNSUPPORTED) == 2 && log.count == 2);
    assert(log.events[0].user.side == 0 && log.events[0].battler == 1 && log.events[0].ability == 104);
    assert(log.events[1].user.side == 1 && log.events[1].battler == 2 && log.events[1].ability == 2);
    // The battle runs; the ability is inert.
    battle_run_turn(&b, (Action[]){move(0, 1, 0)}, 1, &log);
    assert(find(&log, EV_DAMAGE) && !find(&log, EV_ABILITY));
    r.rules |= RULE_IGNORE_ABILITIES;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(log.count == 0);
}

// Every format, every representative ability, random commands: invariants hold
// and the event log never overflows.
static void test_ability_formats(void) {
    static BattleRequest r;
    static Battle b;
    static EventLog log;
    const BattleAbility pool[] = {HUGE_POWER, TECHNICIAN, THICK_FAT, BLAZE, LEVITATE, VOLT_ABSORB, LIMBER,
                                  INNER_FOCUS, OWN_TEMPO, CLEAR_BODY, HYPER_CUTTER, KEEN_EYE, COMPOUND_EYES,
                                  NO_GUARD, STATIC, ROUGH_SKIN, INTIMIDATE, SPEED_BOOST, STURDY, ROCK_HEAD,
                                  SHIELD_DUST, SERENE_GRACE, INFILTRATOR, UNSUPPORTED(104)};
    const unsigned pool_size = sizeof(pool) / sizeof(*pool);
    const uint8_t formats[] = {BATTLE_FORMAT_SINGLE, BATTLE_FORMAT_DOUBLE, BATTLE_FORMAT_TRIPLE, BATTLE_FORMAT_ROTATION};
    unsigned peak = 0, abilities = 0;
    for (unsigned f = 0; f < 4; ++f)
        for (uint32_t seed = 1; seed <= 60; ++seed) {
            make_abilities(&r, formats[f]);
            for (unsigned side = 0; side < 2; ++side) add_reserve(&r, side);
            for (unsigned side = 0; side < 2; ++side) {
                BattleEntry *e = side ? r.opponent : r.player;
                const unsigned count = side ? r.opponent_count : r.player_count;
                for (unsigned i = 0; i < count; ++i) {
                    give(&e[i], pool[(seed * 7 + side * 5 + i * 3 + f) % pool_size]);
                    e[i].record.hp = e[i].record.max_hp = 60;
                    BattleMove m = contact(30, i == 1 ? 3 : 1);
                    BattleEffect drop = fx(BEFF_STAT, BWHO_TARGET, 30, BSTAT_DEF, -1, 0);
                    drop.flags = BEFL_TIMING(i == 1 ? BETIME_PER_HIT : BETIME_PER_TARGET);
                    push(&m, drop);
                    push(&m, fx(BEFF_FLINCH, BWHO_TARGET, 20, 0, 0, 0));
                    m.type = (uint8_t)((seed + i) % NTYPES == T_GHOST ? T_FIRE : (seed + i) % NTYPES);
                    set_move(&e[i], 1, 200 + i, m, 30);
                    BattleMove spread = damaging(40, T_WATER, 90);
                    spread.target = BTARGET_ALL_FOES;
                    push(&spread, fx(BEFF_RECOIL, BWHO_USER, 0, 25, 0, 0));
                    set_move(&e[i], 2, 300 + i, spread, 30);
                }
            }
            assert(battle_init(&b, &r, seed) == BI_OK);
            battle_begin(&b, &log);
            for (unsigned t = 0; t < 60 && b.outcome == OUTCOME_NONE; ++t) {
                Position pending;
                while (battle_pending_replacement(&b, &pending)) {
                    const uint8_t mask = battle_replacement_mask(&b, pending);
                    uint8_t pick = 0;
                    while (!(mask & (1u << pick))) ++pick;
                    assert(battle_replace(&b, pending, pick, &log));
                }
                if (battle_pending_pivot(&b, NULL)) break;
                Action actions[BC_MAX_ACTIONS];
                unsigned n = 0;
                for (uint8_t side = 0; side < 2; ++side) {
                    Position actors[BC_MAX_ACTIVE];
                    const unsigned count = battle_collect_actors(&b, side, actors);
                    for (unsigned i = 0; i < count; ++i) battle_choose_at(&b, actors[i], &actions[n++]);
                }
                battle_run_turn(&b, actions, n, &log);
                assert(log.count < BC_MAX_EVENTS);
                if (log.count > peak) peak = log.count;
                abilities += event_count(&log, EV_ABILITY) + event_count(&log, EV_ABILITY_BLOCK);
                for (unsigned side = 0; side < 2; ++side)
                    for (unsigned i = 0; i < b.sides[side].count; ++i) {
                        const Battler *x = &b.sides[side].battlers[i];
                        assert(x->hp <= x->max_hp && (x->hp == 0) == x->fainted);
                        for (unsigned s = 0; s < BSTAT_COUNT; ++s)
                            assert(x->stat_stages[s] >= -6 && x->stat_stages[s] <= 6);
                    }
            }
        }
    assert(abilities > 100);
    printf("ability formats: peak events %u of %u, ability events %u\n", peak, (unsigned)BC_MAX_EVENTS, abilities);
}

// ------------------------------------------------------------- V12 multi-hook abilities (Milestone 4-A2)

static void make_hooks(BattleRequest *r, uint8_t format) {
    make_abilities(r, format);
    r->version = BATTLE_REQUEST_VERSION_ABILITY_HOOKS;
}

static BattleAbilityHook hk(uint8_t kind, uint8_t arg0, int8_t arg1, uint8_t arg2, uint8_t num, uint8_t den,
                            uint8_t percent, uint8_t flags) {
    return (BattleAbilityHook){kind, {{arg0, arg1, arg2, num, den, percent}}, flags};
}

static BattleAbility hooks(uint16_t source, unsigned n, const BattleAbilityHook *list) {
    BattleAbility a = {.cls = BACLASS_HOOKS, .count = (uint8_t)n, .source = source};
    for (unsigned i = 0; i < n; ++i) a.hooks[i] = list[i];
    return a;
}

#define HOOKS(source, ...) hooks(source, sizeof((BattleAbilityHook[]){__VA_ARGS__}) / sizeof(BattleAbilityHook), \
                                 (BattleAbilityHook[]){__VA_ARGS__})
#define GUTS HOOKS(62, hk(BABIL_ATTACK_MULT, BCAT_PHYSICAL, 0, 0, 0, 0, 150, BAHF_REQUIRES_STATUS), \
                       hk(BABIL_IGNORE_BURN, 0, 0, 0, 0, 0, 0, 0))
#define MARVEL_SCALE HOOKS(63, hk(BABIL_DEFENSE_MULT, BCAT_PHYSICAL, 0, 0, 0, 0, 150, BAHF_REQUIRES_STATUS))
#define ADAPTABILITY HOOKS(91, hk(BABIL_STAB_MULT, 0, 0, 0, 0, 0, 200, 0))
#define MOXIE HOOKS(153, hk(BABIL_KO_STAT, BSTAT_ATK, 1, 0, 0, 0, 0, 0))
#define SHED_SKIN HOOKS(61, hk(BABIL_END_TURN_CURE, 0xfe, 0, 0, 1, 3, 0, 0))
#define FLASH_FIRE HOOKS(18, hk(BABIL_ABSORB_STATE, T_FIRE, 0, 0, 0, 0, 0, 0), \
                             hk(BABIL_ATTACK_MULT, 0, 0, T_FIRE, 0, 0, 150, BAHF_REQUIRES_STATE | BAHF_TYPE_FILTER))
#define SCRAPPY HOOKS(113, hk(BABIL_TYPE_PASS, T_NORMAL, 0, T_GHOST, 0, 0, 0, 0), \
                           hk(BABIL_VOLATILE_IMMUNE, BABIL_VOL_INTIMIDATE, 0, 0, 0, 0, 0, 0))
#define SYNCHRONIZE HOOKS(28, hk(BABIL_STATUS_MIRROR, 0x1e, 0, 0, 0, 0, 0, 0))
#define INTIMIDATE_ADJ HOOKS(22, hk(BABIL_ENTRY_STAT, BSTAT_ATK, -1, BRANGE_ADJACENT_FOES, 0, 0, 0, 0))
#define INNER_FOCUS12 HOOKS(39, hk(BABIL_VOLATILE_IMMUNE, BABIL_VOL_FLINCH | BABIL_VOL_INTIMIDATE, 0, 0, 0, 0, 0, 0))

static bool hook12_ok(BattleAbilityHook h) {
    return battle_ability_hook_valid(&h, NTYPES, BATTLE_REQUEST_VERSION_ABILITY_HOOKS);
}

static void test_v12_wire(void) {
    static BattleRequest r, d;
    static uint8_t bytes[4096];
    make_hooks(&r, BATTLE_FORMAT_DOUBLE);
    give(&r.player[0], SCRAPPY);
    give(&r.player[1], SHED_SKIN);
    give(&r.opponent[0], UNSUPPORTED(104));
    give(&r.opponent[1], (BattleAbility){.cls = BACLASS_NONE, .source = 50});
    r.rules |= RULE_STRICT_ABILITIES | RULE_FIXED_SEED;
    uint32_t n = battle_request_encode(&r, bytes, sizeof(bytes));
    assert(n == battle_request_size(&r) && battle_request_decode(bytes, n, &d));
    assert(d.rules & RULE_FIXED_SEED);
    assert(d.player[0].mon.ability.cls == BACLASS_HOOKS && d.player[0].mon.ability.count == 2 &&
           d.player[0].mon.ability.hooks[1].kind == BABIL_VOLATILE_IMMUNE && d.player[0].mon.ability.source == 113);
    assert(d.player[1].mon.ability.hooks[0].chance == 1 && d.player[1].mon.ability.hooks[0].chance_den == 3);
    assert(d.opponent[0].mon.ability.cls == BACLASS_UNSUPPORTED && d.opponent[1].mon.ability.cls == BACLASS_NONE);
    // Block sizes: header 4 + 8 per hook: 4+16, 4+8, 4, 4.
    static BattleRequest v11;
    v11 = r;
    v11.version = BATTLE_REQUEST_VERSION_ABILITIES;
    v11.rules &= (uint16_t)~(RULE_STRICT_ABILITIES | RULE_FIXED_SEED);
    give(&v11.player[0], STATIC);
    give(&v11.player[1], STATIC);
    assert(battle_request_size(&r) == battle_request_size(&v11) - 4 * BATTLE_ABILITY_BYTES + 20 + 12 + 4 + 4);
    // Too many hooks, hooks on NONE, a later kind or flag in V11, rule bits below V12: all malformed.
    const uint32_t block = n - BATTLE_VOLATILE_RULE_BYTES - 40;
    assert(bytes[block] == BACLASS_HOOKS && bytes[block + 1] == 2);
    bytes[block + 1] = BATTLE_MAX_ABILITY_HOOKS + 1;
    assert(!battle_request_decode(bytes, n, &d));
    bytes[block + 1] = 2;
    {   // Five hooks claimed with enough bytes behind them: still refused before any is stored.
        static BattleRequest big;
        static uint8_t raw[4096];
        big = r;
        give(&big.player[1], HOOKS(61, hk(BABIL_STURDY, 0, 0, 0, 0, 0, 0, 0), hk(BABIL_NO_RECOIL, 0, 0, 0, 0, 0, 0, 0),
                                   hk(BABIL_NO_GUARD, 0, 0, 0, 0, 0, 0, 0), hk(BABIL_INFILTRATOR, 0, 0, 0, 0, 0, 0, 0)));
        const uint32_t m = battle_request_encode(&big, raw, sizeof(raw));
        const uint32_t at = m - BATTLE_VOLATILE_RULE_BYTES - (20 + 36 + 4 + 4);
        assert(m && battle_request_decode(raw, m, &d) && raw[at + 1] == 2);
        raw[at + 1] = BATTLE_MAX_ABILITY_HOOKS + 1;
        assert(!battle_request_decode(raw, m, &d));
    }
    bytes[block + 4 + 7] = BAHF_REQUIRES_STATE;  // a flag TYPE_PASS does not take
    assert(!battle_request_decode(bytes, n, &d));
    bytes[block + 4 + 7] = BAHF_BREAKABLE;       // carried for Mold Breaker: accepted
    assert(battle_request_decode(bytes, n, &d));
    bytes[block + 4 + 7] = 0;
    assert(!battle_request_decode(bytes, n - 1, &d));
    bytes[0] = BATTLE_REQUEST_VERSION_ABILITIES;
    assert(!battle_request_decode(bytes, n, &d));
    static BattleRequest v11r;
    v11r = v11;
    v11r.rules |= RULE_FIXED_SEED;
    assert(!battle_request_encode(&v11r, bytes, sizeof(bytes)));  // a V12 rule cannot be written as V11
    v11r = v11;
    give(&v11r.player[0], SCRAPPY);                                // two hooks cannot be written as V11
    assert(!battle_request_encode(&v11r, bytes, sizeof(bytes)));
    n = battle_request_encode(&v11, bytes, sizeof(bytes));
    bytes[6] |= RULE_FIXED_SEED;
    assert(n && !battle_request_decode(bytes, n, &d));
    // Hook rules.
    assert(hook12_ok(hk(BABIL_END_TURN_CURE, 0xfe, 0, 0, 1, 3, 0, 0)));
    assert(!hook12_ok(hk(BABIL_END_TURN_CURE, 0xfe, 0, 0, 0, 0, 0, 0)));   // needs a chance
    assert(!hook12_ok(hk(BABIL_END_TURN_CURE, 0xfe, 0, 0, 4, 3, 0, 0)));   // more than certain
    assert(!hook12_ok(hk(BABIL_STATUS_MIRROR, 1u << BST_SLEEP, 0, 0, 0, 0, 0, 0)));
    assert(!hook12_ok(hk(BABIL_ATTACK_MULT, 0, 0, T_FIRE, 0, 0, 150, 0)));  // a type without the filter flag
    assert(hook12_ok(hk(BABIL_ATTACK_MULT, 0, 0, T_FIRE, 0, 0, 150, BAHF_TYPE_FILTER)));
    assert(!battle_ability_hook_valid(&(BattleAbilityHook){.kind = BABIL_ATTACK_MULT, .percent = 150}, NTYPES,
                                      BATTLE_REQUEST_VERSION_ABILITIES));  // "any category" is V12
    assert(!battle_ability_hook_valid(&(BattleAbilityHook){.kind = BABIL_STAB_MULT, .percent = 200}, NTYPES,
                                      BATTLE_REQUEST_VERSION_ABILITIES));
    assert(!hook12_ok(hk(BABIL_ENTRY_STAT, BSTAT_ATK, -1, 2, 0, 0, 0, 0)));
    assert(strcmp(battle_ability_kind_name(BABIL_STATUS_MIRROR), "status_mirror") == 0);
    // V11 has one hook and no flags.
    BattleAbilityHook breakable = hk(BABIL_STURDY, 0, 0, 0, 0, 0, 0, BAHF_BREAKABLE);
    assert(hook12_ok(breakable) &&
           !battle_ability_hook_valid(&breakable, NTYPES, BATTLE_REQUEST_VERSION_ABILITIES));
    const BattleAbility two = HOOKS(5, hk(BABIL_STURDY, 0, 0, 0, 0, 0, 0, 0), hk(BABIL_NO_RECOIL, 0, 0, 0, 0, 0, 0, 0));
    assert(battle_ability_valid(&two, NTYPES, BATTLE_REQUEST_VERSION_ABILITY_HOOKS) &&
           !battle_ability_valid(&two, NTYPES, BATTLE_REQUEST_VERSION_ABILITIES));
}

static uint8_t atk_stages(const Battle *b, uint8_t side) {
    uint8_t mask = 0;
    for (uint8_t s = 0; s < b->sides[side].active_count; ++s)
        if (battle_at(b, (Position){side, s})->stat_stages[BSTAT_ATK] < 0) mask |= 1u << s;
    return mask;
}

// Gen 5/6 Triple: Intimidate reaches the adjacent foes only (left/right 2, center 3).
static void test_triple_range(void) {
    static BattleRequest r;
    static Battle b;
    static EventLog log;
    static const uint8_t expected[3] = {0x3, 0x7, 0x6};
    for (uint8_t slot = 0; slot < 3; ++slot) {
        make_hooks(&r, BATTLE_FORMAT_TRIPLE);
        give(&r.player[slot], INTIMIDATE_ADJ);
        assert(battle_init(&b, &r, 1) == BI_OK);
        battle_begin(&b, &log);
        assert(atk_stages(&b, 1) == expected[slot] && event_count(&log, EV_ABILITY) == 1);
        for (uint8_t s = 0; s < 3; ++s)
            assert(battle_in_range(&b, (Position){0, slot}, (Position){1, s}, BRANGE_ADJACENT_FOES) ==
                   ((expected[slot] >> s) & 1));
    }
    // Blockers apply only to the foes in range.
    make_hooks(&r, BATTLE_FORMAT_TRIPLE);
    give(&r.player[0], INTIMIDATE_ADJ);
    give(&r.opponent[1], INNER_FOCUS12);
    give(&r.opponent[2], INNER_FOCUS12);   // not adjacent: no block message either
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(atk_stages(&b, 1) == 0x1 && event_count(&log, EV_ABILITY_BLOCK) == 1 &&
           find(&log, EV_ABILITY_BLOCK)->user.slot == 1);
    // A switch-in on the right reaches center and right; a Triple Shift does not re-trigger.
    make_hooks(&r, BATTLE_FORMAT_TRIPLE);
    add_reserve(&r, 0);
    give(&r.player[3], INTIMIDATE_ADJ);
    give(&r.player[2], INTIMIDATE_ADJ);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    for (uint8_t s = 0; s < 3; ++s) ((Battler *)battle_at(&b, (Position){1, s}))->stat_stages[BSTAT_ATK] = 0;
    battle_run_turn(&b, (Action[]){{.kind = ACT_SHIFT, .user = {0, 2}}}, 1, &log);
    assert(find(&log, EV_SHIFT) && !find(&log, EV_ABILITY) && atk_stages(&b, 1) == 0);
    battle_run_turn(&b, (Action[]){{.kind = ACT_SWITCH, .user = {0, 2}, .reserve = 3}}, 1, &log);
    assert(find(&log, EV_SEND_OUT) && event_count(&log, EV_ABILITY) == 1 && atk_stages(&b, 1) == 0x6);
    // Single, Double and Rotation: every foe on the field, as before.
    const uint8_t formats[] = {BATTLE_FORMAT_SINGLE, BATTLE_FORMAT_DOUBLE, BATTLE_FORMAT_ROTATION};
    const uint8_t all[] = {0x1, 0x3, 0x7};
    for (unsigned f = 0; f < 3; ++f) {
        make_hooks(&r, formats[f]);
        give(&r.player[0], INTIMIDATE_ADJ);
        assert(battle_init(&b, &r, 1) == BI_OK);
        battle_begin(&b, &log);
        assert(atk_stages(&b, 1) == all[f]);
    }
    // V11 (no range field) keeps the M4-A behaviour: all three Triple foes.
    make_abilities(&r, BATTLE_FORMAT_TRIPLE);
    give(&r.player[0], INTIMIDATE);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(atk_stages(&b, 1) == 0x7);
}

static void test_guts_marvel_adaptability(void) {
    static BattleRequest r, plain;
    Battle b, p;
    uint8_t eff;
    make_hooks(&r, BATTLE_FORMAT_SINGLE);
    plain = r;
    const BattleMove phys = damaging(40, T_FIRE, 0);
    BattleMove spec = phys;
    spec.category = BCAT_SPECIAL;
    give(&r.player[0], GUTS);
    assert(battle_init(&b, &r, 1) == BI_OK && battle_init(&p, &plain, 1) == BI_OK);
    Battler *a = battle_active(&b, 0), *pa = battle_active(&p, 0), *d = battle_active(&b, 1), *pd = battle_active(&p, 1);
    // No status: no change.
    assert(battle_damage(&b, a, &phys, d, 100, &eff) == battle_damage(&p, pa, &phys, pd, 100, &eff));
    // Paralysed: physical x1.5, special unchanged.
    a->status = BST_PARALYSIS;
    pa->attack = 90;
    assert(battle_damage(&b, a, &phys, d, 100, &eff) == battle_damage(&p, pa, &phys, pd, 100, &eff));
    assert(battle_damage(&b, a, &spec, d, 100, &eff) == battle_damage(&p, pa, &spec, pd, 100, &eff));
    // Burned: x1.5 and no halving (the plain attacker is unburned).
    a->status = BST_BURN;
    assert(battle_damage(&b, a, &phys, d, 100, &eff) == battle_damage(&p, pa, &phys, pd, 100, &eff));
    pa->status = BST_BURN;
    pa->attack = 60;
    assert(battle_damage(&b, a, &phys, d, 100, &eff) > battle_damage(&p, pa, &phys, pd, 100, &eff));
    pa->status = 0;
    // Marvel Scale: the holder's Defense x1.5 against physical moves while statused.
    give(&r.player[0], (BattleAbility){0});
    give(&r.opponent[0], MARVEL_SCALE);
    assert(battle_init(&b, &r, 1) == BI_OK);
    a = battle_active(&b, 0);
    d = battle_active(&b, 1);
    assert(battle_damage(&b, a, &phys, d, 100, &eff) == battle_damage(&p, pa, &phys, pd, 100, &eff));
    d->status = BST_POISON;
    pd->defense = 90;
    assert(battle_damage(&b, a, &phys, d, 100, &eff) == battle_damage(&p, pa, &phys, pd, 100, &eff));
    pd->defense = 60;
    assert(battle_damage(&b, a, &spec, d, 100, &eff) == battle_damage(&p, pa, &spec, pd, 100, &eff));
    // Adaptability: same-type bonus x2 instead of x1.5.
    give(&r.opponent[0], (BattleAbility){0});
    give(&r.player[0], ADAPTABILITY);
    assert(battle_init(&b, &r, 1) == BI_OK);
    a = battle_active(&b, 0);
    d = battle_active(&b, 1);
    const BattleMove stab = damaging(40, T_NORMAL, 0), other = damaging(40, T_GHOST, 0);
    const uint16_t none = battle_damage(&b, a, &other, d, 100, &eff);
    assert(battle_damage(&b, a, &stab, d, 100, &eff) == 2 * none);
    assert(battle_damage(&p, pa, &stab, pd, 100, &eff) == none * 3 / 2);
}

static void test_moxie(void) {
    static BattleRequest r;
    Battle b;
    EventLog log;
    make_hooks(&r, BATTLE_FORMAT_DOUBLE);
    add_reserve(&r, 1);
    give(&r.player[0], MOXIE);
    BattleMove spread = damaging(250, T_NORMAL, 0);
    spread.target = BTARGET_ALL_FOES;
    set_move(&r.player[0], 1, 57, spread, 10);
    set_move(&r.player[0], 2, 33, damaging(250, T_NORMAL, 0), 10);
    // Two foes knocked out by one move: +2 at once, after the move.
    for (unsigned i = 0; i < 2; ++i) r.opponent[i].record.hp = 10;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(event_count(&log, EV_FAINT) == 2 && battle_active(&b, 0)->stat_stages[BSTAT_ATK] == 2);
    assert(index_of(&log, EV_ABILITY, 0) > index_of(&log, EV_FAINT, 1));
    // One knocked out: +1. None: nothing.
    r.opponent[1].record.hp = 200;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(battle_active(&b, 0)->stat_stages[BSTAT_ATK] == 1);
    r.opponent[0].record.hp = 200;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 2)}, 1, &log);
    assert(!find(&log, EV_ABILITY) && battle_active(&b, 0)->stat_stages[BSTAT_ATK] == 0);
    // A substitute takes the hit: no knock-out, nothing.
    r.opponent[0].record.hp = 10;
    assert(battle_init(&b, &r, 1) == BI_OK);
    ((Battler *)battle_at(&b, (Position){1, 0}))->vol.substitute_hp = 50;
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 2)}, 1, &log);
    assert(!find(&log, EV_FAINT) && !find(&log, EV_ABILITY));
    // The last foe goes down: the battle is decided, nothing.
    make_hooks(&r, BATTLE_FORMAT_SINGLE);
    give(&r.player[0], MOXIE);
    set_move(&r.player[0], 1, 33, damaging(250, T_NORMAL, 0), 10);
    r.opponent[0].record.hp = 10;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(find(&log, EV_FAINT) && !find(&log, EV_ABILITY) && b.outcome == OUTCOME_WIN);
    // Recoil knocks the user out first: nothing.
    add_reserve(&r, 1);
    BattleMove edge = damaging(250, T_NORMAL, 0);
    push(&edge, fx(BEFF_RECOIL, BWHO_USER, 0, 50, 0, 0));
    set_move(&r.player[0], 1, 38, edge, 10);
    r.player[0].record.hp = 5;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(find(&log, EV_RECOIL) && !find(&log, EV_ABILITY));
}

static void test_shed_skin(void) {
    static BattleRequest r;
    Battle b;
    EventLog log;
    make_hooks(&r, BATTLE_FORMAT_SINGLE);
    give(&r.player[0], SHED_SKIN);
    r.player[0].record.status = BST_BURN;
    unsigned cured = 0, kept = 0;
    for (uint32_t seed = 1; seed <= 60; ++seed) {
        assert(battle_init(&b, &r, seed) == BI_OK);
        battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 0)}, 1, &log);
        const BattleEvent *cure = find(&log, EV_CURE);
        if (cure) {
            ++cured;
            // Before the residual damage: a cured holder takes none that turn.
            assert(!find(&log, EV_RESIDUAL) && index_of(&log, EV_ABILITY, 0) + 1 == index_of(&log, EV_CURE, 0));
            assert(!battle_active(&b, 0)->status);
        } else {
            ++kept;
            assert(find(&log, EV_RESIDUAL) && !find(&log, EV_ABILITY));
        }
    }
    assert(cured > 5 && kept > cured);  // about one in three
    // A status outside the hook's mask is never cured.
    r.player[0].mon.ability.hooks[0].arg0 = 1u << BST_PARALYSIS;
    for (uint32_t seed = 1; seed <= 30; ++seed) {
        assert(battle_init(&b, &r, seed) == BI_OK);
        battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 0)}, 1, &log);
        assert(!find(&log, EV_CURE) && find(&log, EV_RESIDUAL));
    }
    r.player[0].mon.ability.hooks[0].arg0 = 0xfe;
    // No status: no roll, nothing (the same seed then runs exactly like a plain battle).
    static BattleRequest plain;
    r.player[0].record.status = 0;
    plain = r;
    give(&plain.player[0], (BattleAbility){0});
    Battle p;
    EventLog pl;
    assert(battle_init(&b, &r, 7) == BI_OK && battle_init(&p, &plain, 7) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 0)}, 1, &log);
    battle_run_turn(&p, (Action[]){slot_move(0, 0, 0, 0)}, 1, &pl);
    assert(b.rng == p.rng && log.count == pl.count);
}

static void test_flash_fire(void) {
    static BattleRequest r;
    Battle b;
    EventLog log;
    uint8_t eff;
    make_hooks(&r, BATTLE_FORMAT_SINGLE);
    add_reserve(&r, 1);
    give(&r.opponent[0], FLASH_FIRE);
    give(&r.opponent[1], FLASH_FIRE);
    set_move(&r.player[0], 1, 52, damaging(40, T_FIRE, 0), 10);
    BattleMove wisp = effect_move(BTARGET_SINGLE, 0, fx(BEFF_STATUS, BWHO_TARGET, 0, BST_BURN, 0, 0));
    wisp.type = T_FIRE;
    set_move(&r.player[0], 2, 261, wisp, 10);
    set_move(&r.opponent[0], 1, 53, damaging(40, T_FIRE, 0), 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    Battler *holder = battle_active(&b, 1);
    const BattleMove *fire = &holder->moves[1];
    const uint16_t before = battle_damage(&b, holder, fire, battle_active(&b, 0), 100, &eff);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(find(&log, EV_ABILITY) && !find(&log, EV_DAMAGE) && holder->vol.ability_state == 1);
    assert(battle_damage(&b, holder, fire, battle_active(&b, 0), 100, &eff) > before);
    const uint16_t boosted = battle_damage(&b, holder, fire, battle_active(&b, 0), 100, &eff);
    // A Fire status move is absorbed too; the boost does not stack.
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 2)}, 1, &log);
    assert(find(&log, EV_ABILITY) && !holder->status &&
           battle_damage(&b, holder, fire, battle_active(&b, 0), 100, &eff) == boosted);
    // The holder's other moves are not boosted; a switch-out clears the state.
    const uint16_t normal_on = battle_damage(&b, holder, &holder->moves[0], battle_active(&b, 0), 100, &eff);
    holder->vol.ability_state = 0;
    assert(battle_damage(&b, holder, &holder->moves[0], battle_active(&b, 0), 100, &eff) == normal_on);
    holder->vol.ability_state = 1;
    battle_run_turn(&b, (Action[]){{.kind = ACT_SWITCH, .user = {1, 0}, .reserve = 1}}, 1, &log);
    assert(!b.sides[1].battlers[0].vol.ability_state);
    // With the old freeze rule (BAHF_NOT_FROZEN), a frozen holder takes the hit.
    r.opponent[0].mon.ability.hooks[0].flags = BAHF_NOT_FROZEN;
    r.opponent[0].record.status = BST_FREEZE;
    r.conditions.flags &= (uint8_t)~BCR_THAW_ON_FIRE_HIT;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(find(&log, EV_DAMAGE) && !find(&log, EV_ABILITY) && !battle_active(&b, 1)->vol.ability_state);
}

static void test_scrappy(void) {
    static BattleRequest r;
    Battle b;
    EventLog log;
    uint8_t eff;
    make_hooks(&r, BATTLE_FORMAT_SINGLE);
    r.opponent[0].mon.types[0] = r.opponent[0].mon.types[1] = T_GHOST;
    give(&r.player[0], SCRAPPY);
    assert(battle_init(&b, &r, 1) == BI_OK);
    const BattleMove normal = damaging(40, T_NORMAL, 0), fire = damaging(40, T_FIRE, 0);
    assert(battle_damage(&b, battle_active(&b, 0), &normal, battle_active(&b, 1), 100, &eff) && eff == 4);
    assert(battle_damage(&b, battle_active(&b, 0), &fire, battle_active(&b, 1), 100, &eff) && eff == 4);
    // Only the Ghost part changes: Ghost/Water keeps Water's multiplier.
    battle_active(&b, 1)->type_count = 2;
    battle_active(&b, 1)->types[1] = T_WATER;
    const BattleMove water = damaging(40, T_WATER, 0);
    battle_damage(&b, battle_active(&b, 0), &normal, battle_active(&b, 1), 100, &eff);
    assert(eff == 4);
    (void)water;
    // Another immunity of a Normal move (Water here) stays: only the hook's defending type passes.
    r.type_chart[T_NORMAL * NTYPES + T_WATER] = 0;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_active(&b, 1)->types[0] = battle_active(&b, 1)->types[1] = T_WATER;
    battle_damage(&b, battle_active(&b, 0), &normal, battle_active(&b, 1), 100, &eff);
    assert(eff == 0);
    r.type_chart[T_NORMAL * NTYPES + T_WATER] = 4;
    // Without Scrappy: no effect. Its second hook blocks Intimidate.
    give(&r.player[0], (BattleAbility){0});
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_damage(&b, battle_active(&b, 0), &normal, battle_active(&b, 1), 100, &eff);
    assert(eff == 0);
    give(&r.player[0], SCRAPPY);
    give(&r.opponent[0], INTIMIDATE_ADJ);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(find(&log, EV_ABILITY_BLOCK) && battle_active(&b, 0)->stat_stages[BSTAT_ATK] == 0);
}

static void test_synchronize(void) {
    static BattleRequest r;
    Battle b;
    EventLog log;
    make_hooks(&r, BATTLE_FORMAT_SINGLE);
    give(&r.player[0], SYNCHRONIZE);
    set_move(&r.opponent[0], 1, 86, effect_move(BTARGET_SINGLE, 0, fx(BEFF_STATUS, BWHO_TARGET, 0, BST_PARALYSIS, 0, 0)), 10);
    set_move(&r.opponent[0], 2, 92, effect_move(BTARGET_SINGLE, 0, fx(BEFF_STATUS, BWHO_TARGET, 0, BST_TOXIC, 0, 0)), 10);
    set_move(&r.opponent[0], 3, 95, effect_move(BTARGET_SINGLE, 0, fx(BEFF_STATUS, BWHO_TARGET, 0, BST_SLEEP, 0, 0)), 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(1, 0, 0, 1)}, 1, &log);
    assert(event_count(&log, EV_STATUS) == 2 && BATTLE_CONDITION_KIND(battle_active(&b, 1)->status) == BST_PARALYSIS);
    assert(index_of(&log, EV_ABILITY, 0) == index_of(&log, EV_STATUS, 0) + 1);
    // Bad poison goes back as bad poison, or as poison under the old rule.
    for (unsigned old = 0; old < 2; ++old) {
        r.player[0].mon.ability.hooks[0].flags = old ? BAHF_TOXIC_TO_POISON : 0;
        assert(battle_init(&b, &r, 1) == BI_OK);
        battle_run_turn(&b, (Action[]){slot_move(1, 0, 0, 2)}, 1, &log);
        assert(BATTLE_CONDITION_KIND(battle_active(&b, 1)->status) == (old ? BST_POISON : BST_TOXIC));
    }
    r.player[0].mon.ability.hooks[0].flags = 0;
    // Sleep is not reflected.
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(1, 0, 0, 3)}, 1, &log);
    assert(event_count(&log, EV_STATUS) == 1 && !find(&log, EV_ABILITY) && !battle_active(&b, 1)->status);
    // The source cannot take it (Electric type under the rule): shown, nothing set.
    r.opponent[0].mon.types[0] = r.opponent[0].mon.types[1] = T_WATER;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(1, 0, 0, 1)}, 1, &log);
    assert(find(&log, EV_ABILITY) && event_count(&log, EV_STATUS) == 1 && !battle_active(&b, 1)->status);
    r.opponent[0].mon.types[0] = r.opponent[0].mon.types[1] = T_NORMAL;
    // Both sides with Synchronize: the status comes back once, not again.
    give(&r.opponent[0], SYNCHRONIZE);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(1, 0, 0, 1)}, 1, &log);
    assert(event_count(&log, EV_STATUS) == 2 && event_count(&log, EV_ABILITY) == 1);
    // A contact status from Static goes back to the Static holder.
    make_hooks(&r, BATTLE_FORMAT_SINGLE);
    give(&r.player[0], SYNCHRONIZE);
    give(&r.opponent[0], ab(BABIL_CONTACT_STATUS, BST_PARALYSIS, 0, 0, 100, 0, 9));
    set_move(&r.player[0], 1, 24, contact(10, 1), 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    assert(BATTLE_CONDITION_KIND(battle_active(&b, 0)->status) == BST_PARALYSIS &&
           BATTLE_CONDITION_KIND(battle_active(&b, 1)->status) == BST_PARALYSIS);
}

static void test_strict_mode(void) {
    static BattleRequest r;
    Battle b;
    EventLog log;
    make_hooks(&r, BATTLE_FORMAT_DOUBLE);
    give(&r.opponent[1], UNSUPPORTED(104));
    assert(battle_init(&b, &r, 1) == BI_OK);          // development: runs, with the notice
    battle_begin(&b, &log);
    assert(event_count(&log, EV_ABILITY_UNSUPPORTED) == 1);
    r.rules |= RULE_STRICT_ABILITIES;
    assert(battle_init(&b, &r, 1) == BI_UNSUPPORTED_ABILITY);
    assert(strcmp(battle_init_error_name(BI_UNSUPPORTED_ABILITY), "strict rules: an ability is not simulated") == 0);
    give(&r.opponent[1], (BattleAbility){.cls = BACLASS_NONE, .source = 50});  // NONE is not UNSUPPORTED
    assert(battle_init(&b, &r, 1) == BI_OK);
    give(&r.opponent[1], UNSUPPORTED(104));
    r.rules |= RULE_IGNORE_ABILITIES;                  // abilities normalised away: nothing to refuse
    assert(battle_init(&b, &r, 1) == BI_OK);
}

// The same request and seed give the same battle, event for event (fixtures rely on it).
static void test_v12_determinism(void) {
    static BattleRequest r;
    static Battle a, b;
    static EventLog la, lb;
    const BattleAbility pool[] = {GUTS, MARVEL_SCALE, ADAPTABILITY, MOXIE, SHED_SKIN, FLASH_FIRE, SCRAPPY, SYNCHRONIZE,
                                  INTIMIDATE_ADJ, ROUGH_SKIN, STATIC, STURDY, UNSUPPORTED(104)};
    const unsigned n = sizeof(pool) / sizeof(*pool);
    const uint8_t formats[] = {BATTLE_FORMAT_SINGLE, BATTLE_FORMAT_DOUBLE, BATTLE_FORMAT_TRIPLE, BATTLE_FORMAT_ROTATION};
    unsigned ability_events = 0;
    for (unsigned f = 0; f < 4; ++f)
        for (uint32_t seed = 1; seed <= 40; ++seed) {
            make_hooks(&r, formats[f]);
            for (unsigned side = 0; side < 2; ++side) add_reserve(&r, side);
            for (unsigned side = 0; side < 2; ++side) {
                BattleEntry *e = side ? r.opponent : r.player;
                const unsigned count = side ? r.opponent_count : r.player_count;
                for (unsigned i = 0; i < count; ++i) {
                    give(&e[i], pool[(seed * 5 + side * 3 + i + f) % n]);
                    e[i].record.hp = e[i].record.max_hp = 50;
                    e[i].record.status = (seed + i) % 4 == 0 ? BST_BURN : 0;
                    BattleMove m = contact(30, i == 1 ? 2 : 1);
                    m.type = (uint8_t)((seed + i) % 3);
                    push(&m, fx(BEFF_STATUS, BWHO_TARGET, 30, BST_PARALYSIS, 0, 0));
                    set_move(&e[i], 1, 200 + i, m, 30);
                    BattleMove spread = damaging(45, T_FIRE, 95);
                    spread.target = BTARGET_ALL_FOES;
                    set_move(&e[i], 2, 300 + i, spread, 30);
                }
            }
            assert(battle_init(&a, &r, seed) == BI_OK && battle_init(&b, &r, seed) == BI_OK);
            battle_begin(&a, &la);
            battle_begin(&b, &lb);
            for (unsigned t = 0; t < 50 && a.outcome == OUTCOME_NONE; ++t) {
                Position pending;
                while (battle_pending_replacement(&a, &pending)) {
                    const uint8_t mask = battle_replacement_mask(&a, pending);
                    uint8_t pick = 0;
                    while (!(mask & (1u << pick))) ++pick;
                    assert(battle_replace(&a, pending, pick, &la) && battle_replace(&b, pending, pick, &lb));
                }
                Action actions[BC_MAX_ACTIONS];
                unsigned count = 0;
                for (uint8_t side = 0; side < 2; ++side) {
                    Position actors[BC_MAX_ACTIVE];
                    const unsigned k = battle_collect_actors(&a, side, actors);
                    for (unsigned i = 0; i < k; ++i) battle_choose_at(&a, actors[i], &actions[count++]);
                }
                for (unsigned i = 0; i < count; ++i) { Action x; battle_choose_at(&b, actions[i].user, &x); }
                battle_run_turn(&a, actions, count, &la);
                battle_run_turn(&b, actions, count, &lb);
                assert(la.count == lb.count && la.count < BC_MAX_EVENTS &&
                       memcmp(la.events, lb.events, la.count * sizeof(BattleEvent)) == 0);
                ability_events += event_count(&la, EV_ABILITY) + event_count(&la, EV_ABILITY_BLOCK);
                for (unsigned side = 0; side < 2; ++side)
                    for (unsigned i = 0; i < a.sides[side].count; ++i) {
                        const Battler *x = &a.sides[side].battlers[i];
                        assert(x->hp <= x->max_hp && (x->hp == 0) == x->fainted);
                    }
            }
            assert(memcmp(&a.sides, &b.sides, sizeof(a.sides)) == 0 && a.rng == b.rng);
        }
    assert(ability_events > 100);
    printf("v12 determinism: %u ability events\n", ability_events);
}

// ------------------------------------------------------------- V13 modifier records (docs/rule-poc.md)

static BattleModifier modr(uint8_t stage, uint8_t percent, uint8_t k0, uint8_t a0, uint8_t k1, uint8_t a1) {
    return (BattleModifier){.stage = stage, .percent = percent, .pred = {{k0, a0}, {k1, a1}}};
}
static BattleEffect mod_effect(BattleModifier m) {
    BattleEffect e = {.kind = BEFF_MODIFIER};
    e.mod = m;
    return e;
}
static BattleAbilityHook mod_hook(BattleModifier m) {
    BattleAbilityHook h = {.kind = BABIL_MODIFIER};
    h.mod = m;
    return h;
}
static BattleAbility mod_ability(uint16_t source, unsigned n, const BattleModifier *list) {
    BattleAbility a = {.cls = BACLASS_HOOKS, .count = (uint8_t)n, .source = source};
    for (unsigned i = 0; i < n; ++i) a.hooks[i] = mod_hook(list[i]);
    return a;
}
#define MODS(source, ...) mod_ability(source, sizeof((BattleModifier[]){__VA_ARGS__}) / sizeof(BattleModifier), \
                                      (BattleModifier[]){__VA_ARGS__})
#define ANY_STATUS 0xfeu
#define GUTS_ONLY HOOKS(62, hk(BABIL_ATTACK_MULT, BCAT_PHYSICAL, 0, 0, 0, 0, 150, BAHF_REQUIRES_STATUS))
#define ST(stage, category) BATTLE_MODIFIER_STAGE(stage, category)
// Hex: base power x2 while the target has a major status.
#define HEX_MOD modr(BMOD_BASE_POWER, 200, BPRED_STATUS | BPRED_DEFENDER, ANY_STATUS, 0, 0)
// The PoC custom ability: physical power x2 while the holder is at half HP or
// less AND has a major status (V12 could not combine these two conditions).
#define CUSTOM_MOD modr(ST(BMOD_POWER, BCAT_PHYSICAL), 200, BPRED_HP_AT_MOST, 2, BPRED_STATUS, ANY_STATUS)

static void make_mods(BattleRequest *r, uint8_t format) {
    make_hooks(r, format);
    r->version = BATTLE_REQUEST_VERSION_MODIFIERS;
}

static BattleMove with_mod(BattleMove m, BattleModifier x) {
    push(&m, mod_effect(x));
    return m;
}

static uint16_t dmg_at(Battle *b, const BattleMove *m, unsigned random_percent) {
    uint8_t eff;
    return battle_damage(b, battle_active(b, 0), m, battle_active(b, 1), random_percent, &eff);
}
static uint16_t dmg(Battle *b, const BattleMove *m) { return dmg_at(b, m, 100); }

static bool mod_ok(BattleModifier m, bool ability) {
    return battle_modifier_valid(&m, NTYPES, ability ? BMSRC_ABILITY : BMSRC_MOVE, BATTLE_REQUEST_VERSION_FIXED_POINT);
}

static void test_v13_validation(void) {
    // Valid records.
    assert(mod_ok(HEX_MOD, false) && mod_ok(CUSTOM_MOD, true) && mod_ok(HEX_MOD, true));
    assert(mod_ok(modr(BMOD_FINAL, 1, 0, 0, 0, 0), false));
    assert(mod_ok(modr(ST(BMOD_DEFENSE, BCAT_SPECIAL) | BMODF_DEFENDER, 255, BPRED_MOVE_TYPE, T_GHOST, 0, 0), true));
    // Stage, category, reserved bits, percent.
    assert(!mod_ok(modr(BMOD_NONE, 100, 0, 0, 0, 0), true));
    assert(!mod_ok(modr(BMOD_V14_COUNT, 100, 0, 0, 0, 0), true) && !mod_ok(modr(7, 100, 0, 0, 0, 0), true));
    assert(!mod_ok(modr(ST(BMOD_POWER, 3), 100, 0, 0, 0, 0), true));
    assert(!mod_ok(modr(BMOD_POWER | 0x20, 100, 0, 0, 0, 0), true) && !mod_ok(modr(BMOD_POWER | 0x40, 100, 0, 0, 0, 0), true));
    assert(!mod_ok(modr(BMOD_POWER, 0, 0, 0, 0, 0), true));
    // Moves: no category, no holder role, no move predicates (the GBA knows its own move).
    assert(!mod_ok(modr(ST(BMOD_POWER, BCAT_PHYSICAL), 150, 0, 0, 0, 0), false));
    assert(!mod_ok(modr(BMOD_POWER | BMODF_DEFENDER, 150, 0, 0, 0, 0), false));
    assert(!mod_ok(modr(BMOD_POWER, 150, BPRED_MOVE_TYPE, T_FIRE, 0, 0), false));
    assert(!mod_ok(modr(BMOD_POWER, 150, BPRED_POWER_AT_MOST, 60, 0, 0), false));
    // Predicates: unknown kinds, reserved bit, arguments, packing.
    assert(!mod_ok(modr(BMOD_POWER, 150, BPRED_V14_COUNT, 1, 0, 0), true));
    assert(!mod_ok(modr(BMOD_POWER, 150, 0x3f, 1, 0, 0), true));
    assert(!mod_ok(modr(BMOD_POWER, 150, BPRED_STATUS | 0x40, ANY_STATUS, 0, 0), true));
    assert(!mod_ok(modr(BMOD_POWER, 150, BPRED_HP_AT_MOST, 1, 0, 0), true));
    assert(!mod_ok(modr(BMOD_POWER, 150, BPRED_STATUS, 0, 0, 0), true));
    assert(!mod_ok(modr(BMOD_POWER, 150, BPRED_STATUS, 0x01, 0, 0), true));
    assert(!mod_ok(modr(BMOD_POWER, 150, BPRED_MOVE_TYPE, NTYPES, 0, 0), true));
    assert(!mod_ok(modr(BMOD_POWER, 150, BPRED_MOVE_TYPE | BPRED_DEFENDER, T_FIRE, 0, 0), true));
    assert(!mod_ok(modr(BMOD_POWER, 150, BPRED_POWER_AT_MOST, 0, 0, 0), true));
    assert(!mod_ok(modr(BMOD_BASE_POWER, 150, BPRED_POWER_AT_MOST, 60, 0, 0), true));
    assert(!mod_ok(modr(BMOD_POWER, 150, 0, 0, BPRED_STATUS, ANY_STATUS), true));
    assert(!mod_ok(modr(BMOD_POWER, 150, 0, 1, 0, 0), true) && !mod_ok(modr(BMOD_POWER, 150, BPRED_DEFENDER, 0, 0, 0), true));
    // A hook: the kind is V13 only; of the slot flags only BAHF_BREAKABLE (wire byte 3).
    BattleAbilityHook h = mod_hook(CUSTOM_MOD);
    assert(battle_ability_hook_valid(&h, NTYPES, BATTLE_REQUEST_VERSION_MODIFIERS));
    assert(!battle_ability_hook_valid(&h, NTYPES, BATTLE_REQUEST_VERSION_ABILITY_HOOKS));
    h.flags = BAHF_BREAKABLE;
    assert(battle_ability_hook_valid(&h, NTYPES, BATTLE_REQUEST_VERSION_MODIFIERS));
    for (unsigned bit = 0; bit < 7; ++bit) {
        h.flags = (uint8_t)(1u << bit);
        assert(!battle_ability_hook_valid(&h, NTYPES, BATTLE_REQUEST_VERSION_MODIFIERS));
    }
    h = mod_hook(CUSTOM_MOD);
    h = (BattleAbilityHook){.kind = BABIL_COUNT, .percent = 100};
    assert(!battle_ability_hook_valid(&h, NTYPES, BATTLE_REQUEST_VERSION_MODIFIERS));
    // An effect: damaging moves only, no slot flags (no timing, no type chart).
    BattleMove hex = with_mod(damaging(65, T_FIRE, 100), HEX_MOD);
    assert(battle_move_effects_valid(&hex));
    for (unsigned bit = 0; bit < 8; ++bit) {
        hex.effects[1].flags = (uint8_t)(1u << bit);
        assert(!battle_move_effects_valid(&hex));
    }
    hex.effects[1].flags = 0;
    // The record shares the legacy fields' storage: 8 bytes, as on the wire.
    assert(sizeof(BattleEffect) == BATTLE_EFFECT_BYTES && sizeof(BattleAbilityHook) == BATTLE_ABILITY_HOOK_BYTES);
    BattleMove status = status_move(BTARGET_SINGLE, 100);
    push(&status, fx(BEFF_STATUS, BWHO_TARGET, 0, BST_PARALYSIS, 0, 0));
    push(&status, mod_effect(HEX_MOD));
    assert(!battle_move_effects_valid(&status));
}

static void test_v13_wire(void) {
    static BattleRequest r, d;
    static uint8_t bytes[4096];
    make_mods(&r, BATTLE_FORMAT_DOUBLE);
    set_move(&r.player[0], 0, 506, with_mod(damaging(65, T_FIRE, 100), HEX_MOD), 10);
    BattleAbility custom = MODS(250, CUSTOM_MOD);
    custom.hooks[0].flags = BAHF_BREAKABLE;
    give(&r.player[1], custom);
    give(&r.opponent[0], GUTS);  // V12 kinds stay valid in V13
    uint32_t n = battle_request_encode(&r, bytes, sizeof bytes);
    assert(n == battle_request_size(&r) && bytes[0] == BATTLE_REQUEST_VERSION_MODIFIERS);
    assert(battle_request_decode(bytes, n, &d));
    const BattleEffect *e = &d.player[0].mon.moves[0].effects[1];
    assert(e->kind == BEFF_MODIFIER && !e->flags &&
           memcmp(&e->mod, &r.player[0].mon.moves[0].effects[1].mod, sizeof e->mod) == 0);
    const BattleAbilityHook *h = &d.player[1].mon.ability.hooks[0];
    assert(h->kind == BABIL_MODIFIER && h->flags == BAHF_BREAKABLE &&
           memcmp(&h->mod, &custom.hooks[0].mod, sizeof h->mod) == 0);
    Battle b;
    assert(battle_init(&b, &d, 1) == BI_OK);
    // The record bytes: kind, then the body (Hex is the first move's second effect).
    const uint32_t fixed = BATTLE_REQUEST_HEADER + 4 * BATTLE_ENTRY_BYTES + NTYPES * NTYPES + BATTLE_CONDITION_RULE_BYTES;
    uint8_t *q = bytes + fixed + BATTLE_MOVE_EFFECTS_HEADER + BATTLE_EFFECT_BYTES;
    assert(q[0] == BEFF_MODIFIER && q[1] == BMOD_BASE_POWER && q[2] == 200 && q[3] == 0 &&
           q[4] == (BPRED_STATUS | BPRED_DEFENDER) && q[5] == ANY_STATUS && !q[6] && !q[7]);
    // Unknown or reserved values make the request malformed.
    const uint8_t keep[8] = {q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7]};
    const struct { unsigned at; uint8_t value; } bad[] = {
        {1, 6}, {1, BMOD_BASE_POWER | 1u << BMOD_CATEGORY_SHIFT}, {1, BMOD_BASE_POWER | BMODF_DEFENDER}, {2, 0},
        {3, BAHF_BREAKABLE}, {4, BPRED_COUNT}, {4, BPRED_STATUS | 0x40}, {4, BPRED_MOVE_TYPE}, {5, 1}, {6, BPRED_COUNT + 1},
    };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; ++i) {
        q[bad[i].at] = bad[i].value;
        assert(!battle_request_decode(bytes, n, &d));
        memcpy(q, keep, sizeof keep);
    }
    assert(battle_request_decode(bytes, n, &d));
    // Labelled V12, the same bytes are malformed (V13 records are not V12 data),
    // also with only the move record.
    bytes[0] = BATTLE_REQUEST_VERSION_ABILITY_HOOKS;
    assert(!battle_request_decode(bytes, n, &d));
    {
        static BattleRequest only_move;
        static uint8_t raw[4096];
        only_move = r;
        give(&only_move.player[1], (BattleAbility){0});
        const uint32_t k = battle_request_encode(&only_move, raw, sizeof raw);
        assert(k && battle_request_decode(raw, k, &d));
        raw[0] = BATTLE_REQUEST_VERSION_ABILITY_HOOKS;
        assert(!battle_request_decode(raw, k, &d));
    }
    // V14 has the V13 layout (only the damage arithmetic differs); later versions are unknown.
    bytes[0] = BATTLE_REQUEST_VERSION_FIXED_POINT;
    assert(battle_request_decode(bytes, n, &d) && d.version == BATTLE_REQUEST_VERSION_FIXED_POINT &&
           d.player[1].mon.ability.hooks[0].flags == BAHF_BREAKABLE);
    bytes[0] = BATTLE_REQUEST_VERSION_LATEST + 1;
    assert(!battle_request_decode(bytes, n, &d));
    // The encoder refuses V13 records in an earlier request (moves and abilities).
    r.version = BATTLE_REQUEST_VERSION_ABILITY_HOOKS;
    assert(battle_request_encode(&r, bytes, sizeof bytes) == 0);
    give(&r.player[1], (BattleAbility){0});
    assert(battle_request_encode(&r, bytes, sizeof bytes) == 0);
    set_move(&r.player[0], 0, 506, damaging(65, T_FIRE, 100), 10);
    give(&r.player[1], custom);
    assert(battle_request_encode(&r, bytes, sizeof bytes) == 0);
    give(&r.player[1], (BattleAbility){0});
    assert(battle_request_encode(&r, bytes, sizeof bytes) != 0);
    // A natively built V12 request with a V13 record: the move is not supported.
    BattleMove hex = with_mod(damaging(65, T_FIRE, 100), HEX_MOD);
    assert(!battle_move_supported(&r, &hex));
    r.version = BATTLE_REQUEST_VERSION_MODIFIERS;
    assert(battle_move_supported(&r, &hex));
}

// The largest request equals BATTLE_REQUEST_MAX_BYTES, fits a buffer of that
// size and not one byte less (the Frontier buffer is sized with the macro).
static void build_largest(BattleRequest *r, uint8_t format, unsigned per_side, uint8_t version) {
    make_mods(r, format);
    r->version = version;
    for (unsigned side = 0; side < 2; ++side) {
        BattleEntry *entries = side ? r->opponent : r->player;
        for (unsigned i = 0; i < per_side; ++i) {
            if (i >= r->active_per_side) make_entry(&entries[i], (uint8_t)(0x40 + 8 * side + i), 9, 50, 100, 60, 50, T_NORMAL);
            for (unsigned m = 0; m < 4; ++m) {
                BattleMove move = damaging(40, T_NORMAL, 100);
                for (unsigned k = 1; k < BATTLE_MAX_EFFECTS; ++k)
                    push(&move, version >= BATTLE_REQUEST_VERSION_MODIFIERS
                                    ? mod_effect(modr(BMOD_POWER, (uint8_t)(100 + k), BPRED_STATUS, ANY_STATUS, 0, 0))
                                    : fx(BEFF_STAT, BWHO_TARGET, 10, BSTAT_DEF, -1, 0));
                set_move(&entries[i], m, (uint16_t)(100 + m), move, 10);
            }
            BattleAbility a = {.cls = BACLASS_HOOKS, .count = BATTLE_MAX_ABILITY_HOOKS, .source = 300};
            for (unsigned k = 0; k < BATTLE_MAX_ABILITY_HOOKS; ++k)
                a.hooks[k] = version >= BATTLE_REQUEST_VERSION_MODIFIERS
                                 ? mod_hook(modr(BMOD_ATTACK, (uint8_t)(101 + k), BPRED_HP_AT_MOST, 2, 0, 0))
                                 : hk(BABIL_ATTACK_MULT, BCAT_PHYSICAL, 0, 0, 0, 0, (uint8_t)(101 + k), 0);
            give(&entries[i], a);
            if (version >= BATTLE_REQUEST_VERSION_ITEMS) {  // V17: an item with the most records
                BattleItem it = {.cls = BICLASS_RECORDS, .count = BATTLE_MAX_ITEM_RECORDS, .source = 500};
                for (unsigned k = 0; k < BATTLE_MAX_ITEM_RECORDS; ++k)
                    it.records[k] = (BattleItemRecord){.kind = BITEM_MODIFIER,
                                                       .mod = modr(BMOD_POWER, (uint8_t)(101 + k), 0, 0, 0, 0)};
                entries[i].record.held_item = 500;
                entries[i].mon.item = it;
            }
        }
    }
    r->player_count = r->opponent_count = (uint8_t)per_side;
}

static void test_v13_largest_request(void) {
    static BattleRequest r, d;
    static uint8_t bytes[8192];
    const struct { uint8_t format; unsigned per_side; } cases[] = {
        {BATTLE_FORMAT_TRIPLE, 3}, {BATTLE_FORMAT_SINGLE, 6}, {BATTLE_FORMAT_DOUBLE, 6}};
    for (unsigned c = 0; c < 3; ++c)
        for (uint8_t v = BATTLE_REQUEST_VERSION_ABILITY_HOOKS; v <= BATTLE_REQUEST_VERSION_LATEST; ++v) {  // V12..V17
            build_largest(&r, cases[c].format, cases[c].per_side, v);
            // The bound covers every version; requests before V15 have no field rules,
            // before V16 no terrain rules, before V17 no item blocks.
            const uint32_t max = BATTLE_REQUEST_MAX_BYTES(2 * cases[c].per_side, NTYPES) -
                                 (v < BATTLE_REQUEST_VERSION_WEATHER ? BATTLE_FIELD_RULE_BYTES : 0) -
                                 (v < BATTLE_REQUEST_VERSION_TERRAIN ? BATTLE_TERRAIN_RULE_BYTES : 0) -
                                 (v < BATTLE_REQUEST_VERSION_ITEMS
                                      ? 2 * cases[c].per_side *
                                            (BATTLE_ITEM_HEADER_BYTES + BATTLE_MAX_ITEM_RECORDS * BATTLE_ITEM_RECORD_BYTES)
                                      : 0);
            assert(battle_request_size(&r) == max);
            assert(battle_request_encode(&r, bytes, max) == max);
            assert(battle_request_decode(bytes, max, &d));
            assert(!battle_request_decode(bytes, max - 1, &d));
            memset(bytes, 0xee, sizeof bytes);
            assert(battle_request_encode(&r, bytes, max - 1) == 0);
            assert(bytes[0] == 0xee);  // nothing written when it does not fit
        }
    // The Frontier case: six Pokemon in all and its 21 types (the M4-A2 buffer size
    // was 2789; V15 adds the 88-byte field rules, V16 the 72-byte terrain rules,
    // V17 up to 36 bytes of item block per Pokemon).
    assert(BATTLE_FIELD_RULE_BYTES == 88 && BATTLE_TERRAIN_RULE_BYTES == 72);
    assert(BATTLE_REQUEST_MAX_BYTES(6, 21) == 2789 + 88 + 72 + 6 * 36);
}

static void test_v13_hex_and_custom(void) {
    static BattleRequest r, plain;
    Battle b, p;
    make_mods(&r, BATTLE_FORMAT_SINGLE);
    plain = r;
    // Hex (a move record): x2 base power while the target has a major status.
    const BattleMove hex = with_mod(damaging(65, T_FIRE, 100), HEX_MOD);
    const BattleMove p65 = damaging(65, T_FIRE, 100), p130 = damaging(130, T_FIRE, 100);
    assert(battle_init(&b, &r, 1) == BI_OK);
    Battler *a = battle_active(&b, 0), *d = battle_active(&b, 1);
    assert(dmg(&b, &hex) == dmg(&b, &p65) && dmg(&b, &p130) > dmg(&b, &p65));
    a->status = BST_PARALYSIS;  // the user's status does not count
    assert(dmg(&b, &hex) == dmg(&b, &p65));
    for (uint8_t s = BST_POISON; s <= BST_FREEZE; ++s) {
        d->status = s;
        assert(dmg(&b, &hex) == dmg(&b, &p130));
    }
    d->status = 0;
    // The custom ability (an ability record): physical power x2 at half HP or less AND statused.
    give(&r.player[0], MODS(250, CUSTOM_MOD));
    assert(battle_init(&b, &r, 1) == BI_OK && battle_init(&p, &plain, 1) == BI_OK);
    a = battle_active(&b, 0);
    Battler *pa = battle_active(&p, 0);
    const BattleMove phys = damaging(40, T_FIRE, 100), phys80 = damaging(80, T_FIRE, 100);
    BattleMove spec = phys;
    spec.category = BCAT_SPECIAL;
    const struct { uint16_t hp; uint8_t status; bool boosted; } cases[] = {
        {200, 0, false}, {100, 0, false}, {200, BST_PARALYSIS, false}, {101, BST_POISON, false},
        {100, BST_PARALYSIS, true}, {1, BST_SLEEP, true}, {50, BST_TOXIC, true},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; ++i) {
        a->hp = pa->hp = cases[i].hp;
        a->status = pa->status = cases[i].status;
        assert(dmg(&b, &phys) == dmg(&p, cases[i].boosted ? &phys80 : &phys));
        assert(dmg(&b, &spec) == dmg(&p, &spec));
    }
    // A V12 battle never runs a V13 record, even if one reaches the damage calculation.
    {
        static BattleRequest v12;
        Battle old;
        make_hooks(&v12, BATTLE_FORMAT_SINGLE);
        assert(battle_init(&old, &v12, 1) == BI_OK);
        battle_active(&old, 1)->status = BST_BURN;
        assert(dmg(&old, &hex) == dmg(&old, &p65));
    }
    // RULE_IGNORE_ABILITIES: the ability record is inert, the move record is not.
    r.rules |= RULE_IGNORE_ABILITIES;
    assert(battle_init(&b, &r, 1) == BI_OK);
    a = battle_active(&b, 0);
    a->hp = 100;
    a->status = BST_PARALYSIS;
    battle_active(&b, 1)->status = BST_BURN;
    assert(dmg(&b, &phys) == dmg(&p, &phys) && dmg(&b, &hex) == dmg(&b, &p130));
}

// The same record from a move and from an ability takes the same path.
static void test_v13_shared_record(void) {
    static BattleRequest r, plain;
    Battle b, p;
    make_mods(&r, BATTLE_FORMAT_SINGLE);
    plain = r;
    const BattleModifier x = modr(BMOD_POWER, 150, BPRED_HP_AT_MOST | BPRED_DEFENDER, 2, BPRED_STATUS, ANY_STATUS);
    const BattleMove base = damaging(45, T_WATER, 100), as_move = with_mod(base, x);
    give(&r.player[0], MODS(251, x));
    assert(memcmp(&as_move.effects[1].mod, &r.player[0].mon.ability.hooks[0].mod, sizeof x) == 0);
    assert(battle_init(&b, &r, 1) == BI_OK && battle_init(&p, &plain, 1) == BI_OK);
    unsigned boosted = 0;
    for (unsigned s = 0; s < 2; ++s)
        for (unsigned h = 0; h < 3; ++h)
            for (unsigned roll = 85; roll <= 100; roll += 5) {
                battle_active(&b, 0)->status = battle_active(&p, 0)->status = s ? BST_BURN : 0;
                battle_active(&b, 1)->hp = battle_active(&p, 1)->hp = (uint16_t)(h == 0 ? 200 : h == 1 ? 100 : 1);
                const uint16_t from_ability = dmg_at(&b, &base, roll), from_move = dmg_at(&p, &as_move, roll);
                assert(from_ability == from_move);
                boosted += from_move != dmg_at(&p, &base, roll);
            }
    assert(boosted == 2 * 4);  // statused attacker and a defender at half HP or less
}

// Order inside a stage, rounding after each step, the minimum, and where each stage applies.
static void test_v13_order_rounding(void) {
    static BattleRequest r, plain;
    Battle b, p;
    make_mods(&r, BATTLE_FORMAT_SINGLE);
    plain = r;
    assert(battle_init(&p, &plain, 1) == BI_OK);
#define POWER_OF(n) damaging(n, T_FIRE, 100)
    // Move, then attacker: 45 -> 67 (x1.5) -> 89 (x1.33); the other order gives 88.
    give(&r.player[0], MODS(252, modr(BMOD_POWER, 133, 0, 0, 0, 0)));
    assert(battle_init(&b, &r, 1) == BI_OK);
    const BattleMove m150 = with_mod(POWER_OF(45), modr(BMOD_POWER, 150, 0, 0, 0, 0));
    BattleMove p89 = POWER_OF(89), p88 = POWER_OF(88);
    assert(dmg(&b, &m150) == dmg(&p, &p89) && dmg(&p, &p89) != dmg(&p, &p88));
    // Attacker, then defender: 45 -> 59 (x1.33) -> 88 (x1.5).
    give(&r.opponent[0], MODS(253, modr(BMOD_POWER | BMODF_DEFENDER, 150, 0, 0, 0, 0)));
    assert(battle_init(&b, &r, 1) == BI_OK);
    BattleMove p45 = POWER_OF(45);
    assert(dmg(&b, &p45) == dmg(&p, &p88));
    // A defender-role record does not act for the attacking holder, and the reverse.
    give(&r.player[0], MODS(254, modr(BMOD_POWER | BMODF_DEFENDER, 200, 0, 0, 0, 0)));
    give(&r.opponent[0], MODS(255, modr(BMOD_POWER, 200, 0, 0, 0, 0)));
    assert(battle_init(&b, &r, 1) == BI_OK && dmg(&b, &p45) == dmg(&p, &p45));
    // V12 kinds first: Technician then the V13 record (45 -> 67 -> 89).
    give(&r.player[0], (BattleAbility){.cls = BACLASS_HOOKS, .count = 2, .source = 101,
                                       .hooks = {mod_hook(modr(BMOD_POWER, 133, 0, 0, 0, 0)),
                                                 hk(BABIL_LOW_POWER, 60, 0, 0, 0, 0, 150, 0)}});
    give(&r.opponent[0], (BattleAbility){0});
    assert(battle_init(&b, &r, 1) == BI_OK && dmg(&b, &p45) == dmg(&p, &p89));
    // V12 order inside the attack stage: every ATTACK_MULT, then PINCH
    // (67 -> 73 -> 109; PINCH first would give 100 -> 110).
    give(&r.player[0], (BattleAbility){.cls = BACLASS_HOOKS, .count = 2, .source = 102,
                                       .hooks = {hk(BABIL_PINCH, T_FIRE, 0, 2, 0, 0, 150, 0),
                                                 hk(BABIL_ATTACK_MULT, 0, 0, 0, 0, 0, 110, 0)}});
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_active(&b, 0)->attack = 67;
    battle_active(&b, 0)->hp = 100;
    {
        BattleMove p100 = POWER_OF(100);
        battle_active(&p, 0)->attack = 109;
        const uint16_t expected = dmg(&p, &p100);
        battle_active(&p, 0)->attack = 110;
        assert(dmg(&b, &p100) == expected && expected != dmg(&p, &p100));
        battle_active(&p, 0)->attack = 60;
    }
    // Minimum 1 for power; the final stage floors to 0, then the damage minimum gives 1.
    give(&r.player[0], MODS(256, modr(BMOD_POWER, 1, 0, 0, 0, 0)));
    assert(battle_init(&b, &r, 1) == BI_OK);
    BattleMove p50 = POWER_OF(50), p1 = POWER_OF(1);
    assert(dmg(&b, &p50) == dmg(&p, &p1));
    give(&r.player[0], MODS(257, modr(BMOD_FINAL, 1, 0, 0, 0, 0)));
    assert(battle_init(&b, &r, 1) == BI_OK && dmg(&b, &p50) == 1 && dmg(&p, &p50) > 1);
    // Base power before the power stage: Technician-like records and V12
    // Technician see Hex's doubled power (40 -> 80 > 60: no boost), as in the game.
    const BattleMove hex40 = with_mod(POWER_OF(40), HEX_MOD);
    BattleMove p60 = POWER_OF(60), p80 = POWER_OF(80), p120 = POWER_OF(120);
    for (unsigned v = 0; v < 2; ++v) {
        give(&r.player[0], v ? MODS(258, modr(BMOD_POWER, 150, BPRED_POWER_AT_MOST, 60, 0, 0))
                             : (BattleAbility){.cls = BACLASS_HOOKS, .count = 1, .source = 101,
                                               .hooks = {hk(BABIL_LOW_POWER, 60, 0, 0, 0, 0, 150, 0)}});
        assert(battle_init(&b, &r, 1) == BI_OK);
        assert(dmg(&b, &hex40) == dmg(&p, &p60));       // no status: 40 -> 60
        battle_active(&b, 1)->status = BST_POISON;
        assert(dmg(&b, &hex40) == dmg(&p, &p80));       // 80, not 120
        assert(dmg(&p, &p80) != dmg(&p, &p120));
    }
    // Final damage applies after burn: floor(floor(x / 2) * 1.5).
    give(&r.player[0], MODS(259, modr(BMOD_FINAL, 150, 0, 0, 0, 0)));
    assert(battle_init(&b, &r, 1) == BI_OK);
    BattleMove phys = POWER_OF(70);
    for (unsigned s = 0; s < 2; ++s) {
        battle_active(&b, 0)->status = battle_active(&p, 0)->status = s ? BST_BURN : 0;
        for (unsigned roll = 85; roll <= 100; ++roll)
            assert(dmg_at(&b, &phys, roll) == dmg_at(&p, &phys, roll) * 150u / 100u);
    }
    battle_active(&p, 0)->status = 0;
    // Inside the final stage there is no minimum between steps: 1% then x2 stays 0, then 1.
    give(&r.player[0], MODS(262, modr(BMOD_FINAL, 1, 0, 0, 0, 0), modr(BMOD_FINAL, 200, 0, 0, 0, 0)));
    assert(battle_init(&b, &r, 1) == BI_OK && dmg(&b, &phys) == 1);
    // V12 first-only kinds: the second PINCH hook is never consulted.
    give(&r.player[0], HOOKS(66, hk(BABIL_PINCH, T_WATER, 0, 2, 0, 0, 150, 0), hk(BABIL_PINCH, T_FIRE, 0, 2, 0, 0, 200, 0)));
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_active(&b, 0)->hp = 1;
    assert(dmg(&b, &phys) == dmg(&p, &phys));
    // Struggle is typeless: a move-type predicate never holds for it (V12 PINCH and a V13 record alike).
    for (unsigned v = 0; v < 2; ++v) {
        static EventLog with, without;
        Battle s, q;
        give(&r.player[0], v ? MODS(263, modr(BMOD_ATTACK, 200, BPRED_MOVE_TYPE, T_NORMAL, 0, 0))
                             : HOOKS(66, hk(BABIL_PINCH, T_NORMAL, 0, 2, 0, 0, 150, 0)));
        assert(battle_init(&s, &r, 3) == BI_OK && battle_init(&q, &plain, 3) == BI_OK);
        for (unsigned i = 0; i < 4; ++i) battle_active(&s, 0)->pp[i] = battle_active(&q, 0)->pp[i] = 0;
        battle_active(&s, 0)->hp = battle_active(&q, 0)->hp = 1 + v;  // in PINCH range
        with.count = without.count = 0;
        battle_run_turn(&s, (Action[]){{.kind = ACT_STRUGGLE, .user = {0, 0}}}, 1, &with);
        battle_run_turn(&q, (Action[]){{.kind = ACT_STRUGGLE, .user = {0, 0}}}, 1, &without);
        assert(find(&with, EV_DAMAGE) && find(&with, EV_DAMAGE)->amount == find(&without, EV_DAMAGE)->amount);
    }
    // Attack and Defense stages: the stat after its stages.
    give(&r.player[0], MODS(260, modr(ST(BMOD_ATTACK, BCAT_PHYSICAL), 200, 0, 0, 0, 0)));
    give(&r.opponent[0], MODS(261, modr(ST(BMOD_DEFENSE, BCAT_PHYSICAL) | BMODF_DEFENDER, 200, 0, 0, 0, 0)));
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_active(&b, 0)->status = battle_active(&p, 0)->status = 0;
    battle_active(&b, 0)->stat_stages[BSTAT_ATK] = battle_active(&p, 0)->stat_stages[BSTAT_ATK] = 2;
    assert(dmg(&b, &phys) == dmg(&p, &phys));  // x2 / x2
    give(&r.opponent[0], (BattleAbility){0});
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_active(&b, 0)->stat_stages[BSTAT_ATK] = 2;
    battle_active(&p, 0)->attack = 120;  // the plain attacker with twice the stat
    assert(dmg(&b, &phys) == dmg(&p, &phys));
    battle_active(&p, 0)->attack = 60;
    battle_active(&p, 0)->stat_stages[BSTAT_ATK] = 0;
#undef POWER_OF
}

// The V12 multiplier kinds and the same rule written as V13 records give the same damage.
static void test_v13_expresses_v12(void) {
    static BattleRequest v12, v13;
    Battle a, b;
    const BattleAbility pairs[][2] = {
        {GUTS_ONLY, MODS(62, modr(ST(BMOD_ATTACK, BCAT_PHYSICAL), 150, BPRED_STATUS, ANY_STATUS, 0, 0))},
        {HOOKS(66, hk(BABIL_PINCH, T_FIRE, 0, 3, 0, 0, 150, 0)),
         MODS(66, modr(BMOD_ATTACK, 150, BPRED_MOVE_TYPE, T_FIRE, BPRED_HP_AT_MOST, 3))},
        {HOOKS(101, hk(BABIL_LOW_POWER, 60, 0, 0, 0, 0, 150, 0)),
         MODS(101, modr(BMOD_POWER, 150, BPRED_POWER_AT_MOST, 60, 0, 0))},
        {HOOKS(37, hk(BABIL_ATTACK_MULT, BCAT_SPECIAL, 0, 0, 0, 0, 200, 0)),
         MODS(37, modr(ST(BMOD_ATTACK, BCAT_SPECIAL), 200, 0, 0, 0, 0))},
    };
    const BattleAbility defending[][2] = {
        {HOOKS(47, hk(BABIL_RESIST, T_FIRE, 0, T_WATER, 0, 0, 50, 0)),
         MODS(47, modr(BMOD_ATTACK | BMODF_DEFENDER, 50, BPRED_MOVE_TYPE, T_FIRE, 0, 0),
              modr(BMOD_ATTACK | BMODF_DEFENDER, 50, BPRED_MOVE_TYPE, T_WATER, 0, 0))},
        {MARVEL_SCALE, MODS(63, modr(ST(BMOD_DEFENSE, BCAT_PHYSICAL) | BMODF_DEFENDER, 150,
                                     BPRED_STATUS | BPRED_DEFENDER, ANY_STATUS, 0, 0))},
    };
    unsigned compared = 0, differed = 0;
    for (unsigned role = 0; role < 2; ++role) {
        const unsigned n = role ? sizeof defending / sizeof *defending : sizeof pairs / sizeof *pairs;
        for (unsigned k = 0; k < n; ++k) {
            make_hooks(&v12, BATTLE_FORMAT_SINGLE);
            make_mods(&v13, BATTLE_FORMAT_SINGLE);
            const BattleAbility *pair = role ? defending[k] : pairs[k];
            give(role ? &v12.opponent[0] : &v12.player[0], pair[0]);
            give(role ? &v13.opponent[0] : &v13.player[0], pair[1]);
            assert(battle_init(&a, &v12, 1) == BI_OK && battle_init(&b, &v13, 1) == BI_OK);
            for (unsigned type = 0; type < NTYPES; ++type)
                for (unsigned cat = BCAT_PHYSICAL; cat <= BCAT_SPECIAL; ++cat)
                    for (unsigned power = 30; power <= 90; power += 30)
                        for (unsigned hp = 0; hp < 3; ++hp)
                            for (unsigned s = 0; s < 3; ++s) {
                                BattleMove m = damaging((uint16_t)power, (uint8_t)type, 100);
                                m.category = (uint8_t)cat;
                                Battler *holder_a = role ? battle_active(&a, 1) : battle_active(&a, 0);
                                Battler *holder_b = role ? battle_active(&b, 1) : battle_active(&b, 0);
                                holder_a->hp = holder_b->hp = (uint16_t)(hp == 0 ? 200 : hp == 1 ? 67 : 66);
                                holder_a->status = holder_b->status = s == 0 ? 0 : s == 1 ? BST_PARALYSIS : BST_BURN;
                                const uint16_t x = dmg(&a, &m), y = dmg(&b, &m);
                                assert(x == y);
                                ++compared;
                                holder_a->hp = 200;
                                holder_a->status = 0;
                                differed += x != dmg(&a, &m);
                            }
        }
    }
    assert(compared == 6 * NTYPES * 2 * 3 * 3 * 3 && differed > 40);
    printf("v13 expresses v12: %u cases, %u changed by the ability\n", compared, differed);
}

// Predicates that never hold change nothing, not even the RNG; ones that
// hold change the damage but draw the same random numbers.
static void test_v13_no_rng(void) {
    static BattleRequest with, without, holding;
    static Battle a, b, c;
    static EventLog la, lb, lc;
    make_mods(&with, BATTLE_FORMAT_DOUBLE);
    for (unsigned side = 0; side < 2; ++side)
        for (unsigned i = 0; i < 2; ++i) {
            BattleEntry *e = side ? &with.opponent[i] : &with.player[i];
            e->record.hp = e->record.max_hp = 250;
            set_move(e, 1, 506, damaging(65, T_FIRE, 0), 20);
            if (side == 0) e->record.status = i ? BST_POISON : BST_PARALYSIS;
        }
    without = holding = with;
    // Frostbite is off in these rules, so the predicates never hold.
    const BattleModifier never = modr(BMOD_BASE_POWER, 200, BPRED_STATUS | BPRED_DEFENDER, 1u << BST_FROSTBITE, 0, 0);
    with.player[0].mon.moves[1] = with_mod(with.player[0].mon.moves[1], never);
    give(&with.player[1], MODS(250, modr(ST(BMOD_POWER, BCAT_PHYSICAL), 200, BPRED_STATUS, 1u << BST_FROSTBITE, 0, 0)));
    holding.opponent[0].mon.moves[1] = with_mod(holding.opponent[0].mon.moves[1], HEX_MOD);
    give(&holding.opponent[1], MODS(250, modr(BMOD_FINAL, 150, BPRED_STATUS | BPRED_DEFENDER, ANY_STATUS, 0, 0)));
    assert(battle_init(&a, &with, 7) == BI_OK && battle_init(&b, &without, 7) == BI_OK &&
           battle_init(&c, &holding, 7) == BI_OK);
    battle_begin(&a, &la);
    battle_begin(&b, &lb);
    battle_begin(&c, &lc);
    unsigned differed = 0;
    for (unsigned t = 0; t < 3; ++t) {
        const Action actions[] = {slot_move(0, 0, 0, 1), slot_move(0, 1, 1, 1), slot_move(1, 0, 0, 1),
                                  slot_move(1, 1, 1, 1)};
        la.count = lb.count = lc.count = 0;
        battle_run_turn(&a, actions, 4, &la);
        battle_run_turn(&b, actions, 4, &lb);
        battle_run_turn(&c, actions, 4, &lc);
        assert(la.count == lb.count && memcmp(la.events, lb.events, la.count * sizeof *la.events) == 0);
        assert(a.rng == b.rng && c.rng == b.rng && lc.count == lb.count);
        for (unsigned i = 0; i < lc.count; ++i)
            differed += lc.events[i].kind == EV_DAMAGE && lc.events[i].amount != lb.events[i].amount;
    }
    for (unsigned side = 0; side < 2; ++side)
        for (unsigned i = 0; i < a.sides[side].count; ++i)
            assert(a.sides[side].battlers[i].hp == b.sides[side].battlers[i].hp &&
                   a.sides[side].battlers[i].status == b.sides[side].battlers[i].status);
    assert(differed == 6);
}

// Same request and seed: two cores give the same events with V13 records in every format.
static void test_v13_determinism(void) {
    static BattleRequest r;
    static Battle a, b;
    static EventLog la, lb;
    const BattleAbility pool[] = {MODS(250, CUSTOM_MOD), GUTS, MODS(251, modr(BMOD_FINAL | BMODF_DEFENDER, 75, 0, 0, 0, 0)),
                                  MODS(252, modr(BMOD_POWER, 150, BPRED_POWER_AT_MOST, 60, BPRED_HP_AT_MOST, 2)),
                                  STURDY, ROUGH_SKIN, MARVEL_SCALE};
    const unsigned n = sizeof pool / sizeof *pool;
    const uint8_t formats[] = {BATTLE_FORMAT_SINGLE, BATTLE_FORMAT_DOUBLE, BATTLE_FORMAT_TRIPLE, BATTLE_FORMAT_ROTATION};
    unsigned damage = 0;
    for (unsigned f = 0; f < 4; ++f)
        for (uint32_t seed = 1; seed <= 30; ++seed) {
            make_mods(&r, formats[f]);
            for (unsigned side = 0; side < 2; ++side) add_reserve(&r, side);
            for (unsigned side = 0; side < 2; ++side) {
                BattleEntry *e = side ? r.opponent : r.player;
                const unsigned count = side ? r.opponent_count : r.player_count;
                for (unsigned i = 0; i < count; ++i) {
                    give(&e[i], pool[(seed * 3 + side * 5 + i + f) % n]);
                    e[i].record.hp = e[i].record.max_hp = 60;
                    e[i].record.status = (seed + i + side) % 3 == 0 ? BST_POISON : 0;
                    BattleMove hex = with_mod(damaging(50, (uint8_t)(1 + (seed + i) % 2), 95), HEX_MOD);
                    set_move(&e[i], 1, 506, hex, 20);
                    set_move(&e[i], 2, 300, contact(35, 1), 20);
                }
            }
            assert(battle_init(&a, &r, seed) == BI_OK && battle_init(&b, &r, seed) == BI_OK);
            la.count = lb.count = 0;
            battle_begin(&a, &la);
            battle_begin(&b, &lb);
            for (unsigned t = 0; t < 40 && a.outcome == OUTCOME_NONE; ++t) {
                Position pending;
                while (battle_pending_replacement(&a, &pending)) {
                    const uint8_t mask = battle_replacement_mask(&a, pending);
                    uint8_t pick = 0;
                    while (!(mask & (1u << pick))) ++pick;
                    assert(battle_replace(&a, pending, pick, &la) && battle_replace(&b, pending, pick, &lb));
                }
                Action actions[BC_MAX_ACTIONS];
                unsigned count = 0;
                for (uint8_t side = 0; side < 2; ++side) {
                    Position actors[BC_MAX_ACTIVE];
                    const unsigned k = battle_collect_actors(&a, side, actors);
                    for (unsigned i = 0; i < k; ++i) battle_choose_at(&a, actors[i], &actions[count++]);
                }
                for (unsigned i = 0; i < count; ++i) { Action x; battle_choose_at(&b, actions[i].user, &x); }
                la.count = lb.count = 0;
                battle_run_turn(&a, actions, count, &la);
                battle_run_turn(&b, actions, count, &lb);
                assert(la.count == lb.count && memcmp(la.events, lb.events, la.count * sizeof(BattleEvent)) == 0);
                damage += event_count(&la, EV_DAMAGE);
            }
            assert(memcmp(&a.sides, &b.sides, sizeof a.sides) == 0 && a.rng == b.rng);
        }
    assert(damage > 500);
    printf("v13 determinism: %u damage events\n", damage);
}

// ------------------------------------------------------------- V14 fixed-point arithmetic (docs/modifiers.md)

static uint64_t mod_at(Battle *b, const BattleMove *m, uint8_t stage, uint64_t value, bool doubled) {
    const ModContext c = {.b = b, .attacker = battle_active(b, 0), .defender = battle_active(b, 1), .move = m,
                          .doubled = doubled, .base_power = m->power};
    return bc_modify(&c, stage, value);
}

static void test_v14_arithmetic(void) {
    // A percent is the nearest 4.12 value (as UQ_4_12(n) in the game).
    assert(bc_fx_percent(150) == 6144 && bc_fx_percent(130) == 5325 && bc_fx_percent(120) == 4915);
    assert(bc_fx_percent(133) == 5448 && bc_fx_percent(33) == 1352 && bc_fx_percent(50) == 2048);
    assert(bc_fx_percent(1) == 41 && bc_fx_percent(255) == 10445 && bc_fx_percent(100) == 4096);
    // value x factor to the nearest integer, an exact half down.
    assert(bc_fx_apply(6144, 7) == 10 && bc_fx_apply(3072, 5) == 4 && bc_fx_apply(3072, 3) == 2);
    assert(bc_fx_apply(1024, 7) == 2 && bc_fx_apply(1024, 6) == 1 && bc_fx_apply(4096, 12345) == 12345);
    assert(bc_fx_apply(UINT64_MAX, UINT64_MAX) == bc_fx_apply(UINT32_MAX, UINT32_MAX));  // capped, no overflow

    static BattleRequest r;
    Battle b;
    make_mods(&r, BATTLE_FORMAT_SINGLE);
    const BattleMove plain = damaging(50, T_FIRE, 100);
    // One chain per stage: 1.5 x 1.3 = 7987.5 / 4096. The power chain rounds
    // that half up, the stat chains down; V13 floors each step (6144 -> 7987).
    give(&r.player[0], MODS(260, modr(BMOD_POWER, 150, 0, 0, 0, 0), modr(BMOD_POWER, 130, 0, 0, 0, 0),
                            modr(BMOD_ATTACK, 150, 0, 0, 0, 0), modr(BMOD_ATTACK, 130, 0, 0, 0, 0)));
    r.version = BATTLE_REQUEST_VERSION_FIXED_POINT;
    assert(battle_init(&b, &r, 1) == BI_OK);
    assert(mod_at(&b, &plain, BMOD_POWER, 4096, false) == 7988 && mod_at(&b, &plain, BMOD_ATTACK, 4096, false) == 7987);
    r.version = BATTLE_REQUEST_VERSION_MODIFIERS;
    assert(battle_init(&b, &r, 1) == BI_OK && mod_at(&b, &plain, BMOD_POWER, 4096, false) == 7987);
    // Rounded once: x1.5 (attacker) then x0.5 (defender) on 101 is 75.75 -> 76; V13 gives 151 -> 75.
    give(&r.player[0], MODS(261, modr(BMOD_ATTACK, 150, 0, 0, 0, 0)));
    give(&r.opponent[0], MODS(262, modr(BMOD_ATTACK | BMODF_DEFENDER, 50, 0, 0, 0, 0)));
    assert(battle_init(&b, &r, 1) == BI_OK && mod_at(&b, &plain, BMOD_ATTACK, 101, false) == 75);
    r.version = BATTLE_REQUEST_VERSION_FIXED_POINT;
    assert(battle_init(&b, &r, 1) == BI_OK && mod_at(&b, &plain, BMOD_ATTACK, 101, false) == 76);
    // Minimums: the stats and the power stay at least 1; the final stage may reach 0.
    give(&r.player[0], MODS(263, modr(BMOD_ATTACK, 50, 0, 0, 0, 0), modr(BMOD_FINAL, 50, 0, 0, 0, 0)));
    give(&r.opponent[0], (BattleAbility){0});
    assert(battle_init(&b, &r, 1) == BI_OK);
    assert(mod_at(&b, &plain, BMOD_ATTACK, 1, false) == 1 && mod_at(&b, &plain, BMOD_FINAL, 1, false) == 0);
    // The base power stage stays integer (Hex: x2) in V14.
    give(&r.player[0], MODS(264, modr(BMOD_BASE_POWER, 150, 0, 0, 0, 0)));
    assert(battle_init(&b, &r, 1) == BI_OK && mod_at(&b, &plain, BMOD_BASE_POWER, 45, false) == 67);

    // Source order. With 1.10 (move), 1.01 and 1.25 the half-down chain is
    // 5689 in that order and 5688 with the last two swapped.
    const BattleMove m110[] = {with_mod(plain, modr(BMOD_ATTACK, 110, 0, 0, 0, 0)),
                               with_mod(plain, modr(BMOD_DEFENSE, 110, 0, 0, 0, 0)),
                               with_mod(plain, modr(BMOD_FINAL, 110, 0, 0, 0, 0))};
    // Attack: move, attacker, defender.
    give(&r.player[0], MODS(265, modr(BMOD_ATTACK, 101, 0, 0, 0, 0)));
    give(&r.opponent[0], MODS(266, modr(BMOD_ATTACK | BMODF_DEFENDER, 125, 0, 0, 0, 0)));
    assert(battle_init(&b, &r, 1) == BI_OK && mod_at(&b, &m110[0], BMOD_ATTACK, 4096, false) == 5689);
    // Defense: move, defender, attacker (an attacker's record lowering the target's stat comes last).
    give(&r.player[0], MODS(267, modr(BMOD_DEFENSE, 125, 0, 0, 0, 0)));
    give(&r.opponent[0], MODS(268, modr(BMOD_DEFENSE | BMODF_DEFENDER, 101, 0, 0, 0, 0)));
    assert(battle_init(&b, &r, 1) == BI_OK && mod_at(&b, &m110[1], BMOD_DEFENSE, 4096, false) == 5689);
    // Final: move, then the faster Pokemon's ability (the attacker's on a tie).
    give(&r.player[0], MODS(269, modr(BMOD_FINAL, 101, 0, 0, 0, 0)));
    give(&r.opponent[0], MODS(270, modr(BMOD_FINAL | BMODF_DEFENDER, 125, 0, 0, 0, 0)));
    assert(battle_init(&b, &r, 1) == BI_OK);
    const uint16_t speeds[][2] = {{100, 50}, {60, 60}, {10, 50}};
    const uint64_t final[] = {5689, 5689, 5688};
    for (unsigned i = 0; i < 3; ++i) {
        battle_active(&b, 0)->speed = speeds[i][0];
        battle_active(&b, 1)->speed = speeds[i][1];
        assert(mod_at(&b, &m110[2], BMOD_FINAL, 4096, false) == final[i]);
    }
    // The semi-invulnerable x2 heads the final chain (exact); V13 leaves it to the caller.
    give(&r.player[0], (BattleAbility){0});
    give(&r.opponent[0], (BattleAbility){0});
    assert(battle_init(&b, &r, 1) == BI_OK && mod_at(&b, &plain, BMOD_FINAL, 77, true) == 154);
    r.version = BATTLE_REQUEST_VERSION_MODIFIERS;
    assert(battle_init(&b, &r, 1) == BI_OK && mod_at(&b, &plain, BMOD_FINAL, 77, true) == 77);

    // Largest chains: every record and hook at 255% on one stage stays in range.
    r.version = BATTLE_REQUEST_VERSION_FIXED_POINT;
    BattleMove many = plain;
    for (unsigned i = 1; i < BATTLE_MAX_EFFECTS; ++i) push(&many, mod_effect(modr(BMOD_FINAL, 255, 0, 0, 0, 0)));
    const BattleModifier max_mod = modr(BMOD_FINAL, 255, 0, 0, 0, 0);
    const BattleModifier max_def = modr(BMOD_FINAL | BMODF_DEFENDER, 255, 0, 0, 0, 0);
    give(&r.player[0], MODS(271, max_mod, max_mod, max_mod, max_mod));
    give(&r.opponent[0], MODS(272, max_def, max_def, max_def, max_def));
    set_move(&r.player[0], 0, 33, many, 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    assert(mod_at(&b, &many, BMOD_FINAL, UINT64_MAX, true) > UINT32_MAX);  // a chain of 2 x 2.55^13
    uint8_t eff;
    assert(bc_calculate_damage(&b, battle_active(&b, 0), &many, battle_active(&b, 1), 100, false,
                               BCD_SPREAD | BCD_DOUBLED, &eff) == 0xffff);
}

// The spread and semi-invulnerable factors are applied once, inside the
// formula, in a real V14 turn (strike no longer applies them afterwards).
static bool from_roll(Battle *before, Position user, Position target, const BattleMove *m, unsigned flags,
                      uint16_t amount, uint16_t *highest) {
    bool found = false;
    *highest = 0;
    for (unsigned roll = 85; roll <= 100; ++roll) {
        uint8_t eff;
        const uint16_t v = bc_calculate_damage(before, battle_at(before, user), m, battle_at(before, target), roll,
                                               false, flags, &eff);
        found |= v == amount;
        if (v > *highest) *highest = v;
    }
    return found;
}

static void test_v14_battle(void) {
    static BattleRequest r;
    static Battle b, before;
    EventLog log;
    uint16_t highest;
    // Double: a spread hit on each foe is one of its V14 values with BCD_SPREAD, not x0.75 again.
    make_mods(&r, BATTLE_FORMAT_DOUBLE);
    r.version = BATTLE_REQUEST_VERSION_FIXED_POINT;
    BattleMove spread = damaging(80, T_WATER, 0);
    spread.category = BCAT_SPECIAL;
    spread.target = BTARGET_ALL_FOES;
    set_move(&r.player[0], 1, 57, spread, 10);
    assert(battle_init(&b, &r, 5) == BI_OK);
    before = b;
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1)}, 1, &log);
    unsigned hits = 0;
    for (unsigned i = 0; i < log.count; ++i) {
        const BattleEvent *e = &log.events[i];
        if (e->kind != EV_DAMAGE || e->user.side != 0 || e->user.slot != 0) continue;
        ++hits;
        assert(from_roll(&before, e->user, e->target, &spread, BCD_SPREAD, e->amount, &highest));
        assert(e->amount * 4u > highest * 3u);
    }
    assert(hits == 2);
    // Single: Earthquake on a Pokemon underground is doubled once.
    make_mods(&r, BATTLE_FORMAT_SINGLE);
    r.version = BATTLE_REQUEST_VERSION_FIXED_POINT;
    BattleMove quake = damaging(100, T_NORMAL, 0);
    quake.move_flags = BMF_DOUBLE_UNDERGROUND;
    set_move(&r.player[0], 1, 91, charge_move(80, BSEMI_UNDERGROUND), 10);
    set_move(&r.opponent[0], 1, 89, quake, 10);
    r.opponent[0].record.speed = 1;
    assert(battle_init(&b, &r, 3) == BI_OK);
    before = b;
    battle_run_turn(&b, (Action[]){slot_move(0, 0, 0, 1), slot_move(1, 0, 0, 1)}, 2, &log);
    const BattleEvent *hit = find(&log, EV_DAMAGE);
    assert(hit && hit->target.side == 0);
    // The target was already underground when hit: compute against that state.
    bc_mutable_at(&before, (Position){0, 0})->vol.semi_invulnerable = BSEMI_UNDERGROUND;
    assert(from_roll(&before, hit->user, hit->target, &quake, BCD_DOUBLED, hit->amount, &highest));
    assert(hit->amount <= highest);
}

// Source order where it changes a chain (three or more factors; two leading
// factors commute exactly). Each expected value is the chain in the
// documented order; the swapped order gives the other value shown.
static void test_v14_order(void) {
    static BattleRequest r;
    Battle b;
    make_mods(&r, BATTLE_FORMAT_SINGLE);
    r.version = BATTLE_REQUEST_VERSION_FIXED_POINT;
    const BattleMove plain = damaging(50, T_FIRE, 100);
    // Power: the move's records, then Technician (LOW_POWER first would give 6268).
    BattleMove two = with_mod(with_mod(plain, modr(BMOD_POWER, 101, 0, 0, 0, 0)), modr(BMOD_POWER, 101, 0, 0, 0, 0));
    give(&r.player[0], HOOKS(101, hk(BABIL_LOW_POWER, 60, 0, 0, 0, 0, 150, 0)));
    assert(battle_init(&b, &r, 1) == BI_OK && mod_at(&b, &two, BMOD_POWER, 4096, false) == 6267);
    // Attack: ATTACK_MULT, PINCH, then the attacker's records (records before PINCH: 6330).
    give(&r.player[0], (BattleAbility){.cls = BACLASS_HOOKS, .count = 3, .source = 102,
                                       .hooks = {mod_hook(modr(BMOD_ATTACK, 102, 0, 0, 0, 0)),
                                                 hk(BABIL_PINCH, T_FIRE, 0, 2, 0, 0, 150, 0),
                                                 hk(BABIL_ATTACK_MULT, 0, 0, 0, 0, 0, 101, 0)}});
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_active(&b, 0)->hp = battle_active(&b, 0)->max_hp / 2;
    assert(mod_at(&b, &plain, BMOD_ATTACK, 4096, false) == 6329);
    // Attack: the attacker's record, then RESIST, then the defender's records (2110 swapped).
    give(&r.player[0], MODS(273, modr(BMOD_ATTACK, 101, 0, 0, 0, 0)));
    give(&r.opponent[0], (BattleAbility){.cls = BACLASS_HOOKS, .count = 2, .source = 47,
                                         .hooks = {mod_hook(modr(BMOD_ATTACK | BMODF_DEFENDER, 102, 0, 0, 0, 0)),
                                                   hk(BABIL_RESIST, T_FIRE, 0, BABIL_NO_TYPE, 0, 0, 50, 0)}});
    assert(battle_init(&b, &r, 1) == BI_OK && mod_at(&b, &plain, BMOD_ATTACK, 4096, false) == 2109);
    // Final: the move's records before the faster Pokemon's (5539 with the ability first).
    BattleMove fin = with_mod(with_mod(plain, modr(BMOD_FINAL, 101, 0, 0, 0, 0)), modr(BMOD_FINAL, 103, 0, 0, 0, 0));
    give(&r.player[0], MODS(274, modr(BMOD_FINAL, 130, 0, 0, 0, 0)));
    give(&r.opponent[0], (BattleAbility){0});
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_active(&b, 0)->speed = 200;
    assert(mod_at(&b, &fin, BMOD_FINAL, 4096, false) == 5540);
    // A same-type bonus that is not a multiple of x0.5 rounds to the nearest in
    // V14 (it floors in V13): some power shows the difference.
    give(&r.player[0], HOOKS(91, hk(BABIL_STAB_MULT, 0, 0, 0, 0, 0, 133, 0)));
    r.player[0].mon.types[0] = r.player[0].mon.types[1] = T_FIRE;
    static BattleRequest r13;  // a Battle keeps a pointer to its request
    static Battle v13;
    assert(battle_init(&b, &r, 1) == BI_OK);
    r13 = r;
    r13.version = BATTLE_REQUEST_VERSION_MODIFIERS;
    assert(battle_init(&v13, &r13, 1) == BI_OK);
    unsigned differ = 0;
    for (uint16_t power = 10; power <= 120; ++power) {
        const BattleMove m = damaging(power, T_FIRE, 100);
        differ += dmg(&b, &m) != dmg(&v13, &m);
        assert(dmg(&b, &m) >= dmg(&v13, &m));
    }
    assert(differ > 0);
}

// Decoding writes the whole 8-byte slot, and before V13 the modifier kind
// keeps its old meaning: refused on a supported move even when its bytes
// would form a valid record, kept on an unsupported one.
static void test_v14_union_decode(void) {
    static BattleRequest r, d;
    static uint8_t bytes[4096];
    make_mods(&r, BATTLE_FORMAT_DOUBLE);
    // pred[1].arg is the union's last byte, which a legacy effect does not use.
    const BattleModifier wide = modr(BMOD_POWER, 150, BPRED_HP_AT_MOST | BPRED_DEFENDER, 2, BPRED_STATUS | BPRED_DEFENDER,
                                     ANY_STATUS);
    set_move(&r.player[0], 0, 506, with_mod(damaging(65, T_FIRE, 100), wide), 10);
    uint32_t n = battle_request_encode(&r, bytes, sizeof bytes);
    assert(n && battle_request_decode(bytes, n, &d) && d.player[0].mon.moves[0].effects[1].mod.pred[1].arg == ANY_STATUS);
    BattleMove stat = damaging(65, T_FIRE, 100);
    push(&stat, fx(BEFF_STAT, BWHO_TARGET, 30, BSTAT_ATK, -1, 0));
    set_move(&r.player[0], 0, 506, stat, 10);
    n = battle_request_encode(&r, bytes, sizeof bytes);
    assert(n && battle_request_decode(bytes, n, &d));
    assert(d.player[0].mon.moves[0].effects[1].kind == BEFF_STAT && d.player[0].mon.moves[0].effects[1].mod.pred[1].arg == 0);
    // V12 bytes {16, 1, 200, 0x82, 0xfe, 0, 0, 0}: as a legacy effect who = 1 and chance = 200 (refused
    // before V13); read as a record they would be valid Hex.
    make_mods(&r, BATTLE_FORMAT_DOUBLE);
    set_move(&r.player[0], 0, 506, with_mod(damaging(65, T_FIRE, 100), HEX_MOD), 10);
    n = battle_request_encode(&r, bytes, sizeof bytes);
    const uint32_t fixed = BATTLE_REQUEST_HEADER + 4 * BATTLE_ENTRY_BYTES + NTYPES * NTYPES + BATTLE_CONDITION_RULE_BYTES;
    uint8_t *q = bytes + fixed + BATTLE_MOVE_EFFECTS_HEADER + BATTLE_EFFECT_BYTES;
    const uint8_t legacy_view[8] = {BEFF_MODIFIER, BMOD_BASE_POWER, 200, BPRED_STATUS | BPRED_DEFENDER, ANY_STATUS, 0, 0, 0};
    memcpy(q, legacy_view, sizeof legacy_view);
    bytes[0] = BATTLE_REQUEST_VERSION_ABILITY_HOOKS;
    assert(!battle_request_decode(bytes, n, &d));
    // The same bytes on an unsupported move decode, as they did in V12.
    r.player[0].mon.moves[0].unsupported = UNSUP_SECONDARY;
    n = battle_request_encode(&r, bytes, sizeof bytes);
    memcpy(q, legacy_view, sizeof legacy_view);
    bytes[0] = BATTLE_REQUEST_VERSION_ABILITY_HOOKS;
    assert(battle_request_decode(bytes, n, &d) && d.player[0].mon.moves[0].effects[1].kind == BEFF_MODIFIER);
}

// ------------------------------------------------------------- V14 against the Expansion engine (docs/modifiers.md)
//
// Each case is a hit the Frontier build's own test runner computes with the
// real Expansion damage code (test/battle/gc_damage_reference.c in the
// Frontier branch, same stats, rolls 100/93/85, no crit). `expansion` holds
// those numbers; V14 must give them exactly. The abilities and move records
// are what the Frontier exporter sends for those abilities and moves. V13
// (the V12 arithmetic) is printed beside them to show what V14 changed.

enum { R_NORMAL, R_FIRE, R_WATER, R_GHOST, R_ICE, R_DRAGON, R_GROUND, R_ROCK, R_FIGHTING, R_FLYING, R_BUG,
       R_STEEL, R_GRASS, R_ELECTRIC, R_PSYCHIC, R_COUNT };

typedef struct {
    uint8_t level, t0, t1;
    uint16_t stat, max_hp, hp, speed;
    uint32_t status;
    BattleAbility ability;
} RefMon;
typedef struct {
    const char *name;
    RefMon atk, def;
    BattleMove move;
    unsigned flags;  // BCD_*
    uint16_t expansion[3];
} RefCase;
static const unsigned ref_rolls[3] = {100, 93, 85};

static void ref_chart(BattleRequest *r) {
    static const uint8_t pairs[][3] = {
        {R_ICE, R_DRAGON, 8}, {R_ICE, R_GROUND, 8}, {R_ICE, R_FLYING, 8}, {R_ICE, R_FIRE, 2}, {R_ICE, R_WATER, 2},
        {R_ICE, R_ICE, 2}, {R_ICE, R_STEEL, 2}, {R_FIRE, R_ROCK, 2}, {R_FIRE, R_WATER, 2}, {R_FIRE, R_FIRE, 2},
        {R_FIRE, R_DRAGON, 2}, {R_FIRE, R_BUG, 8}, {R_FIRE, R_STEEL, 8}, {R_FIRE, R_ICE, 8},
        {R_ROCK, R_FIRE, 8}, {R_ROCK, R_FLYING, 8}, {R_ROCK, R_ICE, 8}, {R_ROCK, R_BUG, 8},
        {R_ROCK, R_FIGHTING, 2}, {R_ROCK, R_GROUND, 2}, {R_ROCK, R_STEEL, 2},
        {R_FIGHTING, R_NORMAL, 8}, {R_FIGHTING, R_ROCK, 8}, {R_FIGHTING, R_ICE, 8}, {R_FIGHTING, R_STEEL, 8},
        {R_FIGHTING, R_FLYING, 2}, {R_FIGHTING, R_BUG, 2}, {R_FIGHTING, R_GHOST, 0},
        {R_GHOST, R_GHOST, 8}, {R_GHOST, R_NORMAL, 0}, {R_WATER, R_FIRE, 8}, {R_WATER, R_ROCK, 8},
        {R_WATER, R_GROUND, 8}, {R_WATER, R_WATER, 2}, {R_WATER, R_DRAGON, 2},
        {R_GROUND, R_FIRE, 8}, {R_GROUND, R_ROCK, 8}, {R_GROUND, R_STEEL, 8}, {R_GROUND, R_FLYING, 0},
        {R_GROUND, R_BUG, 2}, {R_NORMAL, R_ROCK, 2}, {R_NORMAL, R_GHOST, 0}, {R_NORMAL, R_STEEL, 2},
        // V16 cases: the Electric, Grass and Psychic rows they use.
        {R_ELECTRIC, R_ELECTRIC, 2}, {R_ELECTRIC, R_WATER, 8}, {R_ELECTRIC, R_FLYING, 8}, {R_ELECTRIC, R_GROUND, 0},
        {R_GRASS, R_WATER, 8}, {R_GRASS, R_FLYING, 2}, {R_GRASS, R_GRASS, 2}, {R_GRASS, R_FIRE, 2},
        {R_PSYCHIC, R_PSYCHIC, 2}, {R_DRAGON, R_DRAGON, 8},
    };
    r->type_count = R_COUNT;
    memset(r->type_chart, 4, sizeof r->type_chart);
    for (unsigned i = 0; i < sizeof pairs / sizeof *pairs; ++i)
        r->type_chart[pairs[i][0] * R_COUNT + pairs[i][1]] = pairs[i][2];
}

static void ref_mon(BattleEntry *e, uint8_t slot, const RefMon *m) {
    make_entry(e, slot, 1, m->level, m->max_hp, m->stat, m->speed, m->t0);
    e->record.hp = m->hp;
    e->record.status = m->status;
    e->mon.types[1] = m->t1;
    e->mon.type_count = m->t1 != m->t0 ? 2 : 1;
    give(e, m->ability);
}

static uint16_t ref_damage(const RefCase *k, uint8_t version, unsigned roll) {
    static BattleRequest r;
    static Battle b;
    make_hooks(&r, BATTLE_FORMAT_SINGLE);
    r.version = version;
    ref_chart(&r);
    ref_mon(&r.player[0], 0, &k->atk);
    set_move(&r.player[0], 0, 100, k->move, 10);
    ref_mon(&r.opponent[0], 0x80, &k->def);
    set_move(&r.opponent[0], 0, 101, damaging(10, R_NORMAL, 100), 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    const Battler *a = battle_active(&b, 0);
    uint8_t eff;
    uint16_t amount = bc_calculate_damage(&b, a, &a->moves[0], battle_active(&b, 1), roll, false, k->flags, &eff);
    if (version < BATTLE_REQUEST_VERSION_FIXED_POINT) {  // the caller's steps before V14 (battle_effects.c strike)
        if (k->flags & BCD_SPREAD) amount = amount * 3u / 4u ? (uint16_t)(amount * 3u / 4u) : 1u;
        if (k->flags & BCD_DOUBLED) amount = (uint16_t)(amount * 2u);
    }
    return amount;
}

static BattleMove ref_move(uint16_t power, uint8_t type, uint8_t category) {
    BattleMove m = damaging(power, type, 100);
    m.category = category;
    return m;
}

#define REF_FLAGS BAHF_BREAKABLE
#define R_NO_ABILITY ((BattleAbility){.cls = BACLASS_NONE, .source = 0})
#define R_THICK_FAT HOOKS(47, hk(BABIL_RESIST, R_FIRE, 0, R_ICE, 0, 0, 50, REF_FLAGS))
#define R_GUTS HOOKS(62, hk(BABIL_ATTACK_MULT, BCAT_PHYSICAL, 0, 0, 0, 0, 150, BAHF_REQUIRES_STATUS), \
                     hk(BABIL_IGNORE_BURN, 0, 0, 0, 0, 0, 0, 0))
#define R_BLAZE HOOKS(66, hk(BABIL_PINCH, R_FIRE, 0, 3, 0, 0, 150, 0))
#define R_TECHNICIAN HOOKS(101, hk(BABIL_LOW_POWER, 60, 0, 0, 0, 0, 150, 0))
// The game-data records the Frontier exporter adds in V14 (docs/modifiers.md).
#define R_DEFEATIST MODS(129, modr(BMOD_ATTACK, 50, BPRED_HP_AT_MOST, 2, 0, 0))
#define R_FLARE_BOOST MODS(138, modr(ST(BMOD_POWER, BCAT_SPECIAL), 150, BPRED_STATUS, 1u << BST_BURN, 0, 0))
#define R_TOXIC_BOOST MODS(137, modr(ST(BMOD_POWER, BCAT_PHYSICAL), 150, BPRED_STATUS, \
                                     (1u << BST_POISON) | (1u << BST_TOXIC), 0, 0))
#define R_BRINE_MOD modr(BMOD_POWER, 200, BPRED_HP_AT_MOST | BPRED_DEFENDER, 2, 0, 0)
#define R_HEX_MOD HEX_MOD
#define RMON(level, t0, t1, stat, max_hp, hp, speed, status, ability) \
    {level, t0, t1, stat, max_hp, hp, speed, status, ability}

static void test_v14_expansion_reference(void) {
    const RefCase cases[] = {
        {"Gen 5 example: Ice Fang x4 STAB (Glaceon L75 vs Garchomp)",
         RMON(75, R_ICE, R_ICE, 123, 999, 999, 65, 0, R_NO_ABILITY),
         RMON(50, R_DRAGON, R_GROUND, 163, 999, 999, 102, 0, R_NO_ABILITY),
         ref_move(65, R_ICE, BCAT_PHYSICAL), 0, {196, 180, 168}},
        {"Guts (poisoned) x1.5 and Thick Fat x0.5: attack x0.75 (Ice Punch)",
         RMON(50, R_FIGHTING, R_FIGHTING, 101, 999, 999, 55, BATTLE_CONDITION(BST_POISON, 0), R_GUTS),
         RMON(50, R_NORMAL, R_NORMAL, 80, 999, 999, 30, 0, R_THICK_FAT),
         ref_move(75, R_ICE, BCAT_PHYSICAL), 0, {33, 30, 28}},
        {"Blaze at 1/3 HP x1.5 and Thick Fat x0.5, STAB (Fire Punch)",
         RMON(50, R_FIRE, R_FLYING, 103, 150, 50, 100, 0, R_BLAZE),
         RMON(50, R_NORMAL, R_NORMAL, 77, 999, 999, 30, 0, R_THICK_FAT),
         ref_move(75, R_FIRE, BCAT_PHYSICAL), 0, {52, 48, 43}},
        {"Defeatist at 1/2 HP x0.5 and Thick Fat x0.5: attack x0.25 (Fire Punch)",
         RMON(50, R_ROCK, R_FLYING, 111, 150, 75, 110, 0, R_DEFEATIST),
         RMON(50, R_NORMAL, R_NORMAL, 61, 999, 999, 30, 0, R_THICK_FAT),
         ref_move(75, R_FIRE, BCAT_PHYSICAL), 0, {17, 15, 14}},
        {"Double resist x0.25, STAB (Ember vs Omastar)",
         RMON(50, R_FIRE, R_FIRE, 90, 999, 999, 65, 0, R_NO_ABILITY),
         RMON(50, R_ROCK, R_WATER, 57, 999, 999, 55, 0, R_NO_ABILITY),
         ref_move(40, R_FIRE, BCAT_SPECIAL), 0, {11, 10, 9}},
        {"Spread x0.75 before the roll, STAB (Rock Slide in a Double)",
         RMON(50, R_ROCK, R_GROUND, 100, 999, 999, 45, 0, R_NO_ABILITY),
         RMON(50, R_NORMAL, R_NORMAL, 64, 999, 999, 30, 0, R_NO_ABILITY),
         ref_move(75, R_ROCK, BCAT_PHYSICAL), BCD_SPREAD, {60, 55, 51}},
        {"Technician x1.5 per hit (Double Kick, 2 hits)",
         RMON(50, R_BUG, R_STEEL, 130, 999, 999, 65, 0, R_TECHNICIAN),
         RMON(50, R_NORMAL, R_NORMAL, 83, 999, 999, 30, 0, R_NO_ABILITY),
         ref_move(30, R_FIGHTING, BCAT_PHYSICAL), 0, {66, 60, 56}},
        {"Hex against a poisoned target: base power x2, STAB",
         RMON(50, R_GHOST, R_GHOST, 85, 999, 999, 85, 0, R_NO_ABILITY),
         RMON(50, R_FIGHTING, R_FIGHTING, 67, 999, 999, 35, BATTLE_CONDITION(BST_POISON, 0), R_NO_ABILITY),
         with_mod(ref_move(65, R_GHOST, BCAT_SPECIAL), R_HEX_MOD), 0, {111, 102, 93}},
        {"Brine at 1/2 HP x2 then Flare Boost (burned) x1.5",
         RMON(50, R_GHOST, R_FLYING, 90, 999, 999, 80, BATTLE_CONDITION(BST_BURN, 0), R_FLARE_BOOST),
         RMON(50, R_NORMAL, R_NORMAL, 73, 999, 499, 30, 0, R_NO_ABILITY),
         with_mod(ref_move(65, R_WATER, BCAT_SPECIAL), R_BRINE_MOD), 0, {107, 99, 90}},
        {"Toxic Boost (poisoned) x1.5: power 112 (Ice Punch)",
         RMON(50, R_NORMAL, R_NORMAL, 115, 999, 999, 90, BATTLE_CONDITION(BST_POISON, 0), R_TOXIC_BOOST),
         RMON(50, R_NORMAL, R_NORMAL, 79, 999, 999, 30, 0, R_NO_ABILITY),
         ref_move(75, R_ICE, BCAT_PHYSICAL), 0, {73, 67, 62}},
        {"Earthquake on a Pokemon underground: final x2, STAB",
         RMON(50, R_ROCK, R_GROUND, 110, 999, 999, 10, 0, R_NO_ABILITY),
         RMON(50, R_NORMAL, R_NORMAL, 87, 999, 999, 100, 0, R_NO_ABILITY),
         ref_move(100, R_GROUND, BCAT_PHYSICAL), BCD_DOUBLED, {170, 158, 144}},
    };
    unsigned changed = 0;
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; ++i) {
        const RefCase *k = &cases[i];
        printf("  ref %2u %-70s", i + 1, k->name);
        for (unsigned r = 0; r < 3; ++r) {
            const uint16_t v13 = ref_damage(k, BATTLE_REQUEST_VERSION_MODIFIERS, ref_rolls[r]);
            const uint16_t v14 = ref_damage(k, BATTLE_REQUEST_VERSION_FIXED_POINT, ref_rolls[r]);
            printf(" %u%%: v13=%u v14=%u exp=%u", ref_rolls[r], v13, v14, k->expansion[r]);
            changed += v13 != v14;
            assert(v14 == k->expansion[r]);
        }
        printf("\n");
    }
    printf("  ref: V13 and V14 differ in %u of %u values\n", changed, (unsigned)(3 * sizeof cases / sizeof *cases));
}

// ------------------------------------------------------------- V15 weather (docs/weather.md)
//
// The world of the reference tests (R_* types) with the field rules the
// Frontier exporter sends for this Expansion build (Gen 9): sandstorm and
// hail 1/16 with their immune types, sun/rain x1.5 / x0.5 at the weather
// step, the sandstorm Rock Sp. Def and the snow Ice Defense boosts.

#define W_SUN BWEATHER_BIT(BWEATHER_SUN)
#define W_RAIN BWEATHER_BIT(BWEATHER_RAIN)
#define W_SAND BWEATHER_BIT(BWEATHER_SANDSTORM)
#define W_HAIL BWEATHER_BIT(BWEATHER_HAIL)
#define W_SNOW BWEATHER_BIT(BWEATHER_SNOW)
#define W_ICY (W_HAIL | W_SNOW)

static void frontier_field(BattleFieldRules *f) {
    memset(f, 0, sizeof *f);
    for (unsigned k = 0; k < BWEATHER_KINDS; ++k)
        memset(f->weather[k].immune_types, BABIL_NO_TYPE, sizeof f->weather[k].immune_types);
    f->weather[BWEATHER_SUN - 1].flags = BWKF_NO_FREEZE;
    f->weather[BWEATHER_SANDSTORM - 1] = (BattleWeatherKindRules){16, BWKF_UNDERGROUND_SAFE, {R_ROCK, R_GROUND, R_STEEL}};
    f->weather[BWEATHER_HAIL - 1] = (BattleWeatherKindRules){16, BWKF_UNDERGROUND_SAFE,
                                                              {R_ICE, BABIL_NO_TYPE, BABIL_NO_TYPE}};
    const BattleModifier records[] = {
        modr(BMOD_WEATHER, 150, BPRED_WEATHER, W_SUN, BPRED_MOVE_TYPE, R_FIRE),
        modr(BMOD_WEATHER, 50, BPRED_WEATHER, W_SUN, BPRED_MOVE_TYPE, R_WATER),
        modr(BMOD_WEATHER, 150, BPRED_WEATHER, W_RAIN, BPRED_MOVE_TYPE, R_WATER),
        modr(BMOD_WEATHER, 50, BPRED_WEATHER, W_RAIN, BPRED_MOVE_TYPE, R_FIRE),
        modr(ST(BMOD_DEFENSE, BCAT_SPECIAL), 150, BPRED_WEATHER, W_SAND, BPRED_HAS_TYPE | BPRED_DEFENDER, R_ROCK),
        modr(ST(BMOD_DEFENSE, BCAT_PHYSICAL), 150, BPRED_WEATHER, W_SNOW, BPRED_HAS_TYPE | BPRED_DEFENDER, R_ICE),
    };
    f->modifier_count = sizeof records / sizeof *records;
    for (unsigned i = 0; i < f->modifier_count; ++i) f->modifiers[i] = records[i];
}

static void make_weather(BattleRequest *r, uint8_t format) {
    make_mods(r, format);
    r->version = BATTLE_REQUEST_VERSION_WEATHER;
    ref_chart(r);
    frontier_field(&r->field);
}

// The abilities as the Frontier exporter resolves them (ids are diagnostic only).
#define W_HOOK(kind, arg0, arg1, arg2, flags) hk(kind, arg0, arg1, arg2, 0, 0, 0, flags)
#define W_SETTER(id, kind) HOOKS(id, W_HOOK(BABIL_ENTRY_WEATHER, kind, 0, 5, 0))
#define W_DRIZZLE W_SETTER(2, BWEATHER_RAIN)
#define W_DROUGHT W_SETTER(70, BWEATHER_SUN)
#define W_SAND_STREAM W_SETTER(45, BWEATHER_SANDSTORM)
#define W_SNOW_WARNING W_SETTER(117, BWEATHER_SNOW)
#define W_CLOUD_NINE HOOKS(13, W_HOOK(BABIL_WEATHER_SUPPRESS, 0, 0, 0, 0))
#define W_SWIFT_SWIM MODS(33, modr(BMOD_SPEED, 200, BPRED_WEATHER, W_RAIN, 0, 0))
#define W_SAND_VEIL HOOKS(8, W_HOOK(BABIL_WEATHER_IMMUNE, W_SAND, 0, 0, 0), \
                          mod_hook(modr(ST(BMOD_ACCURACY, 0) | BMODF_DEFENDER, 80, BPRED_WEATHER, W_SAND, 0, 0)))
#define W_RAIN_DISH HOOKS(44, W_HOOK(BABIL_WEATHER_RESIDUAL, W_RAIN, 1, 16, 0))
#define W_ICE_BODY HOOKS(115, W_HOOK(BABIL_WEATHER_RESIDUAL, W_ICY, 1, 16, BAHF_NOT_UNDERGROUND))
#define W_DRY_SKIN HOOKS(87, hk(BABIL_TYPE_ABSORB, R_WATER, 0, 0, 0, 0, 25, 0), \
                         mod_hook(modr(ST(BMOD_POWER, 0) | BMODF_DEFENDER, 125, BPRED_MOVE_TYPE, R_FIRE, 0, 0)), \
                         W_HOOK(BABIL_WEATHER_RESIDUAL, W_RAIN, 1, 8, 0), W_HOOK(BABIL_WEATHER_RESIDUAL, W_SUN, -1, 8, 0))
#define W_SOLAR_POWER HOOKS(94, mod_hook(modr(ST(BMOD_ATTACK, BCAT_SPECIAL), 150, BPRED_WEATHER, W_SUN, 0, 0)), \
                            W_HOOK(BABIL_WEATHER_RESIDUAL, W_SUN, -1, 8, 0))
#define W_SAND_FORCE HOOKS(159, W_HOOK(BABIL_WEATHER_IMMUNE, W_SAND, 0, 0, 0), \
                           mod_hook(modr(BMOD_POWER, 130, BPRED_WEATHER, W_SAND, BPRED_MOVE_TYPE, R_ROCK)), \
                           mod_hook(modr(BMOD_POWER, 130, BPRED_WEATHER, W_SAND, BPRED_MOVE_TYPE, R_GROUND)), \
                           mod_hook(modr(BMOD_POWER, 130, BPRED_WEATHER, W_SAND, BPRED_MOVE_TYPE, R_STEEL)))
#define W_OVERCOAT HOOKS(142, W_HOOK(BABIL_WEATHER_IMMUNE, W_SAND | W_HAIL, 0, 0, 0))

static BattleMove weather_move(uint8_t kind, uint8_t turns) {
    BattleMove m = status_move(BTARGET_SELF, 0);
    push(&m, fx(BEFF_WEATHER, BWHO_USER, 0, kind, 0, turns));
    return m;
}

static BattleMove w_special(uint16_t power, uint8_t type, uint8_t accuracy) {
    BattleMove m = damaging(power, type, accuracy);
    m.category = BCAT_SPECIAL;
    return m;
}

// Thunder / Hurricane: never misses in rain, 50% in sun.
static BattleMove w_thunder(void) {
    BattleMove m = w_special(110, R_FLYING, 70);
    push(&m, fx(BEFF_WEATHER_ACCURACY, 0, 0, W_RAIN, 0, 0));
    push(&m, fx(BEFF_WEATHER_ACCURACY, 0, 0, W_SUN, 0, 50));
    return m;
}

// Solar Beam: no charging turn in sun, half power in rain, sandstorm, hail, snow.
static BattleMove w_solar_beam(void) {
    BattleMove m = charge_move(120, BSEMI_NONE);
    m.category = BCAT_SPECIAL;
    m.effects[0].arg2 = W_SUN;
    push(&m, mod_effect(modr(BMOD_POWER, 50, BPRED_WEATHER, W_RAIN | W_SAND | W_ICY, 0, 0)));
    return m;
}

// Weather Ball: its type follows the weather, base power x2 in any weather.
static BattleMove w_weather_ball(void) {
    BattleMove m = w_special(50, R_NORMAL, 100);
    push(&m, fx(BEFF_WEATHER_TYPE, 0, 0, W_SUN, 0, R_FIRE));
    push(&m, fx(BEFF_WEATHER_TYPE, 0, 0, W_RAIN, 0, R_WATER));
    push(&m, fx(BEFF_WEATHER_TYPE, 0, 0, W_SAND, 0, R_ROCK));
    push(&m, fx(BEFF_WEATHER_TYPE, 0, 0, W_ICY, 0, R_ICE));
    push(&m, mod_effect(modr(BMOD_BASE_POWER, 200, BPRED_WEATHER, BWEATHER_ALL, 0, 0)));
    return m;
}

// Moonlight: 1/2, 2/3 (20/30) in sun, 1/4 in any other weather.
static BattleMove w_moonlight(void) {
    BattleMove m = status_move(BTARGET_SELF, 0);
    push(&m, fx(BEFF_HEAL, BWHO_USER, 0, 1, 0, 2));
    push(&m, fx(BEFF_WEATHER_HEAL, BWHO_USER, 0, W_SUN, 20, 30));
    push(&m, fx(BEFF_WEATHER_HEAL, BWHO_USER, 0, W_RAIN | W_SAND | W_ICY, 1, 4));
    return m;
}

static const BattleEvent *find_event(const EventLog *log, EventKind kind, unsigned nth) {
    for (unsigned i = 0; i < log->count; ++i)
        if (log->events[i].kind == kind && nth-- == 0) return &log->events[i];
    return NULL;
}

static int event_index(const EventLog *log, EventKind kind, unsigned nth) {
    const BattleEvent *e = find_event(log, kind, nth);
    return e ? (int)(e - log->events) : -1;
}

static void test_v15_wire(void) {
    static BattleRequest r, d;
    static uint8_t bytes[4096];
    make_weather(&r, BATTLE_FORMAT_DOUBLE);
    give(&r.player[0], W_DRIZZLE);
    give(&r.player[1], W_SAND_VEIL);
    give(&r.opponent[0], W_DRY_SKIN);
    give(&r.opponent[1], W_SWIFT_SWIM);
    set_move(&r.player[0], 1, 240, weather_move(BWEATHER_RAIN, 5), 5);
    set_move(&r.player[1], 1, 87, w_thunder(), 10);
    set_move(&r.opponent[0], 1, 76, w_solar_beam(), 10);
    set_move(&r.opponent[1], 1, 311, w_weather_ball(), 10);
    set_move(&r.opponent[1], 2, 236, w_moonlight(), 5);
    r.field.start_weather = BWEATHER_HAIL;
    r.field.start_turns = 3;
    r.rules |= RULE_STRICT_MOVES;
    uint32_t n = battle_request_encode(&r, bytes, sizeof bytes);
    assert(n && n == battle_request_size(&r));
    assert(battle_request_decode(bytes, n, &d));
    assert(memcmp(&d.field, &r.field, sizeof r.field) == 0 && d.rules == r.rules);
    assert(d.opponent[1].mon.moves[1].effect_count == 6 && d.opponent[0].mon.moves[1].effects[0].arg2 == W_SUN);
    // The field rules close the request: V14 size + 88.
    BattleRequest v14 = r;
    v14.version = BATTLE_REQUEST_VERSION_FIXED_POINT;
    v14.rules &= (uint16_t)~RULE_STRICT_MOVES;
    for (unsigned i = 0; i < 2; ++i) give(&v14.player[i], (BattleAbility){.cls = BACLASS_NONE, .source = 1});
    for (unsigned i = 0; i < 2; ++i) give(&v14.opponent[i], (BattleAbility){.cls = BACLASS_NONE, .source = 1});
    v14.player[0].mon.moves[1].unsupported = v14.player[1].mon.moves[1].unsupported = UNSUP_EFFECT;
    v14.opponent[0].mon.moves[1].unsupported = v14.opponent[1].mon.moves[1].unsupported = UNSUP_EFFECT;
    v14.opponent[1].mon.moves[2].unsupported = UNSUP_EFFECT;
    BattleRequest same = v14;
    same.version = BATTLE_REQUEST_VERSION_WEATHER;
    assert(battle_request_size(&same) == battle_request_size(&v14) + BATTLE_FIELD_RULE_BYTES);
    // No V15 content can be written into, or read from, an earlier request.
    BattleRequest old = r;
    old.version = BATTLE_REQUEST_VERSION_FIXED_POINT;
    old.rules &= (uint16_t)~RULE_STRICT_MOVES;
    assert(battle_request_encode(&old, bytes, sizeof bytes) == 0);  // weather hooks and kinds
    n = battle_request_encode(&r, bytes, sizeof bytes);
    bytes[0] = BATTLE_REQUEST_VERSION_FIXED_POINT;
    assert(!battle_request_decode(bytes, n, &d));
    // V15 kinds are refused by the V14 checks; accepted by V15's.
    assert(!battle_move_effects_valid_for(&r.player[0].mon.moves[1], BATTLE_REQUEST_VERSION_FIXED_POINT));
    assert(battle_move_effects_valid_for(&r.player[0].mon.moves[1], BATTLE_REQUEST_VERSION_WEATHER));
    const BattleMove v15_moves[] = {w_thunder(), w_solar_beam(), w_weather_ball(), w_moonlight()};
    for (unsigned i = 0; i < 4; ++i) {
        assert(battle_move_effects_valid_for(&v15_moves[i], BATTLE_REQUEST_VERSION_WEATHER));
        assert(!battle_move_effects_valid_for(&v15_moves[i], BATTLE_REQUEST_VERSION_FIXED_POINT));
    }
    const BattleAbility v15_abilities[] = {W_DRIZZLE, W_CLOUD_NINE, W_SWIFT_SWIM, W_SAND_VEIL, W_ICE_BODY, W_DRY_SKIN,
                                           W_SOLAR_POWER, W_SAND_FORCE, W_OVERCOAT};
    for (unsigned i = 0; i < sizeof v15_abilities / sizeof *v15_abilities; ++i) {
        assert(battle_ability_valid(&v15_abilities[i], R_COUNT, BATTLE_REQUEST_VERSION_WEATHER));
        assert(!battle_ability_valid(&v15_abilities[i], R_COUNT, BATTLE_REQUEST_VERSION_FIXED_POINT));
    }
    // The new stages and predicates exist only from V15, and only where they belong.
    const BattleModifier speed = modr(BMOD_SPEED, 200, BPRED_WEATHER, W_RAIN, 0, 0);
    assert(battle_modifier_valid(&speed, R_COUNT, BMSRC_ABILITY, BATTLE_REQUEST_VERSION_WEATHER));
    assert(!battle_modifier_valid(&speed, R_COUNT, BMSRC_ABILITY, BATTLE_REQUEST_VERSION_FIXED_POINT));
    assert(!battle_modifier_valid(&speed, R_COUNT, BMSRC_MOVE, BATTLE_REQUEST_VERSION_WEATHER));
    assert(!battle_modifier_valid(&speed, R_COUNT, BMSRC_FIELD, BATTLE_REQUEST_VERSION_WEATHER));
    const BattleModifier bad_mods[] = {
        modr(BMOD_SPEED | BMODF_DEFENDER, 200, 0, 0, 0, 0),                       // no defender at SPEED
        modr(ST(BMOD_SPEED, BCAT_PHYSICAL), 200, 0, 0, 0, 0),                     // no category at SPEED
        modr(BMOD_SPEED, 200, BPRED_MOVE_TYPE, R_FIRE, 0, 0),                     // no move at SPEED
        modr(BMOD_SPEED, 200, BPRED_HP_AT_MOST | BPRED_DEFENDER, 2, 0, 0),        // only the holder
        modr(BMOD_POWER, 130, BPRED_WEATHER, 0, 0, 0),                            // empty weather mask
        modr(BMOD_POWER, 130, BPRED_WEATHER, (uint8_t)(BWEATHER_ALL + 1), 0, 0),  // unknown weather
        modr(BMOD_POWER, 130, BPRED_WEATHER | BPRED_DEFENDER, W_RAIN, 0, 0),      // weather has no subject
        modr(BMOD_POWER, 130, BPRED_HAS_TYPE, R_COUNT, 0, 0),                     // type out of range
        modr(BMOD_POWER, 130, BPRED_COUNT, 0, 0, 0),                              // unknown predicate
        modr(ST(BMOD_COUNT, 0), 100, 0, 0, 0, 0),                                 // unknown stage
        modr(BMOD_WEATHER, 150, BPRED_WEATHER, W_SUN, 0, 0),                      // WEATHER: field only
    };
    for (unsigned i = 0; i < sizeof bad_mods / sizeof *bad_mods; ++i)
        assert(!battle_modifier_valid(&bad_mods[i], R_COUNT, BMSRC_ABILITY, BATTLE_REQUEST_VERSION_WEATHER));
    const BattleModifier field_ok = modr(BMOD_WEATHER, 150, BPRED_WEATHER, W_SUN, BPRED_MOVE_TYPE, R_FIRE);
    BattleModifier field_bad = field_ok;
    field_bad.stage |= BMODF_DEFENDER;
    assert(battle_modifier_valid(&field_ok, R_COUNT, BMSRC_FIELD, BATTLE_REQUEST_VERSION_WEATHER));
    assert(!battle_modifier_valid(&field_bad, R_COUNT, BMSRC_FIELD, BATTLE_REQUEST_VERSION_WEATHER));
    assert(!battle_modifier_valid(&field_ok, R_COUNT, BMSRC_FIELD, BATTLE_REQUEST_VERSION_FIXED_POINT));
    // Field records exist only from V15, whatever their stage.
    const BattleModifier field_def = modr(ST(BMOD_DEFENSE, BCAT_SPECIAL), 150, BPRED_STATUS, ANY_STATUS, 0, 0);
    assert(battle_modifier_valid(&field_def, R_COUNT, BMSRC_FIELD, BATTLE_REQUEST_VERSION_WEATHER));
    assert(!battle_modifier_valid(&field_def, R_COUNT, BMSRC_FIELD, BATTLE_REQUEST_VERSION_FIXED_POINT));
    const BattleModifier field_power = modr(BMOD_POWER, 130, 0, 0, 0, 0);  // terrain later (M4-T)
    assert(!battle_modifier_valid(&field_power, R_COUNT, BMSRC_FIELD, BATTLE_REQUEST_VERSION_WEATHER));
    // The stage byte: bit 5 is stage bit 3.
    assert(battle_modifier_stage(ST(BMOD_ACCURACY, BCAT_SPECIAL)) == BMOD_ACCURACY &&
           ST(BMOD_ACCURACY, 0) == ((BMOD_ACCURACY & 7u) | BMOD_STAGE_HIGH) &&
           battle_modifier_stage(ST(BMOD_FINAL, BCAT_PHYSICAL)) == BMOD_FINAL);
    // Malformed field rules.
    const uint32_t base = n - BATTLE_FIELD_RULE_BYTES;
    n = battle_request_encode(&r, bytes, sizeof bytes);
    assert(battle_request_decode(bytes, n, &d));
    const struct { unsigned at; uint8_t value; } bad[] = {
        {0, 2}, {1, BWEATHER_COUNT}, {2, BWEATHER_MAX_TURNS + 1}, {3, BATTLE_MAX_FIELD_MODIFIERS + 1}, {3, 7},
        {4, 1}, {5, 4}, {6, R_COUNT}, {29, 1}, {31, 1}, {32 + 2, 1},
        {32 + 6 * BATTLE_MODIFIER_BODY_BYTES, BMOD_WEATHER},  // a record past the count
        {32, BMOD_SPEED}, {32 + 3, BPRED_COUNT},
    };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; ++i) {
        const uint8_t keep = bytes[base + bad[i].at];
        bytes[base + bad[i].at] = bad[i].value;
        assert(!battle_request_decode(bytes, n, &d));
        bytes[base + bad[i].at] = keep;
    }
    assert(battle_request_decode(bytes, n, &d));
    // A start weather needs a kind; no turns or lock without one.
    BattleFieldRules f = r.field;
    f.start_weather = BWEATHER_NONE;
    assert(!battle_field_rules_valid(&f, R_COUNT));
    f.start_turns = 0;
    assert(battle_field_rules_valid(&f, R_COUNT));
    f.flags = BFR_START_LOCKED;
    assert(!battle_field_rules_valid(&f, R_COUNT));
    // Effect and hook fields.
    BattleMove m = weather_move(BWEATHER_SUN, 5);
    m.effects[0].arg2 = BWEATHER_TURNS_KEEP;
    assert(battle_move_effects_valid(&m));
    m.effects[0].arg2 = 16;
    assert(!battle_move_effects_valid(&m));
    m = weather_move(BWEATHER_COUNT, 5);
    assert(!battle_move_effects_valid(&m));
    m = weather_move(BWEATHER_SUN, 5);
    push(&m, fx(BEFF_STAT, BWHO_USER, 0, BSTAT_ATK, 1, 0));
    assert(!battle_move_effects_valid(&m));  // a weather move stands alone
    m = w_moonlight();
    m.effects[1].arg1 = 31;                   // more than the denominator
    assert(!battle_move_effects_valid(&m));
    m = w_weather_ball();
    m.effects[1].arg0 = 0;                    // no weather
    assert(!battle_move_effects_valid(&m));
    m = w_thunder();
    m.effects[1].arg2 = 101;
    assert(!battle_move_effects_valid(&m));
    BattleAbilityHook h = W_HOOK(BABIL_WEATHER_RESIDUAL, W_RAIN, 2, 16, 0);
    assert(!battle_ability_hook_valid(&h, R_COUNT, BATTLE_REQUEST_VERSION_WEATHER));
    h = W_HOOK(BABIL_WEATHER_RESIDUAL, W_RAIN, -1, 1, 0);
    assert(!battle_ability_hook_valid(&h, R_COUNT, BATTLE_REQUEST_VERSION_WEATHER));
    h = W_HOOK(BABIL_WEATHER_IMMUNE, W_RAIN, 0, 0, BAHF_NOT_UNDERGROUND);
    assert(!battle_ability_hook_valid(&h, R_COUNT, BATTLE_REQUEST_VERSION_WEATHER));
    h = W_HOOK(BABIL_ENTRY_WEATHER, BWEATHER_SNOW, 0, BWEATHER_TURNS_KEEP, BAHF_BREAKABLE);
    assert(battle_ability_hook_valid(&h, R_COUNT, BATTLE_REQUEST_VERSION_WEATHER));
    h.arg0 = 0;
    assert(!battle_ability_hook_valid(&h, R_COUNT, BATTLE_REQUEST_VERSION_WEATHER));
    // The V15 rule bit is malformed in V14.
    make_weather(&old, BATTLE_FORMAT_SINGLE);
    old.version = BATTLE_REQUEST_VERSION_FIXED_POINT;
    old.rules |= RULE_STRICT_MOVES;
    assert(battle_request_encode(&old, bytes, sizeof bytes) == 0);
    old.rules &= (uint16_t)~RULE_STRICT_MOVES;
    n = battle_request_encode(&old, bytes, sizeof bytes);
    assert(n);
    msg_put16(bytes + 6, (uint16_t)(old.rules | RULE_STRICT_MOVES));
    assert(!battle_request_decode(bytes, n, &d));
    // The kind names.
    assert(!strcmp(battle_effect_kind_name(BEFF_WEATHER_HEAL), "weather_heal") &&
           !strcmp(battle_ability_kind_name(BABIL_WEATHER_RESIDUAL), "weather_residual"));
}

// Start, same weather again, replacement and the end after its turns.
static void test_v15_set_and_end(void) {
    static BattleRequest r;
    static Battle b;
    static EventLog log;
    make_weather(&r, BATTLE_FORMAT_SINGLE);
    r.player[0].record.speed = 50;
    r.opponent[0].record.speed = 90;
    r.player[0].mon.moves[0].power = r.opponent[0].mon.moves[0].power = 1;
    set_move(&r.player[0], 1, 240, weather_move(BWEATHER_RAIN, 5), 10);
    set_move(&r.opponent[0], 1, 241, weather_move(BWEATHER_SUN, 5), 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(!log.count && battle_weather(&b) == BWEATHER_NONE);
    Action rain = move(0, 0, 0), sun = move(1, 0, 0), hit_p = move(0, 0, 0), hit_f = move(1, 0, 0);
    rain.move_slot = 1;
    sun.move_slot = 1;
    run1(&b, rain, hit_f, &log);
    const BattleEvent *e = find_event(&log, EV_WEATHER_START, 0);
    assert(e && e->condition == BWEATHER_RAIN && e->amount == 5 && e->user.side == 0 && e->user.slot == 0);
    e = find_event(&log, EV_WEATHER_CONTINUE, 0);
    assert(e && e->amount == 4 && battle_weather(&b) == BWEATHER_RAIN);
    assert(battle_field_weather(&b)->source_kind == FSRC_MOVE);
    // The same weather again fails and keeps the count.
    run1(&b, rain, hit_f, &log);
    assert(event_count(&log, EV_FAIL) == 1 && !find_event(&log, EV_WEATHER_START, 0));
    assert(battle_field_weather(&b)->turns == 3);
    // Another weather replaces it with a fresh count.
    run1(&b, hit_p, sun, &log);
    e = find_event(&log, EV_WEATHER_START, 0);
    assert(e && e->condition == BWEATHER_SUN && e->user.side == 1 && battle_field_weather(&b)->turns == 4);
    // Four more turn ends: the last one ends it before any weather effect.
    for (unsigned t = 0; t < 3; ++t) {
        run1(&b, hit_p, hit_f, &log);
        assert(find_event(&log, EV_WEATHER_CONTINUE, 0) && battle_weather(&b) == BWEATHER_SUN);
    }
    run1(&b, hit_p, hit_f, &log);
    e = find_event(&log, EV_WEATHER_END, 0);
    assert(e && e->condition == BWEATHER_SUN && !find_event(&log, EV_WEATHER_CONTINUE, 0));
    assert(battle_weather(&b) == BWEATHER_NONE && !battle_field_weather(&b)->kind);
    // KEEP: the counter of the weather before stays (no weather before: no end).
    set_move(&r.player[0], 1, 240, weather_move(BWEATHER_RAIN, BWEATHER_TURNS_KEEP), 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    run1(&b, rain, sun, &log);  // the faster foe's sun (5), then rain keeping its 5
    assert(battle_weather(&b) == BWEATHER_RAIN && battle_field_weather(&b)->turns == 4);
    // A permanent battle-start weather from the rules, before any entry ability.
    r.field.start_weather = BWEATHER_SANDSTORM;
    r.field.start_turns = 0;
    give(&r.player[0], W_DRIZZLE);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(event_index(&log, EV_WEATHER_START, 0) < event_index(&log, EV_ABILITY, 0));
    assert(log.events[event_index(&log, EV_WEATHER_START, 0)].user.slot == BATTLE_SLOT_NONE);
    assert(battle_weather(&b) == BWEATHER_RAIN);  // Drizzle replaced it
    // Locked (overworld, Gen 9): Drizzle shows the block, Rain Dance fails.
    r.field.flags = BFR_START_LOCKED;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(event_count(&log, EV_ABILITY_BLOCK) == 1 && battle_weather(&b) == BWEATHER_SANDSTORM);
    run1(&b, rain, hit_f, &log);
    assert(event_count(&log, EV_FAIL) == 1 && battle_weather(&b) == BWEATHER_SANDSTORM);
    assert(find_event(&log, EV_WEATHER_CONTINUE, 0)->amount == 0);  // no end
}

// Entry setters: speed order, the same weather, replacements together, switching back in.
static void test_v15_entry(void) {
    static BattleRequest r;
    static Battle b;
    static EventLog log;
    make_weather(&r, BATTLE_FORMAT_DOUBLE);
    give(&r.player[0], W_DRIZZLE);
    give(&r.opponent[0], W_DROUGHT);
    r.player[0].record.speed = 100;
    r.opponent[0].record.speed = 50;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    // Faster first: rain, then the slower Drought's sun stays.
    assert(event_count(&log, EV_WEATHER_START) == 2 && find_event(&log, EV_WEATHER_START, 0)->condition == BWEATHER_RAIN);
    assert(event_index(&log, EV_ABILITY, 0) < event_index(&log, EV_WEATHER_START, 0));
    assert(battle_weather(&b) == BWEATHER_SUN && battle_field_weather(&b)->source_kind == FSRC_ABILITY);
    r.player[0].record.speed = 40;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(battle_weather(&b) == BWEATHER_RAIN);
    // The same weather twice: the second does nothing (no restart, no notice).
    give(&r.opponent[0], W_DRIZZLE);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(event_count(&log, EV_WEATHER_START) == 1 && event_count(&log, EV_ABILITY) == 1);
    // Replacements after a turn enter together (V15): entry abilities after the
    // last one, in speed order. Each side's lead is down; reserves are setters.
    give(&r.opponent[0], W_DROUGHT);
    add_reserve(&r, 0);
    add_reserve(&r, 1);
    give(&r.player[2], W_SNOW_WARNING);
    give(&r.opponent[2], W_SAND_STREAM);
    r.player[2].record.speed = 30;   // slower: its snow stays
    r.opponent[2].record.speed = 120;
    for (unsigned v = 0; v < 2; ++v) {
        r.version = v ? BATTLE_REQUEST_VERSION_FIXED_POINT : BATTLE_REQUEST_VERSION_WEATHER;
        if (v) {  // V14 has no weather hooks: plain abilities, immediate entry (no draw)
            give(&r.player[2], INTIMIDATE_ADJ);
            give(&r.opponent[2], INTIMIDATE_ADJ);
            give(&r.player[0], (BattleAbility){.cls = BACLASS_NONE, .source = 1});
            give(&r.opponent[0], (BattleAbility){.cls = BACLASS_NONE, .source = 1});
        }
        assert(battle_init(&b, &r, 3) == BI_OK);
        battle_begin(&b, &log);
        for (uint8_t side = 0; side < 2; ++side) {
            Battler *x = at_pos(&b, side, 0);
            x->hp = 0;
            x->fainted = true;
            b.sides[side].active[0] = BATTLE_SLOT_NONE;
        }
        Position p;
        assert(battle_pending_replacement(&b, &p) && p.side == 0);
        assert(battle_replace(&b, p, 2, &log));
        if (!v) {
            assert(log.count == 1 && log.events[0].kind == EV_SEND_OUT);  // waits for the foe's
        } else {
            assert(event_count(&log, EV_ABILITY) == 1);                   // V14: at once
        }
        assert(battle_pending_replacement(&b, &p) && p.side == 1);
        assert(battle_replace(&b, p, 2, &log));
        if (!v) {
            const BattleEvent *first = find_event(&log, EV_WEATHER_START, 0), *second = find_event(&log, EV_WEATHER_START, 1);
            assert(first && second && first->condition == BWEATHER_SANDSTORM && second->condition == BWEATHER_SNOW);
            assert(battle_weather(&b) == BWEATHER_SNOW);
        }
    }
    // A Pokemon switched out and in again sets its weather again.
    make_weather(&r, BATTLE_FORMAT_SINGLE);
    add_reserve(&r, 0);
    give(&r.player[0], W_DRIZZLE);
    r.player[0].record.speed = 10;
    set_move(&r.opponent[0], 1, 241, weather_move(BWEATHER_SUN, 5), 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    Action sun = move(1, 0, 0), out = {.kind = ACT_SWITCH, .user = {0, 0}, .reserve = 1};
    sun.move_slot = 1;
    run1(&b, out, sun, &log);
    assert(battle_weather(&b) == BWEATHER_SUN);
    Action back = {.kind = ACT_SWITCH, .user = {0, 0}, .reserve = 0};
    run1(&b, back, move(1, 0, 0), &log);
    assert(battle_weather(&b) == BWEATHER_RAIN && event_count(&log, EV_WEATHER_START) == 1);
}

// Cloud Nine / Air Lock: the weather stays and counts down without effect.
static void test_v15_suppress(void) {
    static BattleRequest r;
    static Battle b;
    static EventLog log;
    make_weather(&r, BATTLE_FORMAT_SINGLE);
    add_reserve(&r, 0);
    give(&r.player[0], W_CLOUD_NINE);
    give(&r.opponent[0], W_SAND_STREAM);
    r.opponent[0].mon.types[0] = R_NORMAL;  // not immune to its own sandstorm
    r.player[0].record.speed = 100;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(event_count(&log, EV_ABILITY) == 2);  // the notice, then the setter
    assert(battle_field_weather(&b)->kind == BWEATHER_SANDSTORM && battle_weather(&b) == BWEATHER_NONE);
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    assert(find_event(&log, EV_WEATHER_CONTINUE, 0)->amount == 4 && !event_count(&log, EV_WEATHER_DAMAGE));
    // Cloud Nine leaves: the weather acts again at once.
    Action out = {.kind = ACT_SWITCH, .user = {0, 0}, .reserve = 1};
    run1(&b, out, move(1, 0, 0), &log);
    assert(battle_weather(&b) == BWEATHER_SANDSTORM && event_count(&log, EV_WEATHER_DAMAGE) == 2);
    // A fainted holder suppresses nothing.
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    Battler *holder = at_pos(&b, 0, 0);
    holder->fainted = true;
    assert(battle_weather(&b) == BWEATHER_SANDSTORM);
    holder->fainted = false;
    assert(battle_weather(&b) == BWEATHER_NONE);
}

// The weather damage step: after the spread factor, before the random factor.
static uint16_t w_damage(const RefMon *atk, const RefMon *def, BattleMove move, unsigned flags, uint8_t weather,
                         unsigned roll) {
    static BattleRequest r;
    static Battle b;
    make_weather(&r, BATTLE_FORMAT_SINGLE);
    ref_mon(&r.player[0], 0, atk);
    set_move(&r.player[0], 0, 100, move, 10);
    ref_mon(&r.opponent[0], 0x80, def);
    set_move(&r.opponent[0], 0, 101, damaging(10, R_NORMAL, 100), 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    b.field.weather = (FieldEffect){.kind = weather, .turns = 5};
    const Battler *a = battle_active(&b, 0);
    BattleMove m = a->moves[0];
    m.type = bc_weather_move_type(&b, &m);  // as bc_execute_move does
    uint8_t eff;
    return bc_calculate_damage(&b, a, &m, battle_active(&b, 1), roll, false, flags, &eff);
}

static void test_v15_damage(void) {
    const RefMon pelipper = RMON(50, R_WATER, R_FLYING, 100, 999, 999, 65, 0, R_NO_ABILITY);
    const RefMon snorlax = RMON(50, R_NORMAL, R_NORMAL, 100, 999, 999, 30, 0, R_NO_ABILITY);
    const BattleMove surf = ref_move(90, R_WATER, BCAT_SPECIAL), flamethrower = ref_move(90, R_FIRE, BCAT_SPECIAL);
    // Base 41; rain x1.5 = 61.5 -> 61 (an exact half rounds down) before the
    // roll, then STAB: 91 / 84 / 76. Applied after the roll the 93% hit would
    // be 41 x0.93 = 38, x1.5 = 57, STAB 85.
    assert(w_damage(&pelipper, &snorlax, surf, 0, BWEATHER_RAIN, 100) == 91);
    assert(w_damage(&pelipper, &snorlax, surf, 0, BWEATHER_RAIN, 93) == 84);
    assert(w_damage(&pelipper, &snorlax, surf, 0, BWEATHER_RAIN, 85) == 76);
    assert(w_damage(&pelipper, &snorlax, surf, 0, BWEATHER_NONE, 100) == 61);
    assert(w_damage(&pelipper, &snorlax, surf, 0, BWEATHER_SUN, 100) == 30);       // 41 x0.5 = 20.5 -> 20, x1.5
    assert(w_damage(&pelipper, &snorlax, flamethrower, 0, BWEATHER_RAIN, 100) == 20);
    assert(w_damage(&pelipper, &snorlax, flamethrower, 0, BWEATHER_SUN, 100) == 61);  // 41 x1.5 = 61.5 -> 61
    // Other weathers and other types: no weather factor.
    for (uint8_t w = BWEATHER_SANDSTORM; w <= BWEATHER_SNOW; ++w)
        assert(w_damage(&pelipper, &snorlax, surf, 0, w, 100) == 61);
    assert(w_damage(&pelipper, &snorlax, ref_move(90, R_NORMAL, BCAT_SPECIAL), 0, BWEATHER_RAIN, 100) == 41);
    // Spread first: 41 x0.75 = 30.75 -> 31, x1.5 = 46.5 -> 46, then STAB 69.
    assert(w_damage(&pelipper, &snorlax, surf, BCD_SPREAD, BWEATHER_RAIN, 100) == 69);
    // Sandstorm: a Rock type's Sp. Def x1.5 (special moves only), in the defending stat's chain.
    const RefMon golem = RMON(50, R_ROCK, R_GROUND, 100, 999, 999, 45, 0, R_NO_ABILITY);
    const uint16_t plain = w_damage(&pelipper, &golem, surf, 0, BWEATHER_NONE, 100);
    const uint16_t sand = w_damage(&pelipper, &golem, surf, 0, BWEATHER_SANDSTORM, 100);
    assert(sand < plain && w_damage(&pelipper, &golem, ref_move(90, R_WATER, BCAT_PHYSICAL), 0, BWEATHER_SANDSTORM,
                                    100) == w_damage(&pelipper, &golem, ref_move(90, R_WATER, BCAT_PHYSICAL), 0,
                                                     BWEATHER_NONE, 100));
    // Snow: an Ice type's Defense x1.5 (physical only); hail does not.
    const RefMon glalie = RMON(50, R_ICE, R_ICE, 100, 999, 999, 50, 0, R_NO_ABILITY);
    const BattleMove body_slam = ref_move(85, R_NORMAL, BCAT_PHYSICAL);
    assert(w_damage(&snorlax, &glalie, body_slam, 0, BWEATHER_SNOW, 100) <
           w_damage(&snorlax, &glalie, body_slam, 0, BWEATHER_HAIL, 100));
    assert(w_damage(&snorlax, &glalie, ref_move(85, R_NORMAL, BCAT_SPECIAL), 0, BWEATHER_SNOW, 100) ==
           w_damage(&snorlax, &glalie, ref_move(85, R_NORMAL, BCAT_SPECIAL), 0, BWEATHER_NONE, 100));
    // Solar Beam: power x0.5 in rain (POWER chain); Weather Ball: Fire 100 in sun (and the sun boost).
    assert(w_damage(&pelipper, &snorlax, w_solar_beam(), 0, BWEATHER_RAIN, 100) ==
           w_damage(&pelipper, &snorlax, ref_move(60, R_NORMAL, BCAT_SPECIAL), 0, BWEATHER_NONE, 100));
    const RefMon ninetales = RMON(50, R_FIRE, R_FIRE, 100, 999, 999, 100, 0, R_NO_ABILITY);
    assert(w_damage(&ninetales, &snorlax, w_weather_ball(), 0, BWEATHER_SUN, 100) ==
           w_damage(&ninetales, &snorlax, ref_move(100, R_FIRE, BCAT_SPECIAL), 0, BWEATHER_SUN, 100));
    assert(w_damage(&ninetales, &snorlax, w_weather_ball(), 0, BWEATHER_NONE, 100) ==
           w_damage(&ninetales, &snorlax, ref_move(50, R_NORMAL, BCAT_SPECIAL), 0, BWEATHER_NONE, 100));
    // The weather step is one factor: only the first matching field record
    // applies (GetWeatherDamageModifier returns), never a chain.
    {
        static BattleRequest r;
        static Battle b;
        make_weather(&r, BATTLE_FORMAT_SINGLE);
        ref_mon(&r.player[0], 0, &pelipper);
        set_move(&r.player[0], 0, 100, surf, 10);
        ref_mon(&r.opponent[0], 0x80, &snorlax);
        r.field.modifiers[r.field.modifier_count++] = modr(BMOD_WEATHER, 200, BPRED_WEATHER, W_RAIN, 0, 0);
        assert(battle_init(&b, &r, 1) == BI_OK);
        b.field.weather = (FieldEffect){.kind = BWEATHER_RAIN, .turns = 5};
        uint8_t eff;
        const Battler *a = battle_active(&b, 0);
        assert(bc_calculate_damage(&b, a, &a->moves[0], battle_active(&b, 1), 100, false, 0, &eff) == 91);
    }
    // Suppressed: Cloud Nine on the field, no weather effect at all.
    RefMon golduck = snorlax;
    golduck.ability = W_CLOUD_NINE;
    assert(w_damage(&pelipper, &golduck, surf, 0, BWEATHER_RAIN, 100) == 61);
}

// Accuracy in a weather (Thunder, Blizzard) and the evasion abilities (Sand Veil).
static void test_v15_accuracy(void) {
    static BattleRequest r;
    static Battle b;
    make_weather(&r, BATTLE_FORMAT_SINGLE);
    set_move(&r.player[0], 0, 87, w_thunder(), 10);
    give(&r.opponent[0], W_SAND_VEIL);
    assert(battle_init(&b, &r, 7) == BI_OK);
    const Position user = {0, 0}, target = {1, 0};
    const BattleMove *thunder = &battle_active(&b, 0)->moves[0];
    const struct { uint8_t weather; unsigned expected; } cases[] = {
        {BWEATHER_NONE, 70}, {BWEATHER_SUN, 50}, {BWEATHER_SANDSTORM, 56}, {BWEATHER_HAIL, 70},
    };
    for (unsigned c = 0; c < 4; ++c) {
        b.field.weather = (FieldEffect){.kind = cases[c].weather, .turns = 5};
        unsigned hits = 0;
        for (unsigned i = 0; i < 4000; ++i) {
            const uint32_t before = b.rng;
            const bool missed = bc_missed(&b, user, target, thunder);
            b.rng = before;
            const unsigned roll = bc_next_random(&b) % 100u;
            assert(missed == (roll >= cases[c].expected));  // exactly the threshold
            hits += !missed;
        }
        assert(hits > 0);
    }
    // Rain: never misses, without a draw.
    b.field.weather = (FieldEffect){.kind = BWEATHER_RAIN, .turns = 5};
    const uint32_t before = b.rng;
    assert(!bc_missed(&b, user, target, thunder) && b.rng == before);
    // V15 accuracy stages use the game's table: -4 is 43/100 (3/7 was 42).
    b.field.weather.kind = BWEATHER_NONE;
    BattleMove plain = damaging(40, R_NORMAL, 100);
    battle_active(&b, 1)->stat_stages[BSTAT_EVA] = 4;
    for (unsigned i = 0; i < 200; ++i) {
        const uint32_t s = b.rng;
        const bool missed = bc_missed(&b, user, target, &plain);
        b.rng = s;
        assert(missed == (bc_next_random(&b) % 100u >= 43));
    }
}

// Speed abilities change the turn order (dynamic speed) and come before paralysis.
static void test_v15_speed_order(void) {
    static BattleRequest r;
    static Battle b;
    static EventLog log;
    make_weather(&r, BATTLE_FORMAT_DOUBLE);
    give(&r.player[0], W_SWIFT_SWIM);
    r.player[0].record.speed = 51;
    r.player[1].record.speed = 200;
    r.opponent[0].record.speed = 90;
    r.opponent[1].record.speed = 80;
    set_move(&r.player[1], 1, 240, weather_move(BWEATHER_RAIN, 5), 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    Action rain = move(0, 1, 0);
    rain.move_slot = 1;
    const Action turn[] = {move(0, 0, 0), rain, move(1, 0, 0), move(1, 1, 0)};
    battle_run_turn(&b, turn, 4, &log);
    // Rain Dance first (200), then Swift Swim's 102 before the foes' 90 and 80.
    const BattleEvent *used[4];
    for (unsigned i = 0; i < 4; ++i) used[i] = find_event(&log, EV_MOVE_USED, i);
    assert(used[0]->user.side == 0 && used[0]->user.slot == 1);
    assert(used[1]->user.side == 0 && used[1]->user.slot == 0);
    assert(used[2]->user.side == 1 && used[2]->user.slot == 0);
    // Next turn: the sorted speed is 102 at the start; paralysis halves after the x2: 51.
    at_pos(&b, 0, 0)->status = BST_PARALYSIS;
    battle_run_turn(&b, (Action[]){move(0, 0, 0), move(0, 1, 0), move(1, 0, 0), move(1, 1, 0)}, 4, &log);
    unsigned speed = 0;
    for (unsigned i = 0; i < b.resolution.count; ++i)
        if (b.resolution.actions[i].action.user.side == 0 && b.resolution.actions[i].battler == 0)
            speed = b.resolution.actions[i].speed;
    assert(speed == 51);  // (51 x2) / 2, not (51 / 2) x2 = 50
}

// End of turn: the countdown, then each Pokemon's weather residual in speed order.
static void test_v15_residual(void) {
    static BattleRequest r;
    static Battle b;
    static EventLog log;
    make_weather(&r, BATTLE_FORMAT_TRIPLE);
    // Sandstorm: Normal 1/16, Rock immune, Sand Veil / Overcoat immune.
    for (unsigned side = 0; side < 2; ++side)
        for (unsigned i = 0; i < 3; ++i) {
            BattleEntry *e = side ? &r.opponent[i] : &r.player[i];
            e->record.hp = e->record.max_hp = 160;
            e->record.speed = (uint16_t)(100 - 10 * (3 * side + i));
        }
    r.player[1].mon.types[0] = R_ROCK;
    give(&r.player[2], W_SAND_VEIL);
    give(&r.opponent[0], W_OVERCOAT);
    give(&r.opponent[1], W_ICE_BODY);
    give(&r.opponent[2], W_RAIN_DISH);
    r.field.start_weather = BWEATHER_SANDSTORM;
    r.field.start_turns = 5;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    const Action idle[] = {move(0, 0, 0), move(0, 1, 0), move(0, 2, 0), move(1, 0, 0), move(1, 1, 0), move(1, 2, 0)};
    for (unsigned i = 0; i < 6; ++i) b.sides[i / 3].battlers[i % 3].moves[0].power = 1;  // keep HP readable
    battle_run_turn(&b, idle, 6, &log);
    const int cont = event_index(&log, EV_WEATHER_CONTINUE, 0);
    assert(cont >= 0 && event_count(&log, EV_WEATHER_DAMAGE) == 3);  // player 0, foe 1 (Ice Body: no heal), foe 2
    for (unsigned i = 0; i < 3; ++i) {
        const BattleEvent *e = find_event(&log, EV_WEATHER_DAMAGE, i);
        assert(e->condition == BWEATHER_SANDSTORM && e->amount == 10 && (int)(e - log.events) > cont);
    }
    assert(find_event(&log, EV_WEATHER_DAMAGE, 0)->target.side == 0 && find_event(&log, EV_WEATHER_DAMAGE, 1)->target.side == 1);
    // Hail: Ice Body heals 1/16 (not at full HP; not underground) instead of taking damage.
    b.field.weather = (FieldEffect){.kind = BWEATHER_HAIL, .turns = 5};
    Battler *ice = at_pos(&b, 1, 1);
    ice->hp = 100;
    battle_run_turn(&b, idle, 6, &log);
    const BattleEvent *heal = find_event(&log, EV_HEAL, 0);
    assert(heal && heal->target.side == 1 && heal->target.slot == 1 && heal->amount == 10 && ice->hp > 100);
    assert(event_count(&log, EV_WEATHER_DAMAGE) == 4);  // Overcoat and Ice Body immune; Sand Veil not in hail
    ice->vol.semi_invulnerable = BSEMI_UNDERGROUND;
    const uint16_t ice_hp = ice->hp;
    b.field.weather.turns = 5;
    static EventLog end;
    end.count = 0;
    Position order[6];
    unsigned n = 0;
    for (uint8_t side = 0; side < 2; ++side)
        for (uint8_t slot = 0; slot < 3; ++slot) order[n++] = (Position){side, slot};
    bc_weather_end_turn(&b, order, n, &end);
    assert(ice->hp == ice_hp && !find_event(&end, EV_HEAL, 0));
    ice->vol.semi_invulnerable = BSEMI_NONE;
    // Underground is safe from the damage too (BWKF_UNDERGROUND_SAFE).
    at_pos(&b, 0, 0)->vol.semi_invulnerable = BSEMI_UNDERGROUND;
    end.count = 0;
    bc_weather_end_turn(&b, order, n, &end);
    for (unsigned i = 0; i < end.count; ++i)
        assert(!(end.events[i].kind == EV_WEATHER_DAMAGE && end.events[i].target.side == 0 && end.events[i].target.slot == 0));
    at_pos(&b, 0, 0)->vol.semi_invulnerable = BSEMI_NONE;
    // Rain: Rain Dish heals 1/16; Dry Skin 1/8; sun: Dry Skin and Solar Power lose 1/8.
    make_weather(&r, BATTLE_FORMAT_DOUBLE);
    for (unsigned i = 0; i < 2; ++i) r.player[i].record.max_hp = 160, r.opponent[i].record.max_hp = 160;
    for (unsigned i = 0; i < 2; ++i) r.player[i].record.hp = 80, r.opponent[i].record.hp = 80;
    give(&r.player[0], W_RAIN_DISH);
    give(&r.player[1], W_DRY_SKIN);
    give(&r.opponent[0], W_SOLAR_POWER);
    assert(battle_init(&b, &r, 1) == BI_OK);
    b.field.weather = (FieldEffect){.kind = BWEATHER_RAIN, .turns = 5};
    n = battle_collect_actors(&b, 0, order);
    n += battle_collect_actors(&b, 1, order + n);
    end.count = 0;
    bc_weather_end_turn(&b, order, n, &end);
    assert(at_pos(&b, 0, 0)->hp == 90 && at_pos(&b, 0, 1)->hp == 100 && at_pos(&b, 1, 0)->hp == 80);
    b.field.weather.kind = BWEATHER_SUN;
    end.count = 0;
    bc_weather_end_turn(&b, order, n, &end);
    assert(at_pos(&b, 0, 0)->hp == 90 && at_pos(&b, 0, 1)->hp == 80 && at_pos(&b, 1, 0)->hp == 60);
    assert(event_count(&end, EV_ABILITY_DAMAGE) == 2 && event_count(&end, EV_ABILITY) == 2);
    // Weather damage can end the battle: the last foe faints at the turn end.
    make_weather(&r, BATTLE_FORMAT_SINGLE);
    r.opponent[0].record.hp = 5;
    r.field.start_weather = BWEATHER_SANDSTORM;
    r.field.start_turns = 5;
    r.player[0].mon.types[0] = R_STEEL;
    for (unsigned i = 0; i < 1; ++i) r.player[i].mon.moves[0].power = 1, r.opponent[i].mon.moves[0].power = 1;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    assert(b.outcome == OUTCOME_WIN && event_count(&log, EV_FAINT) == 1);
    assert(event_index(&log, EV_WEATHER_DAMAGE, 0) < event_index(&log, EV_FAINT, 0));
}

// Moves in a weather: Solar Beam's charge, Weather Ball, Moonlight.
static void test_v15_moves(void) {
    static BattleRequest r;
    static Battle b;
    static EventLog log;
    make_weather(&r, BATTLE_FORMAT_SINGLE);
    r.player[0].record.speed = 100;
    set_move(&r.player[0], 0, 76, w_solar_beam(), 10);
    set_move(&r.player[0], 1, 236, w_moonlight(), 10);
    set_move(&r.player[0], 2, 311, w_weather_ball(), 10);
    r.opponent[0].mon.moves[0].power = 1;
    r.player[0].record.max_hp = 200;
    r.player[0].record.hp = 50;
    give(&r.opponent[0], W_DRY_SKIN);  // absorbs a Water-type Weather Ball
    assert(battle_init(&b, &r, 1) == BI_OK);
    b.field.weather = (FieldEffect){.kind = BWEATHER_SUN, .turns = 5};
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);  // Solar Beam fires at once in sun
    assert(!event_count(&log, EV_CHARGE) && event_count(&log, EV_DAMAGE) >= 1 && battle_active(&b, 0)->pp[0] == 9);
    assert(battle_action_state(&b, (Position){0, 0}) == BAS_READY);
    b.field.weather.kind = BWEATHER_RAIN;
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);  // charges in rain
    assert(event_count(&log, EV_CHARGE) == 1 && battle_action_state(&b, (Position){0, 0}) == BAS_CHARGING);
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    assert(battle_action_state(&b, (Position){0, 0}) == BAS_READY);
    // Moonlight: 2/3 in sun, 1/4 in rain, 1/2 without.
    const struct { uint8_t weather; uint16_t heal; } heals[] = {
        {BWEATHER_SUN, 133}, {BWEATHER_RAIN, 50}, {BWEATHER_SANDSTORM, 50}, {BWEATHER_NONE, 100}};
    for (unsigned i = 0; i < 4; ++i) {
        b.field.weather = (FieldEffect){.kind = heals[i].weather, .turns = heals[i].weather ? 5 : 0};
        battle_active(&b, 0)->hp = 50;
        Action moon = move(0, 0, 0);
        moon.move_slot = 1;
        run1(&b, moon, move(1, 0, 0), &log);
        const BattleEvent *e = find_event(&log, EV_HEAL, 0);
        assert(e && e->target.side == 0 && e->amount == heals[i].heal);
    }
    // Weather Ball turns Water in rain: Dry Skin absorbs it (the type counts everywhere).
    b.field.weather = (FieldEffect){.kind = BWEATHER_RAIN, .turns = 5};
    Battler *foe = battle_active(&b, 1);
    foe->hp = foe->max_hp / 2;
    Action ball = move(0, 0, 0);
    ball.move_slot = 2;
    run1(&b, ball, move(1, 0, 0), &log);
    assert(!event_count(&log, EV_DAMAGE) || find_event(&log, EV_DAMAGE, 0)->target.side == 0);
    const BattleEvent *absorbed = find_event(&log, EV_HEAL, 0);
    assert(absorbed && absorbed->target.side == 1);
    // In sun it is Fire: Dry Skin takes x1.25 power and x1.5 from the sun.
    b.field.weather.kind = BWEATHER_SUN;
    run1(&b, ball, move(1, 0, 0), &log);
    const BattleEvent *hit = find_event(&log, EV_DAMAGE, 0);
    assert(hit && hit->target.side == 1);
}

// Harsh sunlight: no freezing (BWKF_NO_FREEZE), as in this build's status code.
static void test_v15_no_freeze(void) {
    static BattleRequest r;
    static Battle b;
    static EventLog log;
    make_weather(&r, BATTLE_FORMAT_SINGLE);
    r.player[0].record.speed = 100;
    BattleMove freeze = status_move(BTARGET_SINGLE, 0);
    push(&freeze, fx(BEFF_STATUS, BWHO_TARGET, 0, BST_FREEZE, 0, 0));
    set_move(&r.player[0], 0, 58, freeze, 10);
    r.opponent[0].mon.moves[0].power = 1;
    assert(battle_init(&b, &r, 1) == BI_OK);
    b.field.weather = (FieldEffect){.kind = BWEATHER_SUN, .turns = 5};
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    assert(!event_count(&log, EV_STATUS) && event_count(&log, EV_NO_EFFECT) == 1 && !battle_active(&b, 1)->status);
    b.field.weather.kind = BWEATHER_RAIN;
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    assert(event_count(&log, EV_STATUS) == 1 && find_event(&log, EV_STATUS, 0)->condition == BST_FREEZE);  // (it may thaw at once)
}

static void test_v15_strict_moves(void) {
    static BattleRequest r;
    Battle b;
    make_weather(&r, BATTLE_FORMAT_SINGLE);
    BattleMove unknown = hit(60, R_NORMAL, BCAT_SPECIAL);
    unknown.unsupported = UNSUP_EFFECT;
    set_move(&r.opponent[0], 1, 999, unknown, 10);
    assert(battle_init(&b, &r, 1) == BI_OK);        // development: the move cannot be chosen
    r.rules |= RULE_STRICT_MOVES;
    assert(battle_init(&b, &r, 1) == BI_UNSUPPORTED_MOVE);
    assert(!strcmp(battle_init_error_name(BI_UNSUPPORTED_MOVE), "strict rules: a move is not simulated"));
    r.opponent[0].mon.moves[1].unsupported = UNSUP_EMPTY;  // an empty slot is no move
    r.opponent[0].record.moves[1] = 0;
    assert(battle_init(&b, &r, 1) == BI_OK);
}

// Random weather battles in every format: the same request and seed give the
// same events, state and RNG; HP stays in range; the weather events happen.
static void test_v15_formats(void) {
    static BattleRequest r;
    static Battle a, b;
    static EventLog la, lb;
    const BattleAbility pool[] = {W_DRIZZLE, W_DROUGHT, W_SAND_STREAM, W_SNOW_WARNING, W_CLOUD_NINE, W_SWIFT_SWIM,
                                  W_SAND_VEIL, W_ICE_BODY, W_DRY_SKIN, W_SOLAR_POWER, W_SAND_FORCE, W_OVERCOAT,
                                  W_RAIN_DISH, R_NO_ABILITY};
    const unsigned n = sizeof pool / sizeof *pool;
    const uint8_t formats[] = {BATTLE_FORMAT_SINGLE, BATTLE_FORMAT_DOUBLE, BATTLE_FORMAT_TRIPLE, BATTLE_FORMAT_ROTATION};
    unsigned weather_events[4] = {0}, battles = 0;
    for (unsigned f = 0; f < 4; ++f)
        for (uint32_t seed = 1; seed <= 60; ++seed) {
            make_weather(&r, formats[f]);
            for (unsigned side = 0; side < 2; ++side) add_reserve(&r, side);
            for (unsigned side = 0; side < 2; ++side) {
                BattleEntry *e = side ? r.opponent : r.player;
                const unsigned count = side ? r.opponent_count : r.player_count;
                for (unsigned i = 0; i < count; ++i) {
                    give(&e[i], pool[(seed * 7 + side * 5 + i + f) % n]);
                    e[i].record.hp = e[i].record.max_hp = 90;
                    e[i].mon.types[0] = (uint8_t)((seed + 3 * i + side) % R_COUNT);
                    e[i].record.speed = (uint16_t)(40 + (seed * 13 + i * 7 + side * 3) % 60);
                    const uint8_t kind = (uint8_t)(1 + (seed + i + side) % BWEATHER_KINDS);
                    set_move(&e[i], 1, 240 + kind, weather_move(kind, (seed & 1) ? 5 : BWEATHER_TURNS_KEEP), 8);
                    BattleMove extra = (seed + i) % 3 == 0 ? w_thunder() : (seed + i) % 3 == 1 ? w_weather_ball()
                                                                                               : w_solar_beam();
                    set_move(&e[i], 2, 300 + i, extra, 10);
                    BattleMove spread = w_special(45, R_FIRE, 90);
                    spread.target = BTARGET_ALL_FOES;
                    set_move(&e[i], 3, 400 + i, (seed + i) % 4 ? spread : w_moonlight(), 10);
                }
            }
            r.field.start_weather = (uint8_t)(seed % 4 == 0 ? 1 + seed % BWEATHER_KINDS : 0);
            r.field.start_turns = r.field.start_weather ? (uint8_t)(seed % 3) : 0;
            assert(battle_init(&a, &r, seed) == BI_OK && battle_init(&b, &r, seed) == BI_OK);
            battle_begin(&a, &la);
            battle_begin(&b, &lb);
            assert(la.count == lb.count && memcmp(la.events, lb.events, la.count * sizeof(BattleEvent)) == 0);
            for (unsigned t = 0; t < 60 && a.outcome == OUTCOME_NONE; ++t) {
                Position pending;
                while (battle_pending_replacement(&a, &pending)) {
                    const uint8_t mask = battle_replacement_mask(&a, pending);
                    uint8_t pick = 0;
                    while (!(mask & (1u << pick))) ++pick;
                    assert(battle_replace(&a, pending, pick, &la) && battle_replace(&b, pending, pick, &lb));
                    assert(la.count == lb.count && memcmp(la.events, lb.events, la.count * sizeof(BattleEvent)) == 0);
                }
                if (a.outcome != OUTCOME_NONE) break;
                Action actions[BC_MAX_ACTIONS];
                unsigned count = 0;
                for (uint8_t side = 0; side < 2; ++side) {
                    Position actors[BC_MAX_ACTIVE];
                    const unsigned k = battle_collect_actors(&a, side, actors);
                    for (unsigned i = 0; i < k; ++i) battle_choose_at(&a, actors[i], &actions[count++]);
                }
                for (unsigned i = 0; i < count; ++i) {
                    Action dummy;
                    battle_choose_at(&b, actions[i].user, &dummy);
                }
                battle_run_turn(&a, actions, count, &la);
                battle_run_turn(&b, actions, count, &lb);
                assert(la.count == lb.count && la.count < BC_MAX_EVENTS &&
                       memcmp(la.events, lb.events, la.count * sizeof(BattleEvent)) == 0);
                for (unsigned i = 0; i < la.count; ++i)
                    if (la.events[i].kind >= EV_WEATHER_START) ++weather_events[la.events[i].kind - EV_WEATHER_START];
                for (unsigned side = 0; side < 2; ++side)
                    for (unsigned i = 0; i < a.sides[side].count; ++i) {
                        const Battler *x = &a.sides[side].battlers[i];
                        assert(x->hp <= x->max_hp && (x->hp == 0) == x->fainted);
                    }
                assert(a.field.weather.kind < BWEATHER_COUNT && a.field.weather.turns <= BWEATHER_MAX_TURNS);
            }
            assert(memcmp(&a.sides, &b.sides, sizeof a.sides) == 0 && a.rng == b.rng &&
                   memcmp(&a.field, &b.field, sizeof a.field) == 0);
            ++battles;
        }
    for (unsigned i = 0; i < 4; ++i) assert(weather_events[i] > 20);
    printf("v15 formats: %u battles, weather start %u continue %u end %u damage %u\n", battles, weather_events[0],
           weather_events[1], weather_events[2], weather_events[3]);
}

// ------------------------------------------------------------- V15 against the Expansion engine
//
// Hits the Frontier build's test runner computes with the real Expansion
// code in a weather (test/battle/gc_weather_reference.c in frontier-gc-m4w,
// same stats, rolls 100/93/85, no crit; the weather is set by an ability).
// The build keeps the Gen 3 powers of Surf, Flamethrower (95) and Heat Wave (100).
typedef struct {
    const char *name;
    RefMon atk, def;
    BattleMove move;
    unsigned flags;
    uint8_t weather;
    uint16_t expansion[3];
} WeatherRefCase;

static void test_v15_expansion_reference(void) {
    const WeatherRefCase cases[] = {
        {"Rain x1.5 on Surf before the roll, STAB (Pelipper vs Snorlax)",
         RMON(50, R_WATER, R_FLYING, 100, 999, 999, 65, 0, W_DRIZZLE),
         RMON(50, R_NORMAL, R_NORMAL, 100, 999, 999, 30, 0, R_NO_ABILITY),
         ref_move(95, R_WATER, BCAT_SPECIAL), 0, BWEATHER_RAIN, {96, 88, 81}},
        {"Rain x0.5 on Flamethrower (Pelipper vs Snorlax)",
         RMON(50, R_WATER, R_FLYING, 100, 999, 999, 65, 0, W_DRIZZLE),
         RMON(50, R_NORMAL, R_NORMAL, 100, 999, 999, 30, 0, R_NO_ABILITY),
         ref_move(95, R_FIRE, BCAT_SPECIAL), 0, BWEATHER_RAIN, {21, 19, 17}},
        {"Sun after the spread x0.75: Heat Wave (100), STAB (Torkoal, Double)",
         RMON(50, R_FIRE, R_FIRE, 85, 999, 999, 20, 0, W_DROUGHT),
         RMON(50, R_NORMAL, R_NORMAL, 70, 999, 999, 30, 0, R_NO_ABILITY),
         ref_move(100, R_FIRE, BCAT_SPECIAL), BCD_SPREAD, BWEATHER_SUN, {91, 84, 76}},
        {"Sandstorm: a Rock type's Sp. Def x1.5 (Surf vs Golem)",
         RMON(50, R_ROCK, R_ROCK, 95, 999, 999, 61, 0, W_SAND_STREAM),
         RMON(50, R_ROCK, R_GROUND, 65, 999, 999, 45, 0, R_NO_ABILITY),
         ref_move(95, R_WATER, BCAT_SPECIAL), 0, BWEATHER_SANDSTORM, {168, 156, 140}},
        {"Snow: an Ice type's Defense x1.5 (Body Slam vs Glalie)",
         RMON(50, R_ICE, R_ICE, 92, 999, 999, 60, 0, W_SNOW_WARNING),
         RMON(50, R_ICE, R_ICE, 80, 999, 999, 80, 0, R_NO_ABILITY),
         ref_move(85, R_NORMAL, BCAT_PHYSICAL), 0, BWEATHER_SNOW, {30, 27, 25}},
        {"Solar Beam in rain: power x0.5 (Pelipper vs Snorlax)",
         RMON(50, R_WATER, R_FLYING, 100, 999, 999, 65, 0, W_DRIZZLE),
         RMON(50, R_NORMAL, R_NORMAL, 100, 999, 999, 30, 0, R_NO_ABILITY),
         w_solar_beam(), 0, BWEATHER_RAIN, {28, 26, 23}},
        {"Weather Ball in sun: Fire 100, STAB and sun x1.5 (Ninetales)",
         RMON(50, R_FIRE, R_FIRE, 101, 999, 999, 120, 0, W_DROUGHT),
         RMON(50, R_NORMAL, R_NORMAL, 110, 999, 999, 30, 0, R_NO_ABILITY),
         w_weather_ball(), 0, BWEATHER_SUN, {94, 87, 79}},
        {"Sand Force x1.3: Rock Slide in sandstorm (Excadrill vs Hippowdon)",
         RMON(50, R_GROUND, R_STEEL, 155, 999, 999, 108, 0, W_SAND_FORCE),
         RMON(50, R_GROUND, R_GROUND, 138, 999, 999, 67, 0, W_SAND_STREAM),
         ref_move(75, R_ROCK, BCAT_PHYSICAL), 0, BWEATHER_SANDSTORM, {25, 23, 21}},
        {"Solar Power x1.5 and sun x1.5: Flamethrower, STAB (Charizard vs Ninetales)",
         RMON(50, R_FIRE, R_FLYING, 129, 999, 999, 120, 0, W_SOLAR_POWER),
         RMON(50, R_FIRE, R_FIRE, 120, 999, 999, 100, 0, W_DROUGHT),
         ref_move(95, R_FIRE, BCAT_SPECIAL), 0, BWEATHER_SUN, {77, 71, 65}},
        {"Dry Skin x1.25 power and sun x1.5: Flamethrower, STAB (Ninetales vs Toxicroak)",
         RMON(50, R_FIRE, R_FIRE, 101, 999, 999, 120, 0, W_DROUGHT),
         RMON(50, R_FIGHTING, R_FIGHTING, 85, 999, 999, 105, 0, W_DRY_SKIN),
         ref_move(95, R_FIRE, BCAT_SPECIAL), 0, BWEATHER_SUN, {144, 133, 121}},
        {"Cloud Nine on the field: no rain boost (Pelipper vs Golduck)",
         RMON(50, R_WATER, R_FLYING, 100, 999, 999, 65, 0, W_DRIZZLE),
         RMON(50, R_WATER, R_WATER, 100, 999, 999, 105, 0, W_CLOUD_NINE),
         ref_move(95, R_WATER, BCAT_SPECIAL), 0, BWEATHER_RAIN, {32, 29, 27}},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; ++i) {
        const WeatherRefCase *k = &cases[i];
        printf("  wref %2u %-72s", i + 1, k->name);
        for (unsigned r = 0; r < 3; ++r) {
            const uint16_t v15 = w_damage(&k->atk, &k->def, k->move, k->flags, k->weather, ref_rolls[r]);
            const uint16_t none = w_damage(&k->atk, &k->def, k->move, k->flags, BWEATHER_NONE, ref_rolls[r]);
            printf(" %u%%: v15=%u none=%u exp=%u", ref_rolls[r], v15, none, k->expansion[r]);
            assert(v15 == k->expansion[r]);
        }
        printf("\n");
    }
}

// ------------------------------------------------------------- V16 terrain (docs/terrain.md)
//
// The weather world of the V15 tests plus the terrain rules the Frontier
// exporter sends for this build (Gen 9: type boost x1.3): Flying is the
// airborne type; Electric Terrain stops sleep, Grassy heals 1/16, Misty
// stops major status and confusion, Psychic stops foes' priority moves; the
// power records in this build's order (grassy, misty, electric, psychic).

#define TB(kind) BTERRAIN_BIT(kind)
#define T_ELEC TB(BTERRAIN_ELECTRIC)
#define T_GRASS TB(BTERRAIN_GRASSY)
#define T_MIST TB(BTERRAIN_MISTY)
#define T_PSY TB(BTERRAIN_PSYCHIC)

static void frontier_terrain(BattleTerrainRules *t, uint8_t boost) {
    memset(t, 0, sizeof *t);
    t->airborne_type = R_FLYING;
    t->terrain[BTERRAIN_ELECTRIC - 1].flags = BTKF_NO_SLEEP;
    t->terrain[BTERRAIN_GRASSY - 1].heal_divisor = 16;
    t->terrain[BTERRAIN_MISTY - 1].flags = BTKF_NO_STATUS | BTKF_NO_CONFUSION;
    t->terrain[BTERRAIN_PSYCHIC - 1].flags = BTKF_BLOCK_PRIORITY;
    const BattleModifier records[] = {
        modr(BMOD_POWER, boost, BPRED_ON_TERRAIN, T_GRASS, BPRED_MOVE_TYPE, R_GRASS),
        modr(BMOD_POWER, 50, BPRED_ON_TERRAIN | BPRED_DEFENDER, T_MIST, BPRED_MOVE_TYPE, R_DRAGON),
        modr(BMOD_POWER, boost, BPRED_ON_TERRAIN, T_ELEC, BPRED_MOVE_TYPE, R_ELECTRIC),
        modr(BMOD_POWER, boost, BPRED_ON_TERRAIN, T_PSY, BPRED_MOVE_TYPE, R_PSYCHIC),
    };
    t->modifier_count = sizeof records / sizeof *records;
    for (unsigned i = 0; i < t->modifier_count; ++i) t->modifiers[i] = records[i];
}

static void make_terrain(BattleRequest *r, uint8_t format) {
    make_weather(r, format);
    r->version = BATTLE_REQUEST_VERSION_TERRAIN;
    frontier_terrain(&r->terrain, 130);
}

#define T_SURGE(id, kind) HOOKS(id, W_HOOK(BABIL_ENTRY_TERRAIN, kind, 0, 5, 0))
#define T_ELECTRIC_SURGE T_SURGE(226, BTERRAIN_ELECTRIC)
#define T_GRASSY_SURGE T_SURGE(229, BTERRAIN_GRASSY)
#define T_MISTY_SURGE T_SURGE(228, BTERRAIN_MISTY)
#define T_PSYCHIC_SURGE T_SURGE(227, BTERRAIN_PSYCHIC)
#define T_LEVITATE HOOKS(26, hk(BABIL_TYPE_IMMUNE, R_GROUND, 0, 0, 0, 0, 0, 0), W_HOOK(BABIL_UNGROUNDED, 0, 0, 0, 0))
#define T_SURGE_SURFER MODS(207, modr(BMOD_SPEED, 200, BPRED_TERRAIN, T_ELEC, 0, 0))
#define T_GRASS_PELT MODS(179, modr(ST(BMOD_DEFENSE, BCAT_PHYSICAL) | BMODF_DEFENDER, 150, BPRED_TERRAIN, T_GRASS, 0, 0))

static BattleMove terrain_move(uint8_t kind) {
    BattleMove m = status_move(BTARGET_SELF, 0);
    push(&m, fx(BEFF_TERRAIN, BWHO_USER, 0, kind, 0, 5));
    return m;
}

// Earthquake / Bulldoze: x0.5 power on Grassy Terrain unless the target is
// semi-invulnerable (then it doubles against an underground one).
static BattleMove t_earthquake(void) {
    BattleMove m = damaging(100, R_GROUND, 100);
    m.move_flags = BMF_DOUBLE_UNDERGROUND;
    push(&m, mod_effect(modr(BMOD_POWER, 50, BPRED_TERRAIN, T_GRASS, BPRED_EXPOSED | BPRED_DEFENDER, 0)));
    return m;
}

static BattleMove t_priority(uint16_t power, int8_t priority) {
    BattleMove m = damaging(power, R_NORMAL, 100);
    m.priority = priority;
    return m;
}

static void test_v16_wire(void) {
    static BattleRequest r, d, old;
    static uint8_t bytes[4096];
    make_terrain(&r, BATTLE_FORMAT_DOUBLE);
    give(&r.player[0], T_ELECTRIC_SURGE);
    give(&r.player[1], T_LEVITATE);
    give(&r.opponent[0], T_GRASS_PELT);
    give(&r.opponent[1], T_SURGE_SURFER);
    set_move(&r.player[0], 1, 604, terrain_move(BTERRAIN_ELECTRIC), 10);
    set_move(&r.opponent[0], 1, 89, t_earthquake(), 10);
    r.terrain.start_terrain = BTERRAIN_MISTY;
    r.terrain.start_turns = 0;
    uint32_t n = battle_request_encode(&r, bytes, sizeof bytes);
    assert(n && n == battle_request_size(&r));
    assert(battle_request_decode(bytes, n, &d));
    assert(memcmp(&d.terrain, &r.terrain, sizeof r.terrain) == 0 && memcmp(&d.field, &r.field, sizeof r.field) == 0);
    // The terrain rules close the request: V15 size + 72.
    old = r;
    old.version = BATTLE_REQUEST_VERSION_WEATHER;
    assert(battle_request_size(&r) == battle_request_size(&old) + BATTLE_TERRAIN_RULE_BYTES);
    assert(battle_request_encode(&old, bytes, sizeof bytes) == 0);  // V16 kinds cannot be written as V15
    n = battle_request_encode(&r, bytes, sizeof bytes);
    bytes[0] = BATTLE_REQUEST_VERSION_WEATHER;
    assert(!battle_request_decode(bytes, n, &d));
    // Version gates of every new value.
    const BattleMove tm = terrain_move(BTERRAIN_GRASSY), eq = t_earthquake();
    assert(battle_move_effects_valid_for(&tm, BATTLE_REQUEST_VERSION_TERRAIN) &&
           !battle_move_effects_valid_for(&tm, BATTLE_REQUEST_VERSION_WEATHER));
    assert(battle_move_effects_valid_for(&eq, BATTLE_REQUEST_VERSION_TERRAIN) &&
           !battle_move_effects_valid_for(&eq, BATTLE_REQUEST_VERSION_WEATHER));
    const BattleAbility v16_abilities[] = {T_ELECTRIC_SURGE, T_GRASSY_SURGE, T_MISTY_SURGE, T_PSYCHIC_SURGE,
                                           T_LEVITATE, T_SURGE_SURFER, T_GRASS_PELT};
    for (unsigned i = 0; i < sizeof v16_abilities / sizeof *v16_abilities; ++i) {
        assert(battle_ability_valid(&v16_abilities[i], R_COUNT, BATTLE_REQUEST_VERSION_TERRAIN));
        assert(!battle_ability_valid(&v16_abilities[i], R_COUNT, BATTLE_REQUEST_VERSION_WEATHER));
    }
    const BattleModifier field_power = modr(BMOD_POWER, 130, BPRED_ON_TERRAIN, T_GRASS, BPRED_MOVE_TYPE, R_GRASS);
    assert(battle_modifier_valid(&field_power, R_COUNT, BMSRC_FIELD, BATTLE_REQUEST_VERSION_TERRAIN));
    assert(!battle_modifier_valid(&field_power, R_COUNT, BMSRC_FIELD, BATTLE_REQUEST_VERSION_WEATHER));
    const BattleModifier bad[] = {
        modr(BMOD_POWER, 130, BPRED_TERRAIN | BPRED_DEFENDER, T_GRASS, 0, 0),  // the terrain has no subject
        modr(BMOD_POWER, 130, BPRED_TERRAIN, 0, 0, 0),                          // empty mask
        modr(BMOD_POWER, 130, BPRED_TERRAIN, (uint8_t)(BTERRAIN_ALL + 1), 0, 0),
        modr(BMOD_POWER, 130, BPRED_ON_TERRAIN, 0, 0, 0),
        modr(BMOD_POWER, 130, BPRED_EXPOSED, 1, 0, 0),                          // EXPOSED has no argument
        modr(BMOD_SPEED, 200, BPRED_ON_TERRAIN | BPRED_DEFENDER, T_ELEC, 0, 0), // speed: the holder only
        modr(BMOD_POWER, 130, BPRED_COUNT, 0, 0, 0),
    };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; ++i)
        assert(!battle_modifier_valid(&bad[i], R_COUNT, BMSRC_ABILITY, BATTLE_REQUEST_VERSION_TERRAIN));
    const BattleModifier v16_pred = modr(BMOD_POWER, 130, BPRED_TERRAIN, T_GRASS, 0, 0);
    assert(battle_modifier_valid(&v16_pred, R_COUNT, BMSRC_ABILITY, BATTLE_REQUEST_VERSION_TERRAIN) &&
           !battle_modifier_valid(&v16_pred, R_COUNT, BMSRC_ABILITY, BATTLE_REQUEST_VERSION_WEATHER));
    // Effect and hook fields.
    BattleMove m = terrain_move(BTERRAIN_COUNT);
    assert(!battle_move_effects_valid(&m));
    m = terrain_move(BTERRAIN_MISTY);
    m.effects[0].arg2 = BWEATHER_TURNS_KEEP;  // no KEEP for terrain
    assert(!battle_move_effects_valid(&m));
    m = terrain_move(BTERRAIN_MISTY);
    push(&m, fx(BEFF_STAT, BWHO_USER, 0, BSTAT_DEF, 1, 0));
    assert(!battle_move_effects_valid(&m));
    BattleAbilityHook h = W_HOOK(BABIL_ENTRY_TERRAIN, BTERRAIN_PSYCHIC, 0, 16, 0);
    assert(!battle_ability_hook_valid(&h, R_COUNT, BATTLE_REQUEST_VERSION_TERRAIN));
    h = W_HOOK(BABIL_UNGROUNDED, 1, 0, 0, 0);
    assert(!battle_ability_hook_valid(&h, R_COUNT, BATTLE_REQUEST_VERSION_TERRAIN));
    h = W_HOOK(BABIL_ENTRY_TERRAIN, BTERRAIN_NONE, 0, 5, 0);  // a surge names a terrain
    assert(!battle_ability_hook_valid(&h, R_COUNT, BATTLE_REQUEST_VERSION_TERRAIN));
    // Malformed terrain rules: each byte that has no meaning, and a record past the count.
    n = battle_request_encode(&r, bytes, sizeof bytes);
    const uint32_t base = n - BATTLE_TERRAIN_RULE_BYTES;
    const struct { unsigned at; uint8_t value; } bad_bytes[] = {
        {0, 1}, {1, BTERRAIN_COUNT}, {2, BTERRAIN_MAX_TURNS + 1}, {3, R_COUNT}, {4, BATTLE_MAX_TERRAIN_MODIFIERS + 1},
        {4, 5}, {5, 1}, {7, 1}, {8, 1}, {9, 0x10}, {16 + 2, 1}, {16 + 4 * BATTLE_MODIFIER_BODY_BYTES, BMOD_POWER},
        {16, BMOD_SPEED}, {16 + 3, BPRED_COUNT},
    };
    assert(battle_request_decode(bytes, n, &d));
    for (unsigned i = 0; i < sizeof bad_bytes / sizeof *bad_bytes; ++i) {
        const uint8_t keep = bytes[base + bad_bytes[i].at];
        bytes[base + bad_bytes[i].at] = bad_bytes[i].value;
        assert(!battle_request_decode(bytes, n, &d));
        bytes[base + bad_bytes[i].at] = keep;
    }
    assert(battle_request_decode(bytes, n, &d));
    BattleTerrainRules t = r.terrain;
    t.start_terrain = BTERRAIN_NONE;
    t.start_turns = 3;
    assert(!battle_terrain_rules_valid(&t, R_COUNT));
    t.airborne_type = BABIL_NO_TYPE;
    t.start_turns = 0;
    assert(battle_terrain_rules_valid(&t, R_COUNT));
    assert(!strcmp(battle_effect_kind_name(BEFF_TERRAIN), "terrain") &&
           !strcmp(battle_ability_kind_name(BABIL_UNGROUNDED), "ungrounded"));
}

// Start, same terrain again, replacement, independent of the weather, the late countdown.
static void test_v16_set_and_end(void) {
    static BattleRequest r;
    static Battle b;
    static EventLog log;
    make_terrain(&r, BATTLE_FORMAT_SINGLE);
    r.player[0].record.speed = 50;
    r.opponent[0].record.speed = 90;
    r.player[0].mon.moves[0].power = r.opponent[0].mon.moves[0].power = 1;
    set_move(&r.player[0], 1, 580, terrain_move(BTERRAIN_GRASSY), 10);
    set_move(&r.player[0], 2, 240, weather_move(BWEATHER_RAIN, 5), 10);
    set_move(&r.opponent[0], 1, 604, terrain_move(BTERRAIN_ELECTRIC), 10);
    r.player[0].record.max_hp = 160;
    r.player[0].record.hp = 100;
    r.player[0].record.status = BST_POISON;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    Action grass = move(0, 0, 0), rain = move(0, 0, 0), elec = move(1, 0, 0);
    grass.move_slot = 1;
    rain.move_slot = 2;
    elec.move_slot = 1;
    run1(&b, grass, move(1, 0, 0), &log);
    const BattleEvent *e = find_event(&log, EV_TERRAIN_START, 0);
    assert(e && e->condition == BTERRAIN_GRASSY && e->amount == 5 && battle_terrain(&b) == BTERRAIN_GRASSY);
    assert(battle_field_terrain(&b)->turns == 4 && battle_field_terrain(&b)->source_kind == FSRC_MOVE);
    // The heal (1/16 = 10) comes before the poison damage at the end of the same turn.
    const int heal = event_index(&log, EV_TERRAIN_HEAL, 0), poison = event_index(&log, EV_RESIDUAL, 0);
    assert(heal >= 0 && poison > heal && log.events[heal].amount == 10);
    run1(&b, grass, move(1, 0, 0), &log);  // the same terrain fails, the count goes on
    assert(event_count(&log, EV_FAIL) == 1 && battle_field_terrain(&b)->turns == 3);
    // Rain and grassy terrain together; each keeps its own count.
    run1(&b, rain, move(1, 0, 0), &log);
    assert(battle_weather(&b) == BWEATHER_RAIN && battle_terrain(&b) == BTERRAIN_GRASSY);
    assert(battle_field_weather(&b)->turns == 4 && battle_field_terrain(&b)->turns == 2);
    // Electric Terrain replaces it; the weather stays.
    run1(&b, move(0, 0, 0), elec, &log);
    assert(battle_terrain(&b) == BTERRAIN_ELECTRIC && battle_field_terrain(&b)->turns == 4 &&
           battle_weather(&b) == BWEATHER_RAIN && battle_field_weather(&b)->turns == 3);
    // Same turn ends: the weather ends first (start of the end of turn), the terrain late.
    b.field.weather.turns = 1;
    b.field.terrain.turns = 1;
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    const int w_end = event_index(&log, EV_WEATHER_END, 0), t_end = event_index(&log, EV_TERRAIN_END, 0);
    assert(w_end >= 0 && t_end > w_end && t_end > event_index(&log, EV_RESIDUAL, 0));
    assert(battle_weather(&b) == BWEATHER_NONE && battle_terrain(&b) == BTERRAIN_NONE);
    // Its last turn still heals: grassy with one turn left heals, then ends.
    b.field.terrain = (FieldEffect){.kind = BTERRAIN_GRASSY, .turns = 1};
    battle_active(&b, 0)->hp = 100;
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    assert(event_index(&log, EV_TERRAIN_HEAL, 0) >= 0 &&
           event_index(&log, EV_TERRAIN_END, 0) > event_index(&log, EV_TERRAIN_HEAL, 0));
    // A permanent battle-start terrain from the rules (after the start weather).
    r.terrain.start_terrain = BTERRAIN_PSYCHIC;
    r.terrain.start_turns = 0;
    r.player[0].record.status = 0;
    r.field.start_weather = BWEATHER_SUN;
    r.field.start_turns = 5;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(event_index(&log, EV_WEATHER_START, 0) < event_index(&log, EV_TERRAIN_START, 0));
    for (unsigned i = 0; i < 6; ++i) run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    assert(battle_terrain(&b) == BTERRAIN_PSYCHIC && battle_weather(&b) == BWEATHER_NONE);
}

// Surge abilities: speed order (the slower one's terrain stays), the same
// terrain, replacements entering together, and switching back in.
static void test_v16_entry(void) {
    static BattleRequest r;
    static Battle b;
    static EventLog log;
    make_terrain(&r, BATTLE_FORMAT_DOUBLE);
    give(&r.player[0], T_ELECTRIC_SURGE);
    give(&r.opponent[0], T_PSYCHIC_SURGE);
    give(&r.opponent[1], W_DROUGHT);
    r.player[0].record.speed = 100;
    r.opponent[0].record.speed = 50;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(event_count(&log, EV_TERRAIN_START) == 2 && find_event(&log, EV_TERRAIN_START, 0)->condition == BTERRAIN_ELECTRIC);
    assert(battle_terrain(&b) == BTERRAIN_PSYCHIC && battle_weather(&b) == BWEATHER_SUN);
    give(&r.opponent[0], T_ELECTRIC_SURGE);  // the same terrain twice: one start, no second notice
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(event_count(&log, EV_TERRAIN_START) == 1 && battle_field_terrain(&b)->turns == 5);
    for (unsigned i = 0; i < log.count; ++i)
        assert(!(log.events[i].kind == EV_ABILITY && log.events[i].user.side == 1 && log.events[i].user.slot == 0));
    // Replacements after a turn: entry abilities after the last one, in speed order.
    give(&r.opponent[0], T_PSYCHIC_SURGE);
    add_reserve(&r, 0);
    add_reserve(&r, 1);
    give(&r.player[2], T_MISTY_SURGE);
    give(&r.opponent[2], T_GRASSY_SURGE);
    r.player[2].record.speed = 30;
    r.opponent[2].record.speed = 120;
    assert(battle_init(&b, &r, 3) == BI_OK);
    battle_begin(&b, &log);
    for (uint8_t side = 0; side < 2; ++side) {
        Battler *x = at_pos(&b, side, 0);
        x->hp = 0;
        x->fainted = true;
        b.sides[side].active[0] = BATTLE_SLOT_NONE;
    }
    Position p;
    assert(battle_pending_replacement(&b, &p) && battle_replace(&b, p, 2, &log) && log.count == 1);
    assert(battle_pending_replacement(&b, &p) && battle_replace(&b, p, 2, &log));
    assert(find_event(&log, EV_TERRAIN_START, 0)->condition == BTERRAIN_GRASSY &&
           find_event(&log, EV_TERRAIN_START, 1)->condition == BTERRAIN_MISTY && battle_terrain(&b) == BTERRAIN_MISTY);
    // Out and in again: the surge again.
    make_terrain(&r, BATTLE_FORMAT_SINGLE);
    add_reserve(&r, 0);
    give(&r.player[0], T_GRASSY_SURGE);
    r.player[0].record.speed = 10;
    set_move(&r.opponent[0], 1, 604, terrain_move(BTERRAIN_ELECTRIC), 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    Action elec = move(1, 0, 0), out = {.kind = ACT_SWITCH, .user = {0, 0}, .reserve = 1};
    elec.move_slot = 1;
    run1(&b, out, elec, &log);
    assert(battle_terrain(&b) == BTERRAIN_ELECTRIC);
    Action back = {.kind = ACT_SWITCH, .user = {0, 0}, .reserve = 0};
    run1(&b, back, move(1, 0, 0), &log);
    assert(battle_terrain(&b) == BTERRAIN_GRASSY);
}

// The shared grounded rule and terrain-affected (grounded, not semi-invulnerable).
static void test_v16_grounded(void) {
    static BattleRequest r;
    static Battle b;
    make_terrain(&r, BATTLE_FORMAT_TRIPLE);
    r.player[1].mon.types[0] = R_FLYING;
    give(&r.player[2], T_LEVITATE);
    r.opponent[0].mon.type_count = 2;
    r.opponent[0].mon.types[1] = R_FLYING;  // a second type counts too
    assert(battle_init(&b, &r, 1) == BI_OK);
    assert(battle_grounded(&b, at_pos(&b, 0, 0)) && !battle_grounded(&b, at_pos(&b, 0, 1)) &&
           !battle_grounded(&b, at_pos(&b, 0, 2)) && !battle_grounded(&b, at_pos(&b, 1, 0)) &&
           battle_grounded(&b, at_pos(&b, 1, 1)));
    b.field.terrain = (FieldEffect){.kind = BTERRAIN_GRASSY, .turns = 5};
    assert(bc_on_terrain(&b, at_pos(&b, 0, 0), T_GRASS) && !bc_on_terrain(&b, at_pos(&b, 0, 0), T_ELEC) &&
           !bc_on_terrain(&b, at_pos(&b, 0, 1), T_GRASS));
    for (uint8_t semi = BSEMI_AIR; semi <= BSEMI_UNDERGROUND; ++semi) {
        at_pos(&b, 0, 0)->vol.semi_invulnerable = semi;
        assert(!bc_on_terrain(&b, at_pos(&b, 0, 0), T_GRASS) && battle_grounded(&b, at_pos(&b, 0, 0)));
    }
    at_pos(&b, 0, 0)->vol.semi_invulnerable = BSEMI_NONE;
    // Abilities normalised away: Levitate does nothing (the request's own choice).
    r.rules |= RULE_IGNORE_ABILITIES;
    assert(battle_init(&b, &r, 1) == BI_OK && battle_grounded(&b, at_pos(&b, 0, 2)));
    // No airborne type in the rules: a Flying type is grounded (the GBA decides).
    r.rules &= (uint16_t)~RULE_IGNORE_ABILITIES;
    r.terrain.airborne_type = BABIL_NO_TYPE;
    assert(battle_init(&b, &r, 1) == BI_OK && battle_grounded(&b, at_pos(&b, 0, 1)));
    // Items can ground or lift a Pokemon (Air Balloon, Iron Ball) and the core
    // runs none: a V16 request must normalise them away, or it is refused
    // (never a silent "grounded"). Earlier versions keep their meaning.
    r.terrain.airborne_type = R_FLYING;
    r.rules &= (uint16_t)~RULE_IGNORE_ITEMS;
    assert(battle_init(&b, &r, 1) == BI_ITEMS);
    assert(!strcmp(battle_init_error_name(BI_ITEMS), "V16: held items are not simulated (grounding)"));
    r.version = BATTLE_REQUEST_VERSION_WEATHER;
    assert(battle_init(&b, &r, 1) == BI_OK);
    // Before V16 there is no terrain at all.
    make_weather(&r, BATTLE_FORMAT_SINGLE);
    assert(battle_init(&b, &r, 1) == BI_OK);
    b.field.terrain = (FieldEffect){.kind = BTERRAIN_GRASSY, .turns = 5};
    assert(battle_terrain(&b) == BTERRAIN_NONE && !bc_on_terrain(&b, battle_active(&b, 0), T_GRASS));
}

// Electric: no new sleep; Misty: no major status, no confusion; the order
// against abilities; only affected (grounded) Pokemon; contact abilities too.
static void test_v16_status(void) {
    static BattleRequest r;
    static Battle b;
    static EventLog log;
    make_terrain(&r, BATTLE_FORMAT_DOUBLE);
    BattleMove sleep = status_move(BTARGET_SINGLE, 0), para = status_move(BTARGET_SINGLE, 0);
    BattleMove confuse = status_move(BTARGET_SINGLE, 0);
    push(&sleep, fx(BEFF_STATUS, BWHO_TARGET, 0, BST_SLEEP, 0, 0));
    push(&para, fx(BEFF_STATUS, BWHO_TARGET, 0, BST_PARALYSIS, 0, 0));
    push(&confuse, fx(BEFF_CONFUSE, BWHO_TARGET, 0, 0, 0, 0));
    set_move(&r.player[0], 0, 79, sleep, 10);
    set_move(&r.player[0], 1, 86, para, 10);
    set_move(&r.player[0], 2, 109, confuse, 10);
    r.player[0].record.speed = 200;
    r.opponent[1].mon.types[0] = R_FLYING;
    for (unsigned i = 0; i < 2; ++i) r.opponent[i].mon.moves[0].power = 1, r.player[1].mon.moves[0].power = 1;
    assert(battle_init(&b, &r, 1) == BI_OK);
    Action use = move(0, 0, 0);
    const Action idle[] = {move(0, 1, 0), move(1, 0, 0), move(1, 1, 0)};
    // Electric: the grounded foe stays awake (shown), the Flying one falls asleep.
    b.field.terrain = (FieldEffect){.kind = BTERRAIN_ELECTRIC, .turns = 5};
    Action turn[4] = {use, idle[0], idle[1], idle[2]};
    battle_run_turn(&b, turn, 4, &log);
    const BattleEvent *blk = find_event(&log, EV_TERRAIN_BLOCK, 0);
    assert(blk && blk->target.side == 1 && blk->target.slot == 0 && blk->condition == BTERRAIN_ELECTRIC &&
           !at_pos(&b, 1, 0)->status);
    turn[0].target.pos.slot = 1;
    battle_run_turn(&b, turn, 4, &log);
    assert(BATTLE_CONDITION_KIND(at_pos(&b, 1, 1)->status) == BST_SLEEP);
    // Electric does not stop paralysis.
    turn[0] = use;
    turn[0].move_slot = 1;
    battle_run_turn(&b, turn, 4, &log);
    assert(BATTLE_CONDITION_KIND(at_pos(&b, 1, 0)->status) == BST_PARALYSIS);
    // Misty: no status and no confusion on the grounded foe.
    assert(battle_init(&b, &r, 1) == BI_OK);
    b.field.terrain = (FieldEffect){.kind = BTERRAIN_MISTY, .turns = 5};
    for (unsigned s = 0; s < 3; ++s) {
        turn[0] = use;
        turn[0].move_slot = (uint8_t)s;
        battle_run_turn(&b, turn, 4, &log);
        assert(event_count(&log, EV_TERRAIN_BLOCK) == 1 && !at_pos(&b, 1, 0)->status &&
               !at_pos(&b, 1, 0)->vol.confusion);
    }
    // The order against abilities: paralysis checks Limber first (its block is shown),
    // sleep checks Electric Terrain before Insomnia.
    give(&r.opponent[0], HOOKS(7, hk(BABIL_STATUS_IMMUNE, (1u << BST_PARALYSIS) | (1u << BST_SLEEP), 0, 0, 0, 0, 0, 0)));
    assert(battle_init(&b, &r, 1) == BI_OK);
    b.field.terrain = (FieldEffect){.kind = BTERRAIN_MISTY, .turns = 5};
    turn[0] = use;
    turn[0].move_slot = 1;
    battle_run_turn(&b, turn, 4, &log);
    assert(event_count(&log, EV_ABILITY_BLOCK) == 1 && !event_count(&log, EV_TERRAIN_BLOCK));
    b.field.terrain.kind = BTERRAIN_ELECTRIC;
    turn[0].move_slot = 0;
    battle_run_turn(&b, turn, 4, &log);
    assert(event_count(&log, EV_TERRAIN_BLOCK) == 1 && !event_count(&log, EV_ABILITY_BLOCK));
    // An existing status stays; a semi-invulnerable Pokemon is not affected.
    assert(battle_init(&b, &r, 1) == BI_OK);
    b.field.terrain = (FieldEffect){.kind = BTERRAIN_MISTY, .turns = 5};
    at_pos(&b, 1, 0)->status = BST_BURN;
    battle_run_turn(&b, turn, 4, &log);
    assert(at_pos(&b, 1, 0)->status == BST_BURN);
    // Contact abilities go through the same rule: Static cannot paralyse a grounded attacker in Misty Terrain.
    make_terrain(&r, BATTLE_FORMAT_SINGLE);
    give(&r.opponent[0], HOOKS(9, hk(BABIL_CONTACT_STATUS, BST_PARALYSIS, 0, 0, 1, 1, 0, 0)));
    set_move(&r.player[0], 0, 33, contact(10, 1), 30);
    assert(battle_init(&b, &r, 1) == BI_OK);
    b.field.terrain = (FieldEffect){.kind = BTERRAIN_MISTY, .turns = 5};
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    assert(!battle_active(&b, 0)->status && !event_count(&log, EV_STATUS));
    b.field.terrain.kind = BTERRAIN_NONE;
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    assert(BATTLE_CONDITION_KIND(battle_active(&b, 0)->status) == BST_PARALYSIS);
}

// Psychic Terrain: a foe's move with priority > 0 fails against an affected
// target, per target; not against an airborne one, an ally, or with priority 0.
static void test_v16_priority(void) {
    static BattleRequest r;
    static Battle b;
    static EventLog log;
    make_terrain(&r, BATTLE_FORMAT_DOUBLE);
    r.opponent[1].mon.types[0] = R_FLYING;
    BattleMove quick = t_priority(40, 1), spread = t_priority(30, 1), slow = t_priority(40, 0);
    spread.target = BTARGET_ALL_ADJACENT;
    set_move(&r.player[0], 0, 98, quick, 30);
    set_move(&r.player[0], 1, 500, spread, 30);
    set_move(&r.player[0], 2, 33, slow, 30);
    set_move(&r.player[0], 3, 1, terrain_move(BTERRAIN_PSYCHIC), 10);
    for (unsigned i = 0; i < 2; ++i) r.opponent[i].mon.moves[0].power = 1, r.player[1].mon.moves[0].power = 1;
    assert(battle_init(&b, &r, 1) == BI_OK);
    b.field.terrain = (FieldEffect){.kind = BTERRAIN_PSYCHIC, .turns = 5};
    Action turn[4] = {move(0, 0, 0), move(0, 1, 0), move(1, 0, 0), move(1, 1, 0)};
    battle_run_turn(&b, turn, 4, &log);  // Quick Attack on the grounded foe: stopped, PP spent
    const BattleEvent *blk = find_event(&log, EV_TERRAIN_BLOCK, 0);
    assert(blk && blk->user.side == 0 && blk->target.side == 1 && blk->target.slot == 0 && blk->move_id == 98);
    assert(battle_active(&b, 0)->pp[0] == 29);
    for (unsigned i = 0; i < log.count; ++i)
        assert(!(log.events[i].kind == EV_DAMAGE && log.events[i].user.side == 0 && log.events[i].user.slot == 0));
    turn[0].target.pos.slot = 1;  // the airborne foe is hit
    battle_run_turn(&b, turn, 4, &log);
    assert(!event_count(&log, EV_TERRAIN_BLOCK));
    // A spread priority move: the grounded foe is protected, the airborne foe and the ally are hit.
    turn[0] = move(0, 0, 0);
    turn[0].move_slot = 1;
    battle_run_turn(&b, turn, 4, &log);
    assert(event_count(&log, EV_TERRAIN_BLOCK) == 1);
    unsigned hit_ally = 0, hit_air = 0;
    for (unsigned i = 0; i < log.count; ++i) {
        const BattleEvent *e = &log.events[i];
        if (e->kind != EV_DAMAGE || e->move_id != 500) continue;
        hit_ally += e->target.side == 0;
        hit_air += e->target.side == 1 && e->target.slot == 1;
        assert(!(e->target.side == 1 && e->target.slot == 0));
    }
    assert(hit_ally == 1 && hit_air == 1);
    // Priority 0 and the user's own field move are never stopped.
    turn[0] = move(0, 0, 0);
    turn[0].move_slot = 2;
    battle_run_turn(&b, turn, 4, &log);
    assert(!event_count(&log, EV_TERRAIN_BLOCK));
    // The foe's priority move against the player's grounded Pokemon: stopped too (either side).
    make_terrain(&r, BATTLE_FORMAT_SINGLE);
    set_move(&r.opponent[0], 0, 98, quick, 30);
    assert(battle_init(&b, &r, 1) == BI_OK);
    b.field.terrain = (FieldEffect){.kind = BTERRAIN_PSYCHIC, .turns = 5};
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    blk = find_event(&log, EV_TERRAIN_BLOCK, 0);
    assert(blk && blk->target.side == 0);
    // Another terrain does not stop priority moves.
    b.field.terrain.kind = BTERRAIN_MISTY;
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    assert(!event_count(&log, EV_TERRAIN_BLOCK));
}

// The terrain power records, the Earthquake record, Grass Pelt, Surge Surfer, with the weather.
static uint16_t t_damage(const RefMon *atk, const RefMon *def, BattleMove move, unsigned flags, uint8_t weather,
                         uint8_t terrain, unsigned roll) {
    static BattleRequest r;
    static Battle b;
    make_terrain(&r, BATTLE_FORMAT_SINGLE);
    ref_mon(&r.player[0], 0, atk);
    set_move(&r.player[0], 0, 100, move, 10);
    ref_mon(&r.opponent[0], 0x80, def);
    set_move(&r.opponent[0], 0, 101, damaging(10, R_NORMAL, 100), 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    b.field.weather = (FieldEffect){.kind = weather, .turns = 5};
    b.field.terrain = (FieldEffect){.kind = terrain, .turns = 5};
    const Battler *a = battle_active(&b, 0);
    BattleMove m = a->moves[0];
    m.type = bc_weather_move_type(&b, &m);
    uint8_t eff;
    return bc_calculate_damage(&b, a, &m, battle_active(&b, 1), roll, false, flags, &eff);
}

static void test_v16_damage(void) {
    const RefMon atk = RMON(50, R_NORMAL, R_NORMAL, 100, 999, 999, 60, 0, R_NO_ABILITY);
    const RefMon flyer = RMON(50, R_FLYING, R_FLYING, 100, 999, 999, 60, 0, R_NO_ABILITY);
    const RefMon def = RMON(50, R_NORMAL, R_NORMAL, 100, 999, 999, 30, 0, R_NO_ABILITY);
    const RefMon def_fly = RMON(50, R_FLYING, R_FLYING, 100, 999, 999, 30, 0, R_NO_ABILITY);
    const BattleMove thunderbolt = ref_move(90, R_ELECTRIC, BCAT_SPECIAL), energy = ref_move(90, R_GRASS, BCAT_SPECIAL);
    const BattleMove psychic = ref_move(90, R_PSYCHIC, BCAT_SPECIAL), dragon = ref_move(80, R_DRAGON, BCAT_SPECIAL);
    // x1.3 power for a grounded attacker: 90 x 5325 / 4096 = 117.0 -> as a 117-power move.
    const uint16_t p117 = t_damage(&atk, &def, ref_move(117, R_ELECTRIC, BCAT_SPECIAL), 0, 0, 0, 100);
    assert(t_damage(&atk, &def, thunderbolt, 0, 0, BTERRAIN_ELECTRIC, 100) == p117);
    assert(t_damage(&atk, &def, energy, 0, 0, BTERRAIN_GRASSY, 100) ==
           t_damage(&atk, &def, ref_move(117, R_GRASS, BCAT_SPECIAL), 0, 0, 0, 100));
    assert(t_damage(&atk, &def, psychic, 0, 0, BTERRAIN_PSYCHIC, 100) ==
           t_damage(&atk, &def, ref_move(117, R_PSYCHIC, BCAT_SPECIAL), 0, 0, 0, 100));
    // Only its own terrain, only a grounded attacker; the defender does not matter.
    assert(t_damage(&atk, &def, thunderbolt, 0, 0, BTERRAIN_GRASSY, 100) == t_damage(&atk, &def, thunderbolt, 0, 0, 0, 100));
    assert(t_damage(&flyer, &def, thunderbolt, 0, 0, BTERRAIN_ELECTRIC, 100) ==
           t_damage(&flyer, &def, thunderbolt, 0, 0, 0, 100));
    assert(t_damage(&atk, &def_fly, thunderbolt, 0, 0, BTERRAIN_ELECTRIC, 100) ==
           t_damage(&atk, &def_fly, ref_move(117, R_ELECTRIC, BCAT_SPECIAL), 0, 0, 0, 100));
    // Misty: x0.5 Dragon against a grounded target, from any attacker.
    assert(t_damage(&flyer, &def, dragon, 0, 0, BTERRAIN_MISTY, 100) ==
           t_damage(&flyer, &def, ref_move(40, R_DRAGON, BCAT_SPECIAL), 0, 0, 0, 100));
    assert(t_damage(&atk, &def_fly, dragon, 0, 0, BTERRAIN_MISTY, 100) == t_damage(&atk, &def_fly, dragon, 0, 0, 0, 100));
    // Earthquake on Grassy Terrain: x0.5 (50); doubled, not halved, against an underground target.
    assert(t_damage(&atk, &def, t_earthquake(), 0, 0, BTERRAIN_GRASSY, 100) ==
           t_damage(&atk, &def, ref_move(50, R_GROUND, BCAT_PHYSICAL), 0, 0, 0, 100));
    {
        static BattleRequest r;
        static Battle b;
        make_terrain(&r, BATTLE_FORMAT_SINGLE);
        set_move(&r.player[0], 0, 89, t_earthquake(), 10);
        assert(battle_init(&b, &r, 1) == BI_OK);
        b.field.terrain = (FieldEffect){.kind = BTERRAIN_GRASSY, .turns = 5};
        uint8_t eff;
        Battler *d = battle_active(&b, 1);
        const BattleMove *m = &battle_active(&b, 0)->moves[0];
        const uint16_t open = bc_calculate_damage(&b, battle_active(&b, 0), m, d, 100, false, 0, &eff);
        d->vol.semi_invulnerable = BSEMI_UNDERGROUND;
        const uint16_t dug = bc_calculate_damage(&b, battle_active(&b, 0), m, d, 100, false, BCD_DOUBLED, &eff);
        b.field.terrain.kind = BTERRAIN_NONE;
        assert(dug == bc_calculate_damage(&b, battle_active(&b, 0), m, d, 100, false, BCD_DOUBLED, &eff) && dug > 2 * open);
    }
    // With the weather: Solar Beam in rain on Grassy Terrain: the power chain 120 x0.5 x1.3 = 78.
    BattleMove beam = w_solar_beam();
    beam.type = R_GRASS;
    assert(t_damage(&atk, &def, beam, 0, BWEATHER_RAIN, BTERRAIN_GRASSY, 100) ==
           t_damage(&atk, &def, ref_move(78, R_GRASS, BCAT_SPECIAL), 0, 0, 0, 100));
    // Rain x1.5 (weather step) and nothing from Electric Terrain on a Water move.
    const BattleMove surf = ref_move(90, R_WATER, BCAT_SPECIAL);
    assert(t_damage(&atk, &def, surf, 0, BWEATHER_RAIN, BTERRAIN_ELECTRIC, 100) ==
           t_damage(&atk, &def, surf, 0, BWEATHER_RAIN, 0, 100));
    // Grass Pelt: Defense x1.5 on Grassy Terrain (any defender, physical only).
    RefMon pelt = def;
    pelt.ability = T_GRASS_PELT;
    const BattleMove tackle = ref_move(80, R_NORMAL, BCAT_PHYSICAL);
    assert(t_damage(&atk, &pelt, tackle, 0, 0, BTERRAIN_GRASSY, 100) < t_damage(&atk, &pelt, tackle, 0, 0, 0, 100));
    assert(t_damage(&atk, &pelt, ref_move(80, R_NORMAL, BCAT_SPECIAL), 0, 0, BTERRAIN_GRASSY, 100) ==
           t_damage(&atk, &pelt, ref_move(80, R_NORMAL, BCAT_SPECIAL), 0, 0, 0, 100));
    // The Gen 7 boost (x1.5) from the same GC: only the GBA's record changes.
    {
        static BattleRequest r;
        static Battle b;
        make_terrain(&r, BATTLE_FORMAT_SINGLE);
        frontier_terrain(&r.terrain, 150);
        ref_mon(&r.player[0], 0, &atk);
        set_move(&r.player[0], 0, 100, thunderbolt, 10);
        ref_mon(&r.opponent[0], 0x80, &def);
        assert(battle_init(&b, &r, 1) == BI_OK);
        b.field.terrain = (FieldEffect){.kind = BTERRAIN_ELECTRIC, .turns = 5};
        uint8_t eff;
        assert(bc_calculate_damage(&b, battle_active(&b, 0), &battle_active(&b, 0)->moves[0], battle_active(&b, 1),
                                   100, false, 0, &eff) ==
               t_damage(&atk, &def, ref_move(135, R_ELECTRIC, BCAT_SPECIAL), 0, 0, 0, 100));
    }
}

// Surge Surfer doubles speed on Electric Terrain (dynamic turn order).
static void test_v16_speed(void) {
    static BattleRequest r;
    static Battle b;
    static EventLog log;
    make_terrain(&r, BATTLE_FORMAT_DOUBLE);
    give(&r.player[0], T_SURGE_SURFER);
    r.player[0].record.speed = 50;
    r.player[1].record.speed = 200;
    r.opponent[0].record.speed = 90;
    r.opponent[1].record.speed = 80;
    set_move(&r.player[1], 1, 604, terrain_move(BTERRAIN_ELECTRIC), 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    Action elec = move(0, 1, 0);
    elec.move_slot = 1;
    const Action turn[] = {move(0, 0, 0), elec, move(1, 0, 0), move(1, 1, 0)};
    battle_run_turn(&b, turn, 4, &log);
    assert(find_event(&log, EV_MOVE_USED, 1)->user.side == 0 && find_event(&log, EV_MOVE_USED, 1)->user.slot == 0);
}

// The orders the reference fixes and the other tests do not reach: a priority
// status move on Psychic Terrain, Misty before Own Tempo, a chance effect kept
// off silently, the Grassy heal before Shed Skin (FIRST_EVENT_BLOCK_GRASSY_
// TERRAIN_HEAL, then FIRST_EVENT_BLOCK_ABILITIES), the start terrain without
// abilities, and the terrain records between the move's own and the
// attacker's power modifiers (CalcMoveBasePowerAfterModifiers).
static void test_v16_order(void) {
    static BattleRequest r;
    static Battle b;
    static EventLog log;
    // A status move with priority 1 is stopped like a damaging one.
    make_terrain(&r, BATTLE_FORMAT_SINGLE);
    BattleMove spore = status_move(BTARGET_SINGLE, 0);
    spore.priority = 1;
    push(&spore, fx(BEFF_STATUS, BWHO_TARGET, 0, BST_SLEEP, 0, 0));
    set_move(&r.player[0], 0, 147, spore, 10);
    r.opponent[0].mon.moves[0].power = 1;
    assert(battle_init(&b, &r, 1) == BI_OK);
    b.field.terrain = (FieldEffect){.kind = BTERRAIN_PSYCHIC, .turns = 5};
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    assert(event_count(&log, EV_TERRAIN_BLOCK) == 1 && !battle_active(&b, 1)->status);
    // Misty Terrain before Own Tempo: the terrain's block is the one shown.
    make_terrain(&r, BATTLE_FORMAT_SINGLE);
    BattleMove confuse = status_move(BTARGET_SINGLE, 0);
    push(&confuse, fx(BEFF_CONFUSE, BWHO_TARGET, 0, 0, 0, 0));
    set_move(&r.player[0], 0, 109, confuse, 10);
    r.opponent[0].mon.moves[0].power = 1;
    give(&r.opponent[0], HOOKS(20, hk(BABIL_VOLATILE_IMMUNE, BABIL_VOL_CONFUSION, 0, 0, 0, 0, 0, 0)));
    assert(battle_init(&b, &r, 1) == BI_OK);
    b.field.terrain = (FieldEffect){.kind = BTERRAIN_MISTY, .turns = 5};
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    assert(event_count(&log, EV_TERRAIN_BLOCK) == 1 && !event_count(&log, EV_ABILITY_BLOCK) &&
           !battle_active(&b, 1)->vol.confusion);
    // A chance effect (100%) is kept off without a notice; without the terrain it lands.
    make_terrain(&r, BATTLE_FORMAT_SINGLE);
    BattleMove spark = damaging(20, R_NORMAL, 100);
    push(&spark, fx(BEFF_STATUS, BWHO_TARGET, 100, BST_PARALYSIS, 0, 0));
    set_move(&r.player[0], 0, 209, spark, 10);
    r.opponent[0].mon.moves[0].power = 1;
    assert(battle_init(&b, &r, 1) == BI_OK);
    b.field.terrain = (FieldEffect){.kind = BTERRAIN_MISTY, .turns = 5};
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    assert(!event_count(&log, EV_TERRAIN_BLOCK) && !battle_active(&b, 1)->status);
    b.field.terrain.kind = BTERRAIN_NONE;
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    assert(BATTLE_CONDITION_KIND(battle_active(&b, 1)->status) == BST_PARALYSIS);
    // The Grassy heal before Shed Skin (made certain here).
    make_terrain(&r, BATTLE_FORMAT_SINGLE);
    give(&r.player[0], HOOKS(61, hk(BABIL_END_TURN_CURE, 0xfe, 0, 0, 1, 1, 0, 0)));
    r.player[0].record.max_hp = 160;
    r.player[0].record.hp = 100;
    r.player[0].record.status = BST_POISON;
    r.player[0].mon.moves[0].power = r.opponent[0].mon.moves[0].power = 1;
    assert(battle_init(&b, &r, 1) == BI_OK);
    b.field.terrain = (FieldEffect){.kind = BTERRAIN_GRASSY, .turns = 5};
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    int heal = -1;
    for (unsigned i = 0; i < log.count && heal < 0; ++i)
        if (log.events[i].kind == EV_TERRAIN_HEAL && log.events[i].target.side == 0) heal = (int)i;
    const int cure = event_index(&log, EV_CURE, 0);
    assert(heal >= 0 && cure > heal && !event_count(&log, EV_RESIDUAL) && !battle_active(&b, 0)->status);
    // Only a grounded Pokemon heals, and never past its max HP (155/160 heals 5, not 10).
    make_terrain(&r, BATTLE_FORMAT_SINGLE);
    set_move(&r.player[0], 0, 580, terrain_move(BTERRAIN_GRASSY), 10);
    set_move(&r.opponent[0], 0, 580, terrain_move(BTERRAIN_GRASSY), 10);
    r.player[0].mon.types[0] = R_FLYING;
    r.player[0].record.max_hp = r.opponent[0].record.max_hp = 160;
    r.player[0].record.hp = 100;
    r.opponent[0].record.hp = 155;
    assert(battle_init(&b, &r, 1) == BI_OK);
    b.field.terrain = (FieldEffect){.kind = BTERRAIN_GRASSY, .turns = 5};
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    const BattleEvent *healed = find_event(&log, EV_TERRAIN_HEAL, 0);
    assert(event_count(&log, EV_TERRAIN_HEAL) == 1 && healed->target.side == 1 && healed->amount == 5 &&
           battle_active(&b, 1)->hp == 160 && battle_active(&b, 0)->hp == 100);
    // Psychic Terrain before Protect (the target-failure order): the terrain's block is shown.
    make_terrain(&r, BATTLE_FORMAT_SINGLE);
    BattleMove guard = status_move(BTARGET_SELF, 0);
    guard.priority = 4;
    push(&guard, fx(BEFF_PROTECT, BWHO_USER, 0, 3, 0, 0));
    set_move(&r.player[0], 0, 98, t_priority(40, 1), 30);
    set_move(&r.opponent[0], 0, 182, guard, 10);
    assert(battle_init(&b, &r, 1) == BI_OK);
    b.field.terrain = (FieldEffect){.kind = BTERRAIN_PSYCHIC, .turns = 5};
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    assert(event_count(&log, EV_PROTECT) == 1 && event_count(&log, EV_TERRAIN_BLOCK) == 1 &&
           !event_count(&log, EV_PROTECTED) && !event_count(&log, EV_DAMAGE));
    // Abilities normalised away: the rules' start terrain still comes (after the start weather).
    make_terrain(&r, BATTLE_FORMAT_SINGLE);
    r.rules |= RULE_IGNORE_ABILITIES;
    r.terrain.start_terrain = BTERRAIN_GRASSY;
    r.terrain.start_turns = 5;
    r.field.start_weather = BWEATHER_RAIN;
    r.field.start_turns = 5;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(battle_terrain(&b) == BTERRAIN_GRASSY && battle_field_terrain(&b)->source_kind == FSRC_RULE &&
           event_index(&log, EV_WEATHER_START, 0) >= 0 &&
           event_index(&log, EV_TERRAIN_START, 0) > event_index(&log, EV_WEATHER_START, 0));
    // The power chain: the move's own x0.5 (rain), the terrain x1.3, then the attacker's x1.2:
    // 4096 -> 2048 -> 2663 -> 3195, so 116 power becomes 90. The terrain after the ability
    // would give 2048 -> 2458 -> 3196 and 91.
    const RefMon atk = RMON(50, R_NORMAL, R_NORMAL, 400, 999, 999, 60, 0,
                            MODS(4, modr(BMOD_POWER, 120, BPRED_MOVE_TYPE, R_GRASS, 0, 0)));
    const RefMon plain = RMON(50, R_NORMAL, R_NORMAL, 400, 999, 999, 60, 0, R_NO_ABILITY);
    const RefMon def = RMON(50, R_NORMAL, R_NORMAL, 50, 999, 999, 30, 0, R_NO_ABILITY);
    BattleMove beam = w_solar_beam();
    beam.type = R_GRASS;
    beam.power = 116;
    const uint16_t got = t_damage(&atk, &def, beam, 0, BWEATHER_RAIN, BTERRAIN_GRASSY, 100);
    const uint16_t p90 = t_damage(&plain, &def, ref_move(90, R_GRASS, BCAT_SPECIAL), 0, 0, 0, 100);
    const uint16_t p91 = t_damage(&plain, &def, ref_move(91, R_GRASS, BCAT_SPECIAL), 0, 0, 0, 100);
    assert(p90 != p91 && got == p90);
}

// Random terrain battles in every format: the same request and seed give the
// same events, state and RNG; HP in range; every terrain event happens.
static void test_v16_formats(void) {
    static BattleRequest r;
    static Battle a, b;
    static EventLog la, lb;
    const BattleAbility pool[] = {T_ELECTRIC_SURGE, T_GRASSY_SURGE, T_MISTY_SURGE, T_PSYCHIC_SURGE, T_LEVITATE,
                                  T_SURGE_SURFER, T_GRASS_PELT, W_DRIZZLE, W_SAND_STREAM, W_CLOUD_NINE, R_NO_ABILITY,
                                  STATIC};
    const unsigned n = sizeof pool / sizeof *pool;
    const uint8_t formats[] = {BATTLE_FORMAT_SINGLE, BATTLE_FORMAT_DOUBLE, BATTLE_FORMAT_TRIPLE, BATTLE_FORMAT_ROTATION};
    unsigned counts[4] = {0}, battles = 0;
    for (unsigned f = 0; f < 4; ++f)
        for (uint32_t seed = 1; seed <= 60; ++seed) {
            make_terrain(&r, formats[f]);
            for (unsigned side = 0; side < 2; ++side) add_reserve(&r, side);
            for (unsigned side = 0; side < 2; ++side) {
                BattleEntry *e = side ? r.opponent : r.player;
                const unsigned count = side ? r.opponent_count : r.player_count;
                for (unsigned i = 0; i < count; ++i) {
                    give(&e[i], pool[(seed * 7 + side * 5 + i + f) % n]);
                    e[i].record.hp = e[i].record.max_hp = 90;
                    e[i].record.hp = (uint16_t)(90 - (seed + i) % 30);
                    e[i].mon.types[0] = (uint8_t)((seed + 3 * i + side) % R_COUNT);
                    e[i].record.speed = (uint16_t)(40 + (seed * 13 + i * 7 + side * 3) % 60);
                    const uint8_t kind = (uint8_t)(1 + (seed + i + side) % BTERRAIN_KINDS);
                    set_move(&e[i], 1, 600 + kind, terrain_move(kind), 8);
                    BattleMove extra = (seed + i) % 3 == 0 ? t_earthquake() : (seed + i) % 3 == 1 ?
                                       t_priority(40, 1) : ref_move(80, (uint8_t)(R_GRASS + (seed + i) % 3), BCAT_SPECIAL);
                    set_move(&e[i], 2, 300 + i, extra, 10);
                    BattleMove status = status_move(BTARGET_SINGLE, 0);
                    push(&status, fx((seed + i) % 2 ? BEFF_STATUS : BEFF_CONFUSE, BWHO_TARGET, 0,
                                     (seed + i) % 2 ? (uint8_t)(BST_POISON + (seed + i) % 5) : 0, 0, 0));
                    if (status.effects[0].kind == BEFF_STATUS && status.effects[0].arg0 == BST_FREEZE)
                        status.effects[0].arg0 = BST_BURN;
                    set_move(&e[i], 3, 400 + i, status, 10);
                }
            }
            r.terrain.start_terrain = (uint8_t)(seed % 5 == 0 ? 1 + seed % BTERRAIN_KINDS : 0);
            r.terrain.start_turns = r.terrain.start_terrain ? (uint8_t)(seed % 4) : 0;
            assert(battle_init(&a, &r, seed) == BI_OK && battle_init(&b, &r, seed) == BI_OK);
            battle_begin(&a, &la);
            battle_begin(&b, &lb);
            assert(la.count == lb.count && memcmp(la.events, lb.events, la.count * sizeof(BattleEvent)) == 0);
            for (unsigned t = 0; t < 60 && a.outcome == OUTCOME_NONE; ++t) {
                Position pending;
                while (battle_pending_replacement(&a, &pending)) {
                    const uint8_t mask = battle_replacement_mask(&a, pending);
                    uint8_t pick = 0;
                    while (!(mask & (1u << pick))) ++pick;
                    assert(battle_replace(&a, pending, pick, &la) && battle_replace(&b, pending, pick, &lb));
                }
                if (a.outcome != OUTCOME_NONE) break;
                Action actions[BC_MAX_ACTIONS];
                unsigned count = 0;
                for (uint8_t side = 0; side < 2; ++side) {
                    Position actors[BC_MAX_ACTIVE];
                    const unsigned k = battle_collect_actors(&a, side, actors);
                    for (unsigned i = 0; i < k; ++i) battle_choose_at(&a, actors[i], &actions[count++]);
                }
                for (unsigned i = 0; i < count; ++i) {
                    Action dummy;
                    battle_choose_at(&b, actions[i].user, &dummy);
                }
                battle_run_turn(&a, actions, count, &la);
                battle_run_turn(&b, actions, count, &lb);
                assert(la.count == lb.count && la.count < BC_MAX_EVENTS &&
                       memcmp(la.events, lb.events, la.count * sizeof(BattleEvent)) == 0);
                for (unsigned i = 0; i < la.count; ++i)
                    if (la.events[i].kind >= EV_TERRAIN_START) ++counts[la.events[i].kind - EV_TERRAIN_START];
                for (unsigned side = 0; side < 2; ++side)
                    for (unsigned i = 0; i < a.sides[side].count; ++i) {
                        const Battler *x = &a.sides[side].battlers[i];
                        assert(x->hp <= x->max_hp && (x->hp == 0) == x->fainted);
                    }
                assert(a.field.terrain.kind < BTERRAIN_COUNT && a.field.terrain.turns <= BTERRAIN_MAX_TURNS);
            }
            assert(memcmp(&a.sides, &b.sides, sizeof a.sides) == 0 && a.rng == b.rng &&
                   memcmp(&a.field, &b.field, sizeof a.field) == 0);
            ++battles;
        }
    for (unsigned i = 0; i < 4; ++i) assert(counts[i] > 10);
    printf("v16 formats: %u battles, terrain start %u end %u heal %u block %u\n", battles, counts[0], counts[1],
           counts[2], counts[3]);
}


// ------------------------------------------------------------- V16 against the Expansion engine
//
// Hits the Frontier build's test runner computes with the real Expansion
// code on a terrain set by a surge ability (test/battle/gc_terrain_reference.c
// in frontier-gc-m4t, same stats, rolls 100/93/85, no crit). The build keeps
// the Gen 3 power of Thunderbolt (95) and Gen 4 Dragon Pulse (90).
typedef struct {
    const char *name;
    RefMon atk, def;
    BattleMove move;
    uint8_t weather, terrain;
    uint16_t expansion[3];
} TerrainRefCase;

static void test_v16_expansion_reference(void) {
    BattleMove beam = w_solar_beam();
    beam.type = R_GRASS;
    const TerrainRefCase cases[] = {
        {"Electric Terrain x1.3: Thunderbolt, STAB (Tapu Koko vs Snorlax)",
         RMON(50, R_ELECTRIC, R_ELECTRIC, 100, 999, 999, 130, 0, T_ELECTRIC_SURGE),
         RMON(50, R_NORMAL, R_NORMAL, 100, 999, 999, 30, 0, R_NO_ABILITY),
         ref_move(95, R_ELECTRIC, BCAT_SPECIAL), 0, BTERRAIN_ELECTRIC, {84, 78, 70}},
        {"Electric Terrain: no boost for a Flying attacker (Zapdos vs Pincurchin)",
         RMON(50, R_ELECTRIC, R_FLYING, 100, 999, 999, 100, 0, R_NO_ABILITY),
         RMON(50, R_ELECTRIC, R_ELECTRIC, 100, 999, 999, 15, 0, T_ELECTRIC_SURGE),
         ref_move(95, R_ELECTRIC, BCAT_SPECIAL), 0, BTERRAIN_ELECTRIC, {32, 29, 27}},
        {"Grassy Terrain x1.3: Energy Ball, STAB (Rillaboom vs Snorlax)",
         RMON(50, R_GRASS, R_GRASS, 100, 999, 999, 85, 0, T_GRASSY_SURGE),
         RMON(50, R_NORMAL, R_NORMAL, 100, 999, 999, 30, 0, R_NO_ABILITY),
         ref_move(90, R_GRASS, BCAT_SPECIAL), 0, BTERRAIN_GRASSY, {79, 73, 67}},
        {"Grassy Terrain: Earthquake x0.5 (Rillaboom vs Snorlax)",
         RMON(50, R_GRASS, R_GRASS, 100, 999, 999, 85, 0, T_GRASSY_SURGE),
         RMON(50, R_NORMAL, R_NORMAL, 100, 999, 999, 30, 0, R_NO_ABILITY),
         t_earthquake(), 0, BTERRAIN_GRASSY, {24, 22, 20}},
        {"Misty Terrain: Dragon Pulse x0.5 on a grounded target (Tapu Fini vs Snorlax)",
         RMON(50, R_WATER, R_WATER, 100, 999, 999, 85, 0, T_MISTY_SURGE),
         RMON(50, R_NORMAL, R_NORMAL, 100, 999, 999, 30, 0, R_NO_ABILITY),
         ref_move(90, R_DRAGON, BCAT_SPECIAL), 0, BTERRAIN_MISTY, {21, 19, 17}},
        {"Misty Terrain: no halving on a Flying target (Tapu Fini vs Pidgeot)",
         RMON(50, R_WATER, R_WATER, 100, 999, 999, 85, 0, T_MISTY_SURGE),
         RMON(50, R_NORMAL, R_FLYING, 100, 999, 999, 30, 0, R_NO_ABILITY),
         ref_move(90, R_DRAGON, BCAT_SPECIAL), 0, BTERRAIN_MISTY, {41, 38, 34}},
        {"Psychic Terrain x1.3: Psychic, STAB (Tapu Lele vs Snorlax)",
         RMON(50, R_PSYCHIC, R_PSYCHIC, 100, 999, 999, 95, 0, T_PSYCHIC_SURGE),
         RMON(50, R_NORMAL, R_NORMAL, 100, 999, 999, 30, 0, R_NO_ABILITY),
         ref_move(90, R_PSYCHIC, BCAT_SPECIAL), 0, BTERRAIN_PSYCHIC, {79, 73, 67}},
        {"Rain and Grassy Terrain: Solar Beam 120 x0.5 x1.3, STAB (Rillaboom vs Pelipper)",
         RMON(50, R_GRASS, R_GRASS, 100, 999, 999, 85, 0, T_GRASSY_SURGE),
         RMON(50, R_WATER, R_FLYING, 100, 999, 999, 65, 0, W_DRIZZLE),
         beam, BWEATHER_RAIN, BTERRAIN_GRASSY, {54, 49, 45}},
        {"Grass Pelt: Defense x1.5 on Grassy Terrain (Rillaboom Body Slam vs Gogoat)",
         RMON(50, R_GRASS, R_GRASS, 100, 999, 999, 85, 0, T_GRASSY_SURGE),
         RMON(50, R_GRASS, R_GRASS, 100, 999, 999, 68, 0, T_GRASS_PELT),
         ref_move(85, R_NORMAL, BCAT_PHYSICAL), 0, BTERRAIN_GRASSY, {26, 24, 22}},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; ++i) {
        const TerrainRefCase *k = &cases[i];
        printf("  tref %2u %-80s", i + 1, k->name);
        for (unsigned r = 0; r < 3; ++r) {
            const uint16_t v16 = t_damage(&k->atk, &k->def, k->move, 0, k->weather, k->terrain, ref_rolls[r]);
            const uint16_t none = t_damage(&k->atk, &k->def, k->move, 0, k->weather, 0, ref_rolls[r]);
            printf(" %u%%: v16=%u none=%u exp=%u", ref_rolls[r], v16, none, k->expansion[r]);
            assert(v16 == k->expansion[r]);
        }
        printf("\n");
    }
}

// ------------------------------------------------------------- V17 held items (docs/items.md)
//
// The items as the Frontier exporter resolves them (ids are diagnostic only):
// a power record of the holder's moves, or the duration of a weather or
// terrain the holder sets.

static BattleItem item_mods(uint16_t id, unsigned n, const BattleModifier *list) {
    BattleItem it = {.cls = BICLASS_RECORDS, .count = (uint8_t)n, .source = id};
    for (unsigned i = 0; i < n; ++i) it.records[i] = (BattleItemRecord){.kind = BITEM_MODIFIER, .mod = list[i]};
    return it;
}
#define IMODS(id, ...) item_mods(id, sizeof((BattleModifier[]){__VA_ARGS__}) / sizeof(BattleModifier), \
                                 (BattleModifier[]){__VA_ARGS__})
static BattleItem item_turns(uint16_t id, uint8_t kind, uint8_t mask, uint8_t turns) {
    BattleItem it = {.cls = BICLASS_RECORDS, .count = 1, .source = id};
    it.records[0] = (BattleItemRecord){.kind = kind, .arg0 = mask, .arg2 = turns};
    return it;
}
#define I_TYPE(id, type, percent) IMODS(id, modr(BMOD_POWER, percent, BPRED_MOVE_TYPE, type, 0, 0))
#define I_CHARCOAL I_TYPE(215, R_FIRE, 120)
#define I_MYSTIC_WATER I_TYPE(209, R_WATER, 120)
#define I_MIRACLE_SEED I_TYPE(205, R_GRASS, 120)
#define I_SEA_INCENSE I_TYPE(220, R_WATER, 120)
#define I_MUSCLE_BAND IMODS(266, modr(ST(BMOD_POWER, BCAT_PHYSICAL) | BMOD_FLOORED, 110, 0, 0, 0, 0))
#define I_WISE_GLASSES IMODS(267, modr(ST(BMOD_POWER, BCAT_SPECIAL) | BMOD_FLOORED, 110, 0, 0, 0, 0))
#define I_DAMP_ROCK item_turns(285, BITEM_WEATHER_TURNS, W_RAIN, 8)
#define I_HEAT_ROCK item_turns(284, BITEM_WEATHER_TURNS, W_SUN, 8)
#define I_SMOOTH_ROCK item_turns(283, BITEM_WEATHER_TURNS, W_SAND, 8)
#define I_ICY_ROCK item_turns(282, BITEM_WEATHER_TURNS, W_ICY, 8)
#define I_TERRAIN_EXTENDER item_turns(896, BITEM_TERRAIN_TURNS, BTERRAIN_ALL, 8)
#define I_UNSUPPORTED(id) ((BattleItem){.cls = BICLASS_UNSUPPORTED, .source = (id)})
#define I_INERT(id) ((BattleItem){.cls = BICLASS_NONE, .source = (id)})
#define I_LEFTOVERS I_UNSUPPORTED(234)
#define I_AMULET_COIN I_INERT(223)

static void hold(BattleEntry *e, BattleItem it) {
    e->record.held_item = it.source;
    e->mon.item = it;
}

static void make_items(BattleRequest *r, uint8_t format) {
    make_terrain(r, format);
    r->version = BATTLE_REQUEST_VERSION_ITEMS;
    r->rules &= (uint16_t)~RULE_IGNORE_ITEMS;  // V17: the item blocks say what each item does
}

static void test_v17_wire(void) {
    static BattleRequest r, d, old, plain;
    static uint8_t bytes[4096], v16[4096], v16_plain[4096];
    make_items(&r, BATTLE_FORMAT_DOUBLE);
    hold(&r.player[0], I_CHARCOAL);
    hold(&r.player[1], I_DAMP_ROCK);
    hold(&r.opponent[0], I_MUSCLE_BAND);
    hold(&r.opponent[1], I_AMULET_COIN);
    uint32_t n = battle_request_encode(&r, bytes, sizeof bytes);
    assert(n && n == battle_request_size(&r));
    assert(battle_request_decode(bytes, n, &d));
    for (unsigned i = 0; i < 2; ++i) {
        assert(memcmp(&d.player[i].mon.item, &r.player[i].mon.item, sizeof(BattleItem)) == 0);
        assert(memcmp(&d.opponent[i].mon.item, &r.opponent[i].mon.item, sizeof(BattleItem)) == 0);
    }
    // The item blocks follow the ability blocks: V16 size + 4 per Pokemon + 8 per record.
    old = r;
    old.version = BATTLE_REQUEST_VERSION_TERRAIN;
    assert(battle_request_size(&r) == battle_request_size(&old) + 4 * BATTLE_ITEM_HEADER_BYTES + 3 * BATTLE_ITEM_RECORD_BYTES);
    // V16 is unchanged: the same request written as V16 is byte for byte the one without items.
    const uint32_t n16 = battle_request_encode(&old, v16, sizeof v16);
    plain = old;
    for (unsigned i = 0; i < BATTLE_MAX_PER_SIDE; ++i) {
        plain.player[i].mon.item = plain.opponent[i].mon.item = (BattleItem){0};
    }
    assert(n16 && battle_request_encode(&plain, v16_plain, sizeof v16_plain) == n16 && !memcmp(v16, v16_plain, n16));
    assert(battle_request_decode(v16, n16, &d) && d.player[0].mon.item.cls == BICLASS_NONE && !d.player[0].mon.item.count);
    bytes[0] = BATTLE_REQUEST_VERSION_TERRAIN;  // V17 bytes are not a V16 request
    assert(!battle_request_decode(bytes, n, &d));
    bytes[0] = BATTLE_REQUEST_VERSION_ITEMS;
    // RULE_STRICT_ITEMS is a V17 rule.
    old.rules |= RULE_STRICT_ITEMS;
    assert(battle_request_encode(&old, v16, sizeof v16) == 0);
    r.rules |= RULE_STRICT_ITEMS;
    assert(battle_request_encode(&r, bytes, sizeof bytes) == n && battle_request_decode(bytes, n, &d) &&
           (d.rules & RULE_STRICT_ITEMS));
    r.rules &= (uint16_t)~RULE_STRICT_ITEMS;
    n = battle_request_encode(&r, bytes, sizeof bytes);
    // Block checks: the record's item, a class that fits, valid records.
    const BattleItem good[] = {I_CHARCOAL, I_MUSCLE_BAND, I_WISE_GLASSES, I_DAMP_ROCK, I_HEAT_ROCK, I_SMOOTH_ROCK,
                               I_ICY_ROCK, I_TERRAIN_EXTENDER, I_LEFTOVERS, I_AMULET_COIN};
    for (unsigned i = 0; i < sizeof good / sizeof *good; ++i) {
        assert(battle_item_valid(&good[i], good[i].source, R_COUNT, BATTLE_REQUEST_VERSION_ITEMS));
        assert(!battle_item_valid(&good[i], good[i].source, R_COUNT, BATTLE_REQUEST_VERSION_TERRAIN));
        assert(!battle_item_valid(&good[i], (uint16_t)(good[i].source + 1), R_COUNT, BATTLE_REQUEST_VERSION_ITEMS));
    }
    const BattleItem none = {0};
    assert(battle_item_valid(&none, 0, R_COUNT, BATTLE_REQUEST_VERSION_ITEMS));
    BattleItem bad[] = {
        I_UNSUPPORTED(0),                                                       // UNSUPPORTED names an item
        {.cls = BICLASS_RECORDS, .count = 0, .source = 5},                      // RECORDS has records
        {.cls = BICLASS_NONE, .count = 1, .source = 5},                         // NONE has none
        {.cls = 3, .source = 5},                                                // unknown class
        I_TYPE(5, R_COUNT, 120),                                                // type out of range
        IMODS(5, modr(BMOD_ATTACK, 150, 0, 0, 0, 0)),                           // V17 items: the power only
        IMODS(5, modr(ST(BMOD_POWER, 0) | BMODF_DEFENDER, 150, 0, 0, 0, 0)),    // the attacker's item
        IMODS(5, modr(BMOD_POWER, 0, 0, 0, 0, 0)),                              // no percent
        item_turns(5, BITEM_WEATHER_TURNS, 0, 8),                               // empty mask
        item_turns(5, BITEM_WEATHER_TURNS, (uint8_t)(BWEATHER_ALL + 1), 8),
        item_turns(5, BITEM_WEATHER_TURNS, W_RAIN, 0),                          // a limited duration
        item_turns(5, BITEM_WEATHER_TURNS, W_RAIN, BWEATHER_MAX_TURNS + 1),
        item_turns(5, BITEM_TERRAIN_TURNS, (uint8_t)(BTERRAIN_ALL + 1), 8),
        item_turns(5, BITEM_TERRAIN_TURNS, T_GRASS, BTERRAIN_MAX_TURNS + 1),
        item_turns(5, BITEM_COUNT, W_RAIN, 8),                                  // unknown kind
        item_turns(5, BITEM_NONE, 0, 0),
    };
    for (unsigned i = 0; i < sizeof bad / sizeof *bad; ++i)
        assert(!battle_item_valid(&bad[i], bad[i].source, R_COUNT, BATTLE_REQUEST_VERSION_ITEMS));
    BattleItem two = I_DAMP_ROCK;  // two durations for the same weather
    two.records[two.count++] = (BattleItemRecord){.kind = BITEM_WEATHER_TURNS, .arg0 = W_RAIN | W_SUN, .arg2 = 6};
    assert(!battle_item_valid(&two, two.source, R_COUNT, BATTLE_REQUEST_VERSION_ITEMS));
    two.records[1].arg0 = W_SUN;
    assert(battle_item_valid(&two, two.source, R_COUNT, BATTLE_REQUEST_VERSION_ITEMS));
    two.records[1] = (BattleItemRecord){.kind = BITEM_WEATHER_TURNS, .arg0 = W_SUN, .arg1 = 1, .arg2 = 6};
    assert(!battle_item_valid(&two, two.source, R_COUNT, BATTLE_REQUEST_VERSION_ITEMS));
    two.records[1] = (BattleItemRecord){.kind = BITEM_WEATHER_TURNS, .arg0 = W_SUN, .arg2 = 6, .flags = 1};
    assert(!battle_item_valid(&two, two.source, R_COUNT, BATTLE_REQUEST_VERSION_ITEMS));
    BattleItem many = I_CHARCOAL;
    many.count = BATTLE_MAX_ITEM_RECORDS + 1;
    assert(!battle_item_valid(&many, many.source, R_COUNT, BATTLE_REQUEST_VERSION_ITEMS));
    // The encoder never writes a block that does not describe its record's item.
    old = r;
    old.player[0].record.held_item = 1;
    assert(battle_request_encode(&old, v16, sizeof v16) == 0);
    // Bit 6 of the stage (BMOD_FLOORED): V17, fixed-point stages, any record source.
    const BattleModifier floored = modr(ST(BMOD_POWER, BCAT_PHYSICAL) | BMOD_FLOORED, 110, 0, 0, 0, 0);
    for (uint8_t src = BMSRC_MOVE; src <= BMSRC_ITEM; ++src) {
        if (src == BMSRC_FIELD) continue;
        const BattleModifier x = src == BMSRC_MOVE ? modr(BMOD_POWER | BMOD_FLOORED, 110, 0, 0, 0, 0) : floored;
        assert(battle_modifier_valid(&x, R_COUNT, src, BATTLE_REQUEST_VERSION_ITEMS));
        assert(!battle_modifier_valid(&x, R_COUNT, src, BATTLE_REQUEST_VERSION_TERRAIN));
    }
    const BattleModifier int_floored[] = {modr(BMOD_BASE_POWER | BMOD_FLOORED, 200, 0, 0, 0, 0),
                                          modr(ST(BMOD_SPEED, 0) | BMOD_FLOORED, 150, 0, 0, 0, 0),
                                          modr(ST(BMOD_ACCURACY, 0) | BMOD_FLOORED, 110, 0, 0, 0, 0)};
    for (unsigned i = 0; i < 3; ++i)
        assert(!battle_modifier_valid(&int_floored[i], R_COUNT, BMSRC_ABILITY, BATTLE_REQUEST_VERSION_ITEMS));
    const BattleModifier low = modr(BMOD_POWER, 150, BPRED_POWER_AT_MOST, 60, 0, 0);
    assert(battle_modifier_valid(&low, R_COUNT, BMSRC_ITEM, BATTLE_REQUEST_VERSION_ITEMS));
    assert(!battle_modifier_valid(&low, R_COUNT, BMSRC_ITEM, BATTLE_REQUEST_VERSION_TERRAIN));
    // A V16 move record with bit 6 is malformed; the same record is a V17 request.
    old = r;
    set_move(&old.player[0], 2, 33, with_mod(ref_move(40, R_NORMAL, BCAT_PHYSICAL), modr(BMOD_POWER | BMOD_FLOORED, 110, 0, 0, 0, 0)), 10);
    const uint32_t n17 = battle_request_encode(&old, bytes, sizeof bytes);
    assert(n17 && battle_request_decode(bytes, n17, &d));
    old.version = BATTLE_REQUEST_VERSION_TERRAIN;
    old.rules |= RULE_IGNORE_ITEMS;
    const uint32_t n16b = battle_request_encode(&old, v16, sizeof v16);
    assert(n16b && !battle_request_decode(v16, n16b, &d));
    // Malformed item bytes: the first block (player 0, Charcoal) starts where the
    // item section begins, just before the fixed rules that close the request.
    n = battle_request_encode(&r, bytes, sizeof bytes);
    const uint32_t items = 4 * BATTLE_ITEM_HEADER_BYTES + 3 * BATTLE_ITEM_RECORD_BYTES;
    const uint32_t base = n - BATTLE_VOLATILE_RULE_BYTES - BATTLE_FIELD_RULE_BYTES - BATTLE_TERRAIN_RULE_BYTES - items;
    assert(bytes[base] == BICLASS_RECORDS && bytes[base + 1] == 1 && bytes[base + 2] == (215 & 0xff));
    const struct { unsigned at; uint8_t value; } bad_bytes[] = {
        {0, 3}, {0, BICLASS_NONE}, {1, 0}, {1, BATTLE_MAX_ITEM_RECORDS + 1}, {2, 0}, {4, BITEM_COUNT},
        {4, BITEM_NONE}, {5, BMOD_SPEED}, {5, BMOD_POWER | BMODF_DEFENDER}, {6, 0}, {7, 1}, {8, BPRED_COUNT},
        {9, R_COUNT},
        // The Damp Rock block (player 1): turns record bytes 4..7 are zero.
        {12 + 5, 0}, {12 + 6, 1}, {12 + 7, 0}, {12 + 7, 16}, {12 + 8, 1}, {12 + 9, 1}, {12 + 10, 1}, {12 + 11, 1},
    };
    assert(bytes[base + 12] == BICLASS_RECORDS && bytes[base + 16] == BITEM_WEATHER_TURNS && bytes[base + 19] == 8);
    for (unsigned i = 0; i < sizeof bad_bytes / sizeof *bad_bytes; ++i) {
        const uint8_t keep = bytes[base + bad_bytes[i].at];
        bytes[base + bad_bytes[i].at] = bad_bytes[i].value;
        assert(!battle_request_decode(bytes, n, &d));
        bytes[base + bad_bytes[i].at] = keep;
    }
    assert(battle_request_decode(bytes, n, &d));
    assert(!battle_request_decode(bytes, n - 1, &d) && !battle_request_decode(bytes, base + 3, &d));
    // Records for no item (source and held item both 0).
    BattleItem orphan = I_CHARCOAL;
    orphan.source = 0;
    assert(!battle_item_valid(&orphan, 0, R_COUNT, BATTLE_REQUEST_VERSION_ITEMS));
    assert(!strcmp(battle_item_kind_name(BITEM_WEATHER_TURNS), "weather turns") &&
           !strcmp(battle_item_kind_name(BITEM_COUNT), "unknown"));
}

// battle_init: what the rules allow, and the battle-start notice of normalised items.
static void test_v17_init(void) {
    static BattleRequest r;
    static Battle b;
    static EventLog log;
    make_items(&r, BATTLE_FORMAT_DOUBLE);
    r.version = BATTLE_REQUEST_VERSION_TERRAIN;  // V16 is unchanged
    assert(battle_init(&b, &r, 1) == BI_ITEMS);
    r.version = BATTLE_REQUEST_VERSION_ITEMS;
    assert(battle_init(&b, &r, 1) == BI_OK && battle_items_simulated(&b) && !battle_items_normalized(&b));
    battle_begin(&b, &log);
    assert(!event_count(&log, EV_ITEMS_NORMALIZED));
    hold(&r.player[0], I_CHARCOAL);
    hold(&r.player[1], I_AMULET_COIN);
    assert(battle_init(&b, &r, 1) == BI_OK);
    assert(battle_held_item(battle_active(&b, 0)) == 215 && bc_item(&b, battle_active(&b, 0)));
    assert(battle_held_item(at_pos(&b, 0, 1)) == 223 && !bc_item(&b, at_pos(&b, 0, 1)));  // no battle effect
    assert(!battle_held_item(battle_active(&b, 1)) && !battle_active(&b, 1)->item_from);
    assert(battle_active(&b, 0)->item_from == &r.player[0]);
    // The state model: the item held now is `item_from`'s, with that item's definition, whoever
    // holds it (what a later item move changes); the item it started with stays its own entry's.
    {
        Battler *x = at_pos(&b, 0, 1), *y = battle_active(&b, 1);
        x->item_from = &r.player[0];
        y->item_from = &r.player[0];
        assert(battle_held_item(x) == 215 && bc_item(&b, x) == &r.player[0].mon.item);
        assert(battle_held_item(y) == 215 && bc_item(&b, y) && x->entry->record.held_item == 223);
        x->item_from = NULL;
        assert(!battle_held_item(x) && !bc_item(&b, x));
    }
    // An item the core cannot run, items not normalised: no battle.
    hold(&r.opponent[1], I_LEFTOVERS);
    assert(battle_init(&b, &r, 1) == BI_UNSUPPORTED_ITEM);
    assert(!strcmp(battle_init_error_name(BI_UNSUPPORTED_ITEM),
                   "V17: a held item is not simulated and items are not normalised"));
    // Normalised: every item off, announced first.
    r.rules |= RULE_IGNORE_ITEMS;
    assert(battle_init(&b, &r, 1) == BI_OK && battle_items_normalized(&b) && !battle_items_simulated(&b));
    assert(!bc_item(&b, battle_active(&b, 0)) && battle_held_item(battle_active(&b, 0)) == 215);
    battle_begin(&b, &log);
    assert(log.count && log.events[0].kind == EV_ITEMS_NORMALIZED && log.events[0].amount == 3 &&
           log.events[0].battler == 1 && event_count(&log, EV_ITEMS_NORMALIZED) == 1);
    // Strict: normalising an item that has an effect (or is not simulated) is refused.
    r.rules |= RULE_STRICT_ITEMS;
    assert(battle_init(&b, &r, 1) == BI_ITEMS_NORMALIZED);
    assert(!strcmp(battle_init_error_name(BI_ITEMS_NORMALIZED), "strict rules: held items would be normalised"));
    hold(&r.opponent[1], I_INERT(0));
    r.opponent[1].record.held_item = 0;
    assert(battle_init(&b, &r, 1) == BI_ITEMS_NORMALIZED);  // Charcoal would be dropped
    hold(&r.player[0], I_INERT(0));
    r.player[0].record.held_item = 0;
    assert(battle_init(&b, &r, 1) == BI_OK);  // only an item without a battle effect is left
    r.rules &= (uint16_t)~RULE_IGNORE_ITEMS;
    hold(&r.player[0], I_CHARCOAL);
    assert(battle_init(&b, &r, 1) == BI_OK);  // strict and simulated
    hold(&r.opponent[0], I_LEFTOVERS);
    assert(battle_init(&b, &r, 1) == BI_UNSUPPORTED_ITEM);
    // Reserves count too.
    make_items(&r, BATTLE_FORMAT_DOUBLE);
    add_reserve(&r, 1);
    hold(&r.opponent[r.opponent_count - 1], I_LEFTOVERS);
    assert(battle_init(&b, &r, 1) == BI_UNSUPPORTED_ITEM);
    r.rules |= RULE_IGNORE_ITEMS;
    assert(battle_init(&b, &r, 1) == BI_OK);
}

// Power of the attacker's moves: after both abilities, truncated or nearest
// factors as the record says, the defender's item playing no part.
static uint16_t i_damage(const RefMon *atk, const RefMon *def, BattleMove move, BattleItem atk_item,
                         BattleItem def_item, uint8_t terrain, unsigned roll) {
    static BattleRequest r;
    static Battle b;
    make_items(&r, BATTLE_FORMAT_SINGLE);
    ref_mon(&r.player[0], 0, atk);
    set_move(&r.player[0], 0, 100, move, 10);
    hold(&r.player[0], atk_item);
    ref_mon(&r.opponent[0], 0x80, def);
    set_move(&r.opponent[0], 0, 101, damaging(10, R_NORMAL, 100), 10);
    hold(&r.opponent[0], def_item);
    assert(battle_init(&b, &r, 1) == BI_OK);
    b.field.terrain = (FieldEffect){.kind = terrain, .turns = 5};
    const Battler *a = battle_active(&b, 0);
    uint8_t eff;
    return bc_calculate_damage(&b, a, &a->moves[0], battle_active(&b, 1), roll, false, 0, &eff);
}

static unsigned i_power(Battle *b, unsigned base_power) {
    const Battler *a = battle_active(b, 0);
    const ModContext c = {.b = b, .attacker = a, .defender = battle_active(b, 1), .move = &a->moves[0],
                          .base_power = base_power};
    return (unsigned)bc_modify(&c, BMOD_POWER, base_power);
}

static void test_v17_power(void) {
    const RefMon atk = RMON(50, R_NORMAL, R_NORMAL, 100, 999, 999, 60, 0, R_NO_ABILITY);
    const RefMon def = RMON(50, R_NORMAL, R_NORMAL, 100, 999, 999, 30, 0, R_NO_ABILITY);
    const BattleItem nothing = {0};
    const BattleMove flame = ref_move(90, R_FIRE, BCAT_SPECIAL), surf = ref_move(90, R_WATER, BCAT_SPECIAL);
    // Charcoal x1.2 (4915): 90 -> 108, Fire moves only; the defender's item does nothing to the power.
    assert(i_damage(&atk, &def, flame, I_CHARCOAL, nothing, 0, 100) ==
           i_damage(&atk, &def, ref_move(108, R_FIRE, BCAT_SPECIAL), nothing, nothing, 0, 100));
    assert(i_damage(&atk, &def, surf, I_CHARCOAL, nothing, 0, 100) == i_damage(&atk, &def, surf, nothing, nothing, 0, 100));
    assert(i_damage(&atk, &def, flame, nothing, I_CHARCOAL, 0, 100) == i_damage(&atk, &def, flame, nothing, nothing, 0, 100));
    assert(i_damage(&atk, &def, surf, I_MYSTIC_WATER, nothing, 0, 100) > i_damage(&atk, &def, surf, nothing, nothing, 0, 100));
    // Muscle Band, truncated x1.1 (4505): 45 -> 49 (the nearest factor 4506 would give 50); physical only.
    {
        static BattleRequest r;
        static Battle b;
        make_items(&r, BATTLE_FORMAT_SINGLE);
        set_move(&r.player[0], 0, 100, ref_move(45, R_NORMAL, BCAT_PHYSICAL), 10);
        hold(&r.player[0], I_MUSCLE_BAND);
        assert(battle_init(&b, &r, 1) == BI_OK);
        assert(i_power(&b, 45) == 49);
        r.player[0].mon.item.records[0].mod.stage &= (uint8_t)~BMOD_FLOORED;  // the same record, nearest factor
        assert(battle_init(&b, &r, 1) == BI_OK && i_power(&b, 45) == 50);
        r.player[0].mon.moves[0].category = BCAT_SPECIAL;
        assert(battle_init(&b, &r, 1) == BI_OK && i_power(&b, 45) == 45);
        hold(&r.player[0], I_WISE_GLASSES);
        assert(battle_init(&b, &r, 1) == BI_OK && i_power(&b, 45) == 49);
        r.rules |= RULE_IGNORE_ITEMS;  // normalised: no item takes part
        assert(battle_init(&b, &r, 1) == BI_OK && i_power(&b, 45) == 45);
        // A record acts only at its own stage (an attack record, which the decoder refuses
        // for V17 items, does nothing to the power).
        r.rules &= (uint16_t)~RULE_IGNORE_ITEMS;
        hold(&r.player[0], IMODS(9, modr(BMOD_ATTACK, 200, 0, 0, 0, 0)));
        assert(battle_init(&b, &r, 1) == BI_OK && i_power(&b, 45) == 45);
    }
    // Order: the move's record x0.5, Grassy Terrain x1.3, the attacker's ability x1.2, then the item
    // (CalcMoveBasePowerAfterModifiers): 74 -> 63; the item first would give 64, as would a nearest factor.
    {
        static BattleRequest r;
        static Battle b;
        make_items(&r, BATTLE_FORMAT_SINGLE);
        r.player[0].mon.types[0] = R_NORMAL;
        set_move(&r.player[0], 0, 100, with_mod(ref_move(74, R_GRASS, BCAT_PHYSICAL), modr(BMOD_POWER, 50, 0, 0, 0, 0)), 10);
        give(&r.player[0], MODS(250, modr(BMOD_POWER, 120, 0, 0, 0, 0)));
        hold(&r.player[0], I_MUSCLE_BAND);
        assert(battle_init(&b, &r, 1) == BI_OK);
        b.field.terrain = (FieldEffect){.kind = BTERRAIN_GRASSY, .turns = 5};
        assert(i_power(&b, 74) == 63);
        // The same three records with the band moved to the front of the chain (as a move record).
        BattleMove front = ref_move(74, R_GRASS, BCAT_PHYSICAL);
        push(&front, mod_effect(modr(ST(BMOD_POWER, 0) | BMOD_FLOORED, 110, 0, 0, 0, 0)));
        push(&front, mod_effect(modr(BMOD_POWER, 50, 0, 0, 0, 0)));
        set_move(&r.player[0], 0, 100, front, 10);
        hold(&r.player[0], nothing);
        r.player[0].record.held_item = 0;
        assert(battle_init(&b, &r, 1) == BI_OK);
        b.field.terrain = (FieldEffect){.kind = BTERRAIN_GRASSY, .turns = 5};
        assert(i_power(&b, 74) == 64);
    }
    // The same GC, another GBA definition: a type item at x1.5 instead of x1.2 (nothing else changes).
    assert(i_damage(&atk, &def, flame, I_TYPE(215, R_FIRE, 150), nothing, 0, 100) ==
           i_damage(&atk, &def, ref_move(135, R_FIRE, BCAT_SPECIAL), nothing, nothing, 0, 100));
    // With terrain: Miracle Seed after Grassy Terrain (90 x1.3 = 117, x1.2 = 140).
    const BattleMove energy = ref_move(90, R_GRASS, BCAT_SPECIAL);
    assert(i_damage(&atk, &def, energy, I_MIRACLE_SEED, nothing, BTERRAIN_GRASSY, 100) ==
           i_damage(&atk, &def, ref_move(140, R_GRASS, BCAT_SPECIAL), nothing, nothing, 0, 100));
    // Struggle is typeless: no type item applies.
    {
        static BattleRequest r;
        static Battle b;
        make_items(&r, BATTLE_FORMAT_SINGLE);
        hold(&r.player[0], I_TYPE(215, R_NORMAL, 120));
        assert(battle_init(&b, &r, 1) == BI_OK);
        const Battler *a = battle_active(&b, 0);
        const BattleMove m = ref_move(50, R_NORMAL, BCAT_PHYSICAL);
        const ModContext typed = {.b = &b, .attacker = a, .defender = battle_active(&b, 1), .move = &m, .base_power = 50};
        const ModContext typeless = {.b = &b, .attacker = a, .defender = battle_active(&b, 1), .move = &m,
                                     .typeless = true, .base_power = 50};
        assert(bc_modify(&typed, BMOD_POWER, 50) == 60 && bc_modify(&typeless, BMOD_POWER, 50) == 50);
    }
}

// The duration of a weather or terrain the holder sets (TryChangeWeather,
// TryChangeBattleTerrain): 8 instead of a limited count; nothing for a weather
// without an end, one that keeps the count, the battle's own start weather,
// the same weather again, or another holder.
static void test_v17_field_turns(void) {
    static BattleRequest r;
    static Battle b;
    static EventLog log;
    make_items(&r, BATTLE_FORMAT_SINGLE);
    r.player[0].record.speed = 90;
    r.opponent[0].record.speed = 50;
    r.player[0].mon.moves[0].power = r.opponent[0].mon.moves[0].power = 1;
    set_move(&r.player[0], 1, 240, weather_move(BWEATHER_RAIN, 5), 10);
    set_move(&r.player[0], 2, 241, weather_move(BWEATHER_SUN, 5), 10);
    set_move(&r.player[0], 3, 580, terrain_move(BTERRAIN_GRASSY), 10);
    set_move(&r.opponent[0], 1, 240, weather_move(BWEATHER_RAIN, 5), 10);
    set_move(&r.opponent[0], 2, 604, terrain_move(BTERRAIN_ELECTRIC), 10);
    hold(&r.player[0], I_DAMP_ROCK);
    Action rain = move(0, 0, 0), sun = move(0, 0, 0), grass = move(0, 0, 0), foe_rain = move(1, 0, 0),
           foe_elec = move(1, 0, 0);
    rain.move_slot = foe_rain.move_slot = 1;
    sun.move_slot = foe_elec.move_slot = 2;
    grass.move_slot = 3;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    run1(&b, rain, move(1, 0, 0), &log);
    const BattleEvent *e = find_event(&log, EV_WEATHER_START, 0);
    assert(e && e->amount == 8 && battle_field_weather(&b)->turns == 7);
    // It lasts eight turns in all: seven more ends.
    unsigned turns = 1;
    while (battle_weather(&b) == BWEATHER_RAIN) {
        run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
        ++turns;
    }
    assert(turns == 8 && event_count(&log, EV_WEATHER_END) == 1);
    // The rock is for rain only; the foe's rain is its own (5).
    run1(&b, sun, move(1, 0, 0), &log);
    assert(find_event(&log, EV_WEATHER_START, 0)->amount == 5);
    run1(&b, move(0, 0, 0), foe_rain, &log);
    assert(find_event(&log, EV_WEATHER_START, 0)->amount == 5);
    run1(&b, rain, move(1, 0, 0), &log);  // the same weather: fails, the count goes on
    assert(!event_count(&log, EV_WEATHER_START) && event_count(&log, EV_FAIL) == 1 && battle_field_weather(&b)->turns == 3);
    // Normalised: the rock does nothing.
    r.rules |= RULE_IGNORE_ITEMS;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    run1(&b, rain, move(1, 0, 0), &log);
    assert(find_event(&log, EV_WEATHER_START, 0)->amount == 5);
    r.rules &= (uint16_t)~RULE_IGNORE_ITEMS;
    // Terrain Extender: the move (8) and a surge (8); the foe's terrain stays 5.
    hold(&r.player[0], I_TERRAIN_EXTENDER);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    run1(&b, grass, move(1, 0, 0), &log);
    assert(find_event(&log, EV_TERRAIN_START, 0)->amount == 8 && battle_field_terrain(&b)->turns == 7);
    run1(&b, move(0, 0, 0), foe_elec, &log);
    assert(find_event(&log, EV_TERRAIN_START, 0)->amount == 5);
    run1(&b, rain, move(1, 0, 0), &log);  // the extender is not a rock
    assert(find_event(&log, EV_WEATHER_START, 0)->amount == 5);
    // Abilities: a surge with the extender, Drizzle with the rock (both 5 -> 8); an ability
    // weather that keeps the count (Gen 5) or has no end is not changed, nor the start rules.
    give(&r.player[0], T_GRASSY_SURGE);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(find_event(&log, EV_TERRAIN_START, 0)->amount == 8);
    give(&r.player[0], W_DRIZZLE);
    hold(&r.player[0], I_DAMP_ROCK);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(find_event(&log, EV_WEATHER_START, 0)->amount == 8);
    give(&r.player[0], HOOKS(2, W_HOOK(BABIL_ENTRY_WEATHER, BWEATHER_RAIN, 0, BWEATHER_TURNS_NO_END, 0)));
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(find_event(&log, EV_WEATHER_START, 0)->amount == 0 && battle_field_weather(&b)->turns == 0);
    give(&r.player[0], HOOKS(2, W_HOOK(BABIL_ENTRY_WEATHER, BWEATHER_RAIN, 0, BWEATHER_TURNS_KEEP, 0)));
    r.field.start_weather = BWEATHER_SUN;
    r.field.start_turns = 3;  // the rules' weather (no setter): 3, then Drizzle keeps that count
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(find_event(&log, EV_WEATHER_START, 0)->amount == 3 && find_event(&log, EV_WEATHER_START, 1)->amount == 3);
    r.field.start_weather = BWEATHER_RAIN;  // the start rain itself is never extended
    r.field.start_turns = 2;
    give(&r.player[0], R_NO_ABILITY);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(find_event(&log, EV_WEATHER_START, 0)->amount == 2);
    r.field.start_weather = BWEATHER_NONE;
    r.field.start_turns = 0;
    r.terrain.start_terrain = BTERRAIN_MISTY;
    r.terrain.start_turns = 3;
    hold(&r.player[0], I_TERRAIN_EXTENDER);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(find_event(&log, EV_TERRAIN_START, 0)->amount == 3);
    // Each rock for its own weather (the item is the setter's: a rock for another weather does nothing).
    const struct { BattleItem it; uint8_t kind; } rocks[] = {
        {I_HEAT_ROCK, BWEATHER_SUN}, {I_SMOOTH_ROCK, BWEATHER_SANDSTORM}, {I_ICY_ROCK, BWEATHER_HAIL},
        {I_ICY_ROCK, BWEATHER_SNOW}, {I_DAMP_ROCK, BWEATHER_RAIN}};
    for (unsigned i = 0; i < 5; ++i)
        for (unsigned other = 0; other < 2; ++other) {
            make_items(&r, BATTLE_FORMAT_SINGLE);
            const uint8_t kind = other ? (rocks[i].kind == BWEATHER_SUN ? BWEATHER_RAIN : BWEATHER_SUN) : rocks[i].kind;
            set_move(&r.player[0], 1, 240, weather_move(kind, 5), 10);
            hold(&r.player[0], rocks[i].it);
            assert(battle_init(&b, &r, 1) == BI_OK);
            battle_begin(&b, &log);
            run1(&b, rain, move(1, 0, 0), &log);
            assert(find_event(&log, EV_WEATHER_START, 0)->amount == (other ? 5 : 8));
        }
}

// Random battles in the four formats with items: deterministic, and a
// normalised battle (RULE_IGNORE_ITEMS) is event for event the battle without
// any item except its first notice.
static void test_v17_formats(void) {
    static BattleRequest r, bare;
    static Battle a, b, c;
    static EventLog la, lb, lc;
    const BattleItem items[] = {I_CHARCOAL, I_MYSTIC_WATER, I_MIRACLE_SEED, I_MUSCLE_BAND, I_WISE_GLASSES,
                                I_DAMP_ROCK, I_HEAT_ROCK, I_TERRAIN_EXTENDER, I_AMULET_COIN, {0}};
    const unsigned n = sizeof items / sizeof *items;
    const uint8_t formats[] = {BATTLE_FORMAT_SINGLE, BATTLE_FORMAT_DOUBLE, BATTLE_FORMAT_TRIPLE, BATTLE_FORMAT_ROTATION};
    unsigned battles = 0, normalised = 0, extended = 0;
    for (unsigned f = 0; f < 4; ++f)
        for (uint32_t seed = 1; seed <= 60; ++seed) {
            make_items(&r, formats[f]);
            for (unsigned side = 0; side < 2; ++side) add_reserve(&r, side);
            for (unsigned side = 0; side < 2; ++side) {
                BattleEntry *e = side ? r.opponent : r.player;
                const unsigned count = side ? r.opponent_count : r.player_count;
                for (unsigned i = 0; i < count; ++i) {
                    e[i].record.hp = e[i].record.max_hp = 90;
                    e[i].record.hp = (uint16_t)(90 - (seed + i) % 30);
                    e[i].mon.types[0] = (uint8_t)((seed + 3 * i + side) % R_COUNT);
                    e[i].record.speed = (uint16_t)(40 + (seed * 13 + i * 7 + side * 3) % 60);
                    set_move(&e[i], 1, 240, weather_move((uint8_t)(1 + (seed + i) % 2), 5), 6);
                    set_move(&e[i], 2, 580, terrain_move((uint8_t)(1 + (seed + side) % BTERRAIN_KINDS)), 6);
                    set_move(&e[i], 3, 300 + i, ref_move(60, (uint8_t)((seed + i * 5) % R_COUNT),
                                                         (seed + i) % 2 ? BCAT_PHYSICAL : BCAT_SPECIAL), 10);
                    hold(&e[i], items[(seed * 5 + side * 3 + i + f) % n]);
                    if ((seed + i) % 4 == 0) give(&e[i], (seed + side) % 2 ? W_DRIZZLE : T_ELECTRIC_SURGE);
                }
            }
            bare = r;
            for (unsigned side = 0; side < 2; ++side)
                for (unsigned i = 0; i < BATTLE_MAX_PER_SIDE; ++i) {
                    BattleEntry *e = side ? &bare.opponent[i] : &bare.player[i];
                    e->mon.item = (BattleItem){0};
                    e->record.held_item = 0;
                }
            const bool ignore = seed % 3 == 0;
            if (ignore) {
                r.rules |= RULE_IGNORE_ITEMS;
                ++normalised;
            }
            assert(battle_init(&a, &r, seed) == BI_OK && battle_init(&b, &r, seed) == BI_OK &&
                   battle_init(&c, &bare, seed) == BI_OK);
            battle_begin(&a, &la);
            battle_begin(&b, &lb);
            battle_begin(&c, &lc);
            assert(la.count == lb.count && memcmp(la.events, lb.events, la.count * sizeof(BattleEvent)) == 0);
            if (ignore)
                assert(la.events[0].kind == EV_ITEMS_NORMALIZED && la.count == lc.count + 1 &&
                       !memcmp(la.events + 1, lc.events, lc.count * sizeof(BattleEvent)));
            for (unsigned t = 0; t < 60 && a.outcome == OUTCOME_NONE; ++t) {
                Position pending;
                while (battle_pending_replacement(&a, &pending)) {
                    const uint8_t mask = battle_replacement_mask(&a, pending);
                    uint8_t pick = 0;
                    while (!(mask & (1u << pick))) ++pick;
                    assert(battle_replace(&a, pending, pick, &la) && battle_replace(&b, pending, pick, &lb));
                    if (ignore) assert(battle_replace(&c, pending, pick, &lc));
                }
                if (a.outcome != OUTCOME_NONE) break;
                Action actions[BC_MAX_ACTIONS];
                unsigned count = 0;
                for (uint8_t side = 0; side < 2; ++side) {
                    Position actors[BC_MAX_ACTIVE];
                    const unsigned k = battle_collect_actors(&a, side, actors);
                    for (unsigned i = 0; i < k; ++i) battle_choose_at(&a, actors[i], &actions[count++]);
                }
                for (unsigned i = 0; i < count; ++i) {
                    Action dummy;
                    battle_choose_at(&b, actions[i].user, &dummy);
                    if (ignore) battle_choose_at(&c, actions[i].user, &dummy);
                }
                battle_run_turn(&a, actions, count, &la);
                battle_run_turn(&b, actions, count, &lb);
                assert(la.count == lb.count && la.count < BC_MAX_EVENTS &&
                       memcmp(la.events, lb.events, la.count * sizeof(BattleEvent)) == 0);
                if (ignore) {
                    battle_run_turn(&c, actions, count, &lc);
                    assert(la.count == lc.count && !memcmp(la.events, lc.events, la.count * sizeof(BattleEvent)));
                }
                for (unsigned i = 0; i < la.count; ++i)
                    if ((la.events[i].kind == EV_WEATHER_START || la.events[i].kind == EV_TERRAIN_START) &&
                        la.events[i].amount == 8)
                        ++extended;
            }
            // B0 never moves an item: every Pokemon still holds its own.
            for (unsigned side = 0; side < 2; ++side)
                for (unsigned i = 0; i < a.sides[side].count; ++i) {
                    const Battler *x = &a.sides[side].battlers[i];
                    assert(x->item_from == (x->entry->record.held_item ? x->entry : NULL));
                }
            assert(memcmp(&a.sides, &b.sides, sizeof a.sides) == 0 && a.rng == b.rng &&
                   memcmp(&a.field, &b.field, sizeof a.field) == 0);
            if (ignore) assert(a.rng == c.rng && a.outcome == c.outcome);
            r.rules &= (uint16_t)~RULE_IGNORE_ITEMS;
            ++battles;
        }
    assert(normalised == 80 && extended > 10);
    printf("v17 formats: %u battles (%u normalised), extended field effects %u\n", battles, normalised, extended);
}

// ------------------------------------------------------------- V17 against the Expansion engine
//
// Hits the Frontier build's test runner computes with the real Expansion
// code (test/battle/gc_item_reference.c in frontier-gc-m4b0, same stats,
// rolls 100/93/85, no crit). `nearest` is the same hit with the item's
// factor rounded to the nearest 4.12 value instead of truncated, printed to
// show where truncation matters.
typedef struct {
    const char *name;
    RefMon atk, def;
    BattleMove move;
    BattleItem atk_item, def_item;
    uint8_t terrain;
    uint16_t expansion[3];
} ItemRefCase;

static void test_v17_expansion_reference(void) {
    const BattleItem nothing = {0};
    const ItemRefCase cases[] = {
        {"Charcoal x1.2: Flamethrower, STAB (Charizard vs Snorlax)",
         RMON(50, R_FIRE, R_FLYING, 100, 999, 999, 100, 0, R_BLAZE),
         RMON(50, R_NORMAL, R_NORMAL, 100, 999, 999, 30, 0, R_NO_ABILITY),
         ref_move(95, R_FIRE, BCAT_SPECIAL), I_CHARCOAL, nothing, 0, {78, 72, 66}},
        {"Sea Incense x1.2: Surf, STAB (Pelipper vs Snorlax)",
         RMON(50, R_WATER, R_FLYING, 100, 999, 999, 65, 0, R_NO_ABILITY),
         RMON(50, R_NORMAL, R_NORMAL, 100, 999, 999, 30, 0, R_NO_ABILITY),
         ref_move(95, R_WATER, BCAT_SPECIAL), I_SEA_INCENSE, nothing, 0, {78, 72, 66}},
        {"Muscle Band x1.1 truncated: Body Slam 85, STAB (Snorlax vs Snorlax)",
         RMON(50, R_NORMAL, R_NORMAL, 100, 999, 999, 30, 0, R_NO_ABILITY),
         RMON(50, R_NORMAL, R_NORMAL, 100, 999, 999, 30, 0, R_NO_ABILITY),
         ref_move(85, R_NORMAL, BCAT_PHYSICAL), I_MUSCLE_BAND, nothing, 0, {63, 58, 52}},
        {"Wise Glasses x1.1 truncated: Thunderbolt 95, STAB (Raichu vs Snorlax)",
         RMON(50, R_ELECTRIC, R_ELECTRIC, 100, 999, 999, 110, 0, R_NO_ABILITY),
         RMON(50, R_NORMAL, R_NORMAL, 100, 999, 999, 30, 0, R_NO_ABILITY),
         ref_move(95, R_ELECTRIC, BCAT_SPECIAL), I_WISE_GLASSES, nothing, 0, {70, 64, 58}},
        {"Miracle Seed after Grassy Terrain: Energy Ball, STAB (Rillaboom vs Snorlax)",
         RMON(50, R_GRASS, R_GRASS, 100, 999, 999, 85, 0, T_GRASSY_SURGE),
         RMON(50, R_NORMAL, R_NORMAL, 100, 999, 999, 30, 0, R_NO_ABILITY),
         ref_move(90, R_GRASS, BCAT_SPECIAL), I_MIRACLE_SEED, nothing, BTERRAIN_GRASSY, {94, 87, 79}},
        {"Technician then Charcoal: Ember 40 (Scizor vs Snorlax)",
         RMON(50, R_BUG, R_STEEL, 100, 999, 999, 65, 0, R_TECHNICIAN),
         RMON(50, R_NORMAL, R_NORMAL, 100, 999, 999, 30, 0, R_NO_ABILITY),
         ref_move(40, R_FIRE, BCAT_SPECIAL), I_CHARCOAL, nothing, 0, {33, 30, 28}},
        {"Charcoal held by the target: no boost (Charizard vs Snorlax)",
         RMON(50, R_FIRE, R_FLYING, 100, 999, 999, 100, 0, R_BLAZE),
         RMON(50, R_NORMAL, R_NORMAL, 100, 999, 999, 30, 0, R_NO_ABILITY),
         ref_move(95, R_FIRE, BCAT_SPECIAL), nothing, I_CHARCOAL, 0, {64, 58, 54}},
        {"Muscle Band on a special move: no boost (Snorlax Hyper Voice 90 vs Snorlax)",
         RMON(50, R_NORMAL, R_NORMAL, 100, 999, 999, 30, 0, R_NO_ABILITY),
         RMON(50, R_NORMAL, R_NORMAL, 100, 999, 999, 30, 0, R_NO_ABILITY),
         ref_move(90, R_NORMAL, BCAT_SPECIAL), I_MUSCLE_BAND, nothing, 0, {61, 57, 51}},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; ++i) {
        const ItemRefCase *k = &cases[i];
        BattleItem nearest = k->atk_item;
        if (nearest.cls == BICLASS_RECORDS) nearest.records[0].mod.stage &= (uint8_t)~BMOD_FLOORED;
        printf("  iref %u %-74s", i + 1, k->name);
        for (unsigned r = 0; r < 3; ++r) {
            const uint16_t v17 = i_damage(&k->atk, &k->def, k->move, k->atk_item, k->def_item, k->terrain, ref_rolls[r]);
            const uint16_t none = i_damage(&k->atk, &k->def, k->move, nothing, nothing, k->terrain, ref_rolls[r]);
            const uint16_t near = i_damage(&k->atk, &k->def, k->move, nearest, k->def_item, k->terrain, ref_rolls[r]);
            printf(" %u%%: v17=%u none=%u nearest=%u exp=%u", ref_rolls[r], v17, none, near, k->expansion[r]);
            assert(v17 == k->expansion[r]);
        }
        printf("\n");
    }
}

// ------------------------------------------------------------- V18 end-of-turn items (docs/items.md)
//
// Leftovers and Black Sludge as the Frontier exporter resolves them (ids are
// the build's, diagnostic only): BITEM_HP records at BITIME_END_TURN. The V18
// cases add a Poison type to the reference chart (which has none) and make
// every match-up neutral; poison_type is that type, so poison-type immunity
// and Black Sludge agree.

#define R_POISON R_COUNT
static BattleItemRecord hp_record(uint8_t op, uint8_t divisor, uint8_t pred, uint8_t arg) {
    BattleItemRecord x = {.kind = BITEM_HP};
    x.hp.timing = BITIME_END_TURN;
    x.hp.op = op;
    x.hp.divisor = divisor;
    x.hp.pred = (BattlePredicate){pred, arg};
    return x;
}
static BattleItem item_hp(uint16_t id, unsigned n, const BattleItemRecord *list) {
    BattleItem it = {.cls = BICLASS_RECORDS, .count = (uint8_t)n, .source = id};
    for (unsigned i = 0; i < n; ++i) it.records[i] = list[i];
    return it;
}
#define IHP(id, ...) item_hp(id, sizeof((BattleItemRecord[]){__VA_ARGS__}) / sizeof(BattleItemRecord), \
                             (BattleItemRecord[]){__VA_ARGS__})
#define I_LEFTOVERS18 IHP(472, hp_record(BIHP_HEAL, 16, BPRED_NONE, 0))
#define I_BLACK_SLUDGE IHP(487, hp_record(BIHP_HEAL, 16, BPRED_HAS_TYPE, R_POISON), \
                           hp_record(BIHP_DAMAGE, 8, BPRED_NONE, 0))
#define I_LIFE_ORB I_UNSUPPORTED(479)
#define SHED_SKIN_ALWAYS HOOKS(61, hk(BABIL_END_TURN_CURE, 0xfe, 0, 0, 1, 1, 0, 0))  // WITH_RNG(RNG_SHED_SKIN, TRUE)

static void make_end_turn_items(BattleRequest *r, uint8_t format) {
    make_items(r, format);
    r->version = BATTLE_REQUEST_VERSION_END_TURN_ITEMS;
    r->type_count = R_COUNT + 1;
    memset(r->type_chart, 4, sizeof r->type_chart);
    r->conditions.poison_type = R_POISON;
    r->conditions.steel_type = R_STEEL;
}

// The reference's Celebrate: a move with no HP effect (+1 accuracy).
static BattleMove wait_move(void) {
    BattleMove m = status_move(BTARGET_SELF, 0);
    push(&m, fx(BEFF_STAT, BWHO_TARGET, 0, BSTAT_ACC, 1, 0));
    return m;
}

// The case tables below give only the fields a case needs (the rest are zero).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
typedef struct {
    uint16_t max_hp, hp, speed;
    uint8_t type;
    uint32_t status;
    BattleItem item;
    BattleAbility ability;
    BattleMove move;  // zero: wait_move
} EtMon;

static void et_mon(BattleEntry *e, const EtMon *m) {
    if (!m->max_hp) return;
    e->record.max_hp = m->max_hp;
    e->record.hp = m->hp;
    e->record.speed = m->speed;
    e->mon.type_count = 1;
    e->mon.types[0] = e->mon.types[1] = m->type;
    e->record.status = m->status;
    hold(e, m->item);
    give(e, m->ability);
    set_move(e, 0, 150, m->move.effect_count ? m->move : wait_move(), 40);
    for (unsigned i = 1; i < 4; ++i) e->mon.moves[i].unsupported = UNSUP_EMPTY, e->record.moves[i] = 0;
}

// An HP or order event at the end of the turn, as the reference shows it (HP_BAR, cure, faint, stat).
typedef struct {
    EventKind kind;
    uint8_t side, slot;
    uint16_t amount, hp_after;
} EtEvent;

static bool end_turn_kind(const BattleEvent *e) {
    switch (e->kind) {
    case EV_ITEM_HEAL: case EV_ITEM_DAMAGE: case EV_TERRAIN_HEAL: case EV_RESIDUAL: case EV_WEATHER_DAMAGE:
    case EV_HEAL: case EV_CURE: case EV_FAINT: return true;
    case EV_STAGE: return e->stat == BSTAT_SPE;  // Speed Boost (the wait move raises accuracy)
    default: return false;
    }
}

static unsigned end_turn_events(const EventLog *log, EtEvent *out, unsigned capacity) {
    unsigned n = 0;
    for (unsigned i = 0; i < log->count && n < capacity; ++i) {
        const BattleEvent *e = &log->events[i];
        if (!end_turn_kind(e)) continue;
        const bool amounts = e->kind != EV_CURE && e->kind != EV_FAINT && e->kind != EV_STAGE;
        out[n++] = (EtEvent){e->kind, e->target.side, e->target.slot, amounts ? e->amount : 0,
                             amounts ? e->hp_after : 0};
    }
    return n;
}

#define ET_MAX 12
typedef struct {
    const char *name;
    uint8_t format;
    EtMon player[3], opponent[3];
    EtEvent expected[ET_MAX];
} EtCase;
#define ET(kind, side, slot, amount, after) {kind, side, slot, amount, after}
#define NOTHING {.max_hp = 0}

static void run_et_case(const EtCase *k, uint8_t version, EtEvent *got, unsigned *n) {
    static BattleRequest r;
    static Battle b;
    static EventLog log;
    make_end_turn_items(&r, k->format);
    r.version = version;
    for (unsigned i = 0; i < r.active_per_side; ++i) {
        et_mon(&r.player[i], &k->player[i]);
        et_mon(&r.opponent[i], &k->opponent[i]);
    }
    assert(battle_init(&b, &r, 7) == BI_OK);
    battle_begin(&b, &log);
    Action actions[BC_MAX_ACTIONS];
    unsigned count = 0;
    for (uint8_t side = 0; side < 2; ++side)
        for (uint8_t slot = 0; slot < r.active_per_side; ++slot)
            actions[count++] = (Action){.kind = ACT_MOVE, .user = {side, slot}, .target = {{side, slot}}};
    battle_run_turn(&b, actions, count, &log);
    *n = end_turn_events(&log, got, ET_MAX);
}

static void check_et(const EtCase *k, const EtEvent *got, unsigned n) {
    unsigned expected = 0;
    while (expected < ET_MAX && k->expected[expected].kind) ++expected;
    bool same = n == expected;
    for (unsigned i = 0; same && i < n; ++i)
        same = got[i].kind == k->expected[i].kind && got[i].side == k->expected[i].side &&
               got[i].slot == k->expected[i].slot && got[i].amount == k->expected[i].amount &&
               got[i].hp_after == k->expected[i].hp_after;
    if (!same) {
        printf("v18 case \"%s\": got", k->name);
        for (unsigned i = 0; i < n; ++i)
            printf(" [%u %u:%u %u->%u]", got[i].kind, got[i].side, got[i].slot, got[i].amount, got[i].hp_after);
        printf("\n");
        fflush(stdout);
    }
    assert(same);
}

// The cases of test/battle/gc_end_turn_item_reference.c (frontier-gc-m4b1a):
// the same Pokemon, one turn of Celebrate, the end-of-turn HP changes in the
// reference's order with its exact amounts. EV_ITEM_HEAL/DAMAGE is the
// reference's item HP_BAR, EV_TERRAIN_HEAL the Grassy Terrain one, EV_RESIDUAL
// the status one, EV_WEATHER_DAMAGE the sandstorm/hail one, EV_HEAL Rain Dish.
static void test_v18_end_turn_reference(void) {
    const EtCase et_cases[] = {
    {"Leftovers: max HP / 16, rounded down", BATTLE_FORMAT_SINGLE,
     {{175, 100, 100, R_NORMAL, 0, I_LEFTOVERS18}}, {{200, 200, 50, R_NORMAL}},
     {ET(EV_ITEM_HEAL, 0, 0, 10, 110)}},
    {"Leftovers stops at max HP", BATTLE_FORMAT_SINGLE,
     {{160, 155, 100, R_NORMAL, 0, I_LEFTOVERS18}}, {{200, 200, 50, R_NORMAL}},
     {ET(EV_ITEM_HEAL, 0, 0, 5, 160)}},
    {"Leftovers does nothing at full HP", BATTLE_FORMAT_SINGLE,
     {{160, 160, 100, R_NORMAL, 0, I_LEFTOVERS18}}, {{200, 200, 50, R_NORMAL}}, {{0}}},
    {"Leftovers heals at least 1", BATTLE_FORMAT_SINGLE,
     {{15, 10, 100, R_NORMAL, 0, I_LEFTOVERS18}}, {{200, 200, 50, R_NORMAL}},
     {ET(EV_ITEM_HEAL, 0, 0, 1, 11)}},
    {"Leftovers heals from 1 HP", BATTLE_FORMAT_SINGLE,
     {{160, 1, 100, R_NORMAL, 0, I_LEFTOVERS18}}, {{200, 200, 50, R_NORMAL}},
     {ET(EV_ITEM_HEAL, 0, 0, 10, 11)}},
    {"Black Sludge heals a Poison type by max HP / 16", BATTLE_FORMAT_SINGLE,
     {{161, 100, 100, R_POISON, 0, I_BLACK_SLUDGE}}, {{200, 200, 50, R_NORMAL}},
     {ET(EV_ITEM_HEAL, 0, 0, 10, 110)}},
    {"Black Sludge does nothing to a Poison type at full HP", BATTLE_FORMAT_SINGLE,
     {{161, 161, 100, R_POISON, 0, I_BLACK_SLUDGE}}, {{200, 200, 50, R_NORMAL}}, {{0}}},
    {"Black Sludge hurts any other type by max HP / 8", BATTLE_FORMAT_SINGLE,
     {{161, 161, 100, R_NORMAL, 0, I_BLACK_SLUDGE}}, {{200, 200, 50, R_NORMAL}},
     {ET(EV_ITEM_DAMAGE, 0, 0, 20, 141)}},
    {"Black Sludge hurts by at least 1", BATTLE_FORMAT_SINGLE,
     {{7, 7, 100, R_NORMAL, 0, I_BLACK_SLUDGE}}, {{200, 200, 50, R_NORMAL}},
     {ET(EV_ITEM_DAMAGE, 0, 0, 1, 6)}},
    {"Black Sludge can make its holder faint", BATTLE_FORMAT_SINGLE,
     {{160, 20, 100, R_NORMAL, 0, I_BLACK_SLUDGE}}, {{200, 200, 50, R_NORMAL}},
     {ET(EV_ITEM_DAMAGE, 0, 0, 20, 0), ET(EV_FAINT, 0, 0, 0, 0)}},
    {"Grassy Terrain heal, then Leftovers", BATTLE_FORMAT_SINGLE,
     {{160, 100, 100, R_NORMAL, 0, I_LEFTOVERS18}}, {{200, 200, 50, R_NORMAL, 0, {0}, T_GRASSY_SURGE}},
     {ET(EV_TERRAIN_HEAL, 0, 0, 10, 110), ET(EV_ITEM_HEAL, 0, 0, 10, 120)}},
    {"Grassy Terrain fills the HP, Leftovers then does nothing", BATTLE_FORMAT_SINGLE,
     {{160, 150, 100, R_NORMAL, 0, I_LEFTOVERS18}}, {{200, 200, 50, R_NORMAL, 0, {0}, T_GRASSY_SURGE}},
     {ET(EV_TERRAIN_HEAL, 0, 0, 10, 160)}},
    {"Grassy Terrain heal, then Black Sludge damage", BATTLE_FORMAT_SINGLE,
     {{160, 100, 100, R_NORMAL, 0, I_BLACK_SLUDGE}}, {{200, 200, 50, R_NORMAL, 0, {0}, T_GRASSY_SURGE}},
     {ET(EV_TERRAIN_HEAL, 0, 0, 10, 110), ET(EV_ITEM_DAMAGE, 0, 0, 20, 90)}},
    {"Grassy Terrain heal, then Black Sludge heal (Poison)", BATTLE_FORMAT_SINGLE,
     {{160, 100, 100, R_POISON, 0, I_BLACK_SLUDGE}}, {{200, 200, 50, R_NORMAL, 0, {0}, T_GRASSY_SURGE}},
     {ET(EV_TERRAIN_HEAL, 0, 0, 10, 110), ET(EV_ITEM_HEAL, 0, 0, 10, 120)}},
    {"Leftovers before poison damage", BATTLE_FORMAT_SINGLE,
     {{160, 100, 100, R_NORMAL, BST_POISON, I_LEFTOVERS18}}, {{200, 200, 50, R_NORMAL}},
     {ET(EV_ITEM_HEAL, 0, 0, 10, 110), ET(EV_RESIDUAL, 0, 0, 20, 90)}},
    {"Leftovers before bad poison damage", BATTLE_FORMAT_SINGLE,
     {{160, 100, 100, R_NORMAL, BST_TOXIC, I_LEFTOVERS18}}, {{200, 200, 50, R_NORMAL}},
     {ET(EV_ITEM_HEAL, 0, 0, 10, 110), ET(EV_RESIDUAL, 0, 0, 10, 100)}},
    {"Leftovers before burn damage", BATTLE_FORMAT_SINGLE,
     {{160, 100, 100, R_NORMAL, BST_BURN, I_LEFTOVERS18}}, {{200, 200, 50, R_NORMAL}},
     {ET(EV_ITEM_HEAL, 0, 0, 10, 110), ET(EV_RESIDUAL, 0, 0, 10, 100)}},
    {"Sandstorm damage, then Leftovers", BATTLE_FORMAT_SINGLE,
     {{160, 100, 100, R_NORMAL, 0, I_LEFTOVERS18}}, {{200, 200, 50, R_ROCK, 0, {0}, W_SAND_STREAM}},
     {ET(EV_WEATHER_DAMAGE, 0, 0, 10, 90), ET(EV_ITEM_HEAL, 0, 0, 10, 100)}},
    {"Hail damage, then Leftovers", BATTLE_FORMAT_SINGLE,
     {{160, 100, 100, R_NORMAL, 0, I_LEFTOVERS18}},
     {{200, 200, 50, R_ICE, 0, {0}, {0}, {.category = BCAT_STATUS, .target = BTARGET_SELF, .effect_count = 1,
                                          .effects = {{BEFF_WEATHER, {{BWHO_USER, 0, BWEATHER_HAIL, 0, 5}}, 0}}}}},
     {ET(EV_WEATHER_DAMAGE, 0, 0, 10, 90), ET(EV_ITEM_HEAL, 0, 0, 10, 100)}},
    {"Rain Dish, then Leftovers", BATTLE_FORMAT_SINGLE,
     {{160, 100, 100, R_NORMAL, 0, I_LEFTOVERS18, W_RAIN_DISH}}, {{200, 200, 50, R_NORMAL, 0, {0}, W_DRIZZLE}},
     {ET(EV_HEAL, 0, 0, 10, 110), ET(EV_ITEM_HEAL, 0, 0, 10, 120)}},
    {"Shed Skin cures the poison, then Leftovers, no poison damage", BATTLE_FORMAT_SINGLE,
     {{160, 100, 100, R_NORMAL, BST_POISON, I_LEFTOVERS18, SHED_SKIN_ALWAYS}}, {{200, 200, 50, R_NORMAL}},
     {ET(EV_CURE, 0, 0, 0, 0), ET(EV_ITEM_HEAL, 0, 0, 10, 110)}},
    {"Leftovers, then Speed Boost", BATTLE_FORMAT_SINGLE,
     {{160, 100, 100, R_NORMAL, 0, I_LEFTOVERS18, SPEED_BOOST}}, {{200, 200, 50, R_NORMAL}},
     {ET(EV_ITEM_HEAL, 0, 0, 10, 110), ET(EV_STAGE, 0, 0, 0, 0)}},
    {"Each Pokemon in speed order, fastest first", BATTLE_FORMAT_DOUBLE,
     {{200, 100, 3, R_NORMAL, 0, I_LEFTOVERS18}, {200, 100, 1, R_NORMAL, 0, I_LEFTOVERS18}},
     {{200, 100, 2, R_NORMAL, 0, I_LEFTOVERS18}, {200, 100, 4, R_NORMAL, 0, I_LEFTOVERS18}},
     {ET(EV_ITEM_HEAL, 1, 1, 12, 112), ET(EV_ITEM_HEAL, 0, 0, 12, 112), ET(EV_ITEM_HEAL, 1, 0, 12, 112),
      ET(EV_ITEM_HEAL, 0, 1, 12, 112)}},
    {"Equal speeds keep the battler order (player left, foe left, player right, foe right)", BATTLE_FORMAT_DOUBLE,
     {{200, 100, 50, R_NORMAL, 0, I_LEFTOVERS18}, {200, 100, 50, R_NORMAL, 0, I_LEFTOVERS18}},
     {{200, 100, 50, R_NORMAL, 0, I_LEFTOVERS18}, {200, 100, 50, R_NORMAL, 0, I_LEFTOVERS18}},
     {ET(EV_ITEM_HEAL, 0, 0, 12, 112), ET(EV_ITEM_HEAL, 1, 0, 12, 112), ET(EV_ITEM_HEAL, 0, 1, 12, 112),
      ET(EV_ITEM_HEAL, 1, 1, 12, 112)}},
    {"Pokemon by Pokemon: the terrain heal and the item of one, then the next", BATTLE_FORMAT_DOUBLE,
     {{200, 100, 4, R_NORMAL, 0, I_LEFTOVERS18}, {200, 100, 2, R_NORMAL, 0, I_BLACK_SLUDGE}},
     {{200, 100, 3, R_NORMAL, 0, I_BLACK_SLUDGE, T_GRASSY_SURGE}, {200, 100, 1, R_POISON, 0, I_BLACK_SLUDGE}},
     {ET(EV_TERRAIN_HEAL, 0, 0, 12, 112), ET(EV_ITEM_HEAL, 0, 0, 12, 124), ET(EV_TERRAIN_HEAL, 1, 0, 12, 112),
      ET(EV_ITEM_DAMAGE, 1, 0, 25, 87), ET(EV_TERRAIN_HEAL, 0, 1, 12, 112), ET(EV_ITEM_DAMAGE, 0, 1, 25, 87),
      ET(EV_TERRAIN_HEAL, 1, 1, 12, 112), ET(EV_ITEM_HEAL, 1, 1, 12, 124)}},
    {"A faint to Black Sludge: the others go on, then the status damage", BATTLE_FORMAT_DOUBLE,
     {{160, 10, 4, R_NORMAL, 0, I_BLACK_SLUDGE}, {200, 100, 2, R_NORMAL, BST_POISON, I_LEFTOVERS18}},
     {{200, 100, 3, R_NORMAL, 0, I_LEFTOVERS18}, {200, 100, 1, R_NORMAL, BST_BURN, I_LEFTOVERS18}},
     {ET(EV_ITEM_DAMAGE, 0, 0, 20, 0), ET(EV_FAINT, 0, 0, 0, 0), ET(EV_ITEM_HEAL, 1, 0, 12, 112),
      ET(EV_ITEM_HEAL, 0, 1, 12, 112), ET(EV_ITEM_HEAL, 1, 1, 12, 112), ET(EV_RESIDUAL, 0, 1, 25, 87),
      ET(EV_RESIDUAL, 1, 1, 12, 100)}},
    };
    EtEvent got[ET_MAX];
    unsigned n;
    for (unsigned i = 0; i < sizeof et_cases / sizeof *et_cases; ++i) {
        run_et_case(&et_cases[i], BATTLE_REQUEST_VERSION_END_TURN_ITEMS, got, &n);
        check_et(&et_cases[i], got, n);
    }
    printf("v18 end-turn reference: %u cases\n", (unsigned)(sizeof et_cases / sizeof *et_cases));
}

static void test_v18_wire(void) {
    static BattleRequest r, d;
    static uint8_t bytes[4096], v17[4096];
    make_end_turn_items(&r, BATTLE_FORMAT_DOUBLE);
    hold(&r.player[0], I_LEFTOVERS18);
    hold(&r.player[1], I_BLACK_SLUDGE);
    hold(&r.opponent[0], I_CHARCOAL);
    hold(&r.opponent[1], I_DAMP_ROCK);
    const uint32_t n = battle_request_encode(&r, bytes, sizeof bytes);
    assert(n && bytes[0] == BATTLE_REQUEST_VERSION_END_TURN_ITEMS);
    assert(battle_request_decode(bytes, n, &d) && d.version == BATTLE_REQUEST_VERSION_END_TURN_ITEMS);
    for (unsigned side = 0; side < 2; ++side)
        for (unsigned i = 0; i < 2; ++i) {
            const BattleItem *a = side ? &r.opponent[i].mon.item : &r.player[i].mon.item;
            const BattleItem *x = side ? &d.opponent[i].mon.item : &d.player[i].mon.item;
            assert(a->cls == x->cls && a->count == x->count && a->source == x->source);
            for (unsigned k = 0; k < a->count; ++k)
                assert(memcmp(&a->records[k], &x->records[k], sizeof a->records[k]) == 0);
        }
    // The V18 layout is the V17 layout: the same request with the HP items
    // removed encodes to the same length under both versions.
    static BattleRequest same;
    same = r;
    hold(&same.player[0], I_AMULET_COIN);
    hold(&same.player[1], I_AMULET_COIN);
    static uint8_t plain[4096];
    const uint32_t n18 = battle_request_encode(&same, plain, sizeof plain);
    same.version = BATTLE_REQUEST_VERSION_ITEMS;
    const uint32_t n17 = battle_request_encode(&same, v17, sizeof v17);
    assert(n18 && n18 == n17 && memcmp(plain + 1, v17 + 1, n17 - 1) == 0);
    // V17 has no HP record: refused by the encoder and the decoder.
    static BattleRequest old;
    old = r;
    old.version = BATTLE_REQUEST_VERSION_ITEMS;
    assert(!battle_request_encode(&old, v17, sizeof v17));
    memcpy(v17, bytes, n);
    v17[0] = BATTLE_REQUEST_VERSION_ITEMS;
    assert(!battle_request_decode(v17, n, &d));
    // Every byte of a record is checked: timing, operation, divisor, predicate, reserved, flags.
    const BattleItem good = I_BLACK_SLUDGE;
    assert(battle_item_valid(&good, 487, R_COUNT + 1, BATTLE_REQUEST_VERSION_END_TURN_ITEMS));
    assert(!battle_item_valid(&good, 487, R_COUNT, BATTLE_REQUEST_VERSION_END_TURN_ITEMS));  // type out of range
    assert(!battle_item_valid(&good, 487, R_COUNT + 1, BATTLE_REQUEST_VERSION_ITEMS));
    BattleItem bad[11];
    for (unsigned i = 0; i < 11; ++i) bad[i] = I_LEFTOVERS18;
    bad[0].records[0].hp.timing = BITIME_NONE;
    bad[1].records[0].hp.timing = BITIME_COUNT;
    bad[2].records[0].hp.op = BIHP_NONE;
    bad[3].records[0].hp.op = BIHP_COUNT;
    bad[4].records[0].hp.divisor = 0;
    bad[5].records[0].hp.pred = (BattlePredicate){BPRED_STATUS, 1u << BST_POISON};
    bad[6].records[0].hp.pred = (BattlePredicate){BPRED_NONE, 1};
    bad[7].records[0].hp.pred = (BattlePredicate){BPRED_HAS_TYPE | BPRED_DEFENDER, R_FIRE};
    bad[8].records[0].hp.reserved = 1;
    bad[9].records[0].flags = 1;
    bad[10].records[0].hp.pred = (BattlePredicate){BPRED_HP_AT_MOST, 2};
    for (unsigned i = 0; i < 11; ++i) {
        assert(!battle_item_valid(&bad[i], 472, R_COUNT + 1, BATTLE_REQUEST_VERSION_END_TURN_ITEMS));
        BattleRequest *x = &old;
        *x = r;
        hold(&x->player[0], bad[i]);
        assert(!battle_request_encode(x, v17, sizeof v17));
    }
    const BattleItem fine = I_LEFTOVERS18;
    assert(battle_item_valid(&fine, 472, R_COUNT + 1, BATTLE_REQUEST_VERSION_END_TURN_ITEMS));
    // Wire bytes: a reserved byte or an unknown operation on the wire is refused by the decoder.
    static const uint8_t pattern[] = {BITEM_HP, BITIME_END_TURN, BIHP_HEAL, 16, BPRED_NONE, 0, 0, 0};
    unsigned at = 0;
    for (unsigned i = 0; i + sizeof pattern <= n; ++i)
        if (!memcmp(bytes + i, pattern, sizeof pattern)) { at = i; break; }
    assert(at);
    static const uint8_t offsets[] = {1, 2, 3, 4, 5, 6, 7};
    static const uint8_t values[] = {2, 3, 0, 2, 1, 1, 1};
    for (unsigned i = 0; i < sizeof offsets; ++i) {
        memcpy(v17, bytes, n);
        v17[at + offsets[i]] = values[i];
        assert(!battle_request_decode(v17, n, &d));
    }
    memcpy(v17, bytes, n);
    assert(battle_request_decode(v17, n, &d));
    // The largest request does not grow: still four records per item.
    assert(BATTLE_REQUEST_MAX_BYTES(6, 21) == 3165);
}

static void test_v18_init(void) {
    static BattleRequest r;
    static Battle b;
    static EventLog log;
    make_end_turn_items(&r, BATTLE_FORMAT_SINGLE);
    hold(&r.player[0], I_LEFTOVERS18);
    hold(&r.opponent[0], I_BLACK_SLUDGE);
    assert(battle_init(&b, &r, 1) == BI_OK && battle_items_simulated(&b));
    // A request built in memory gets the decoder's check (no zero divisor reaches the core).
    BattleItem zero = I_LEFTOVERS18;
    zero.records[0].hp.divisor = 0;
    hold(&r.player[0], zero);
    assert(battle_init(&b, &r, 1) == BI_CONDITIONS);
    hold(&r.player[0], I_LEFTOVERS18);
    r.player[0].record.held_item = 999;  // the block must name the record's item
    assert(battle_init(&b, &r, 1) == BI_CONDITIONS);
    hold(&r.player[0], I_LEFTOVERS18);
    // All or nothing: one unsupported item refuses the battle unless every item is off.
    hold(&r.opponent[0], I_LIFE_ORB);
    assert(battle_init(&b, &r, 1) == BI_UNSUPPORTED_ITEM);
    r.rules |= RULE_IGNORE_ITEMS;
    r.player[0].record.hp = 100;
    assert(battle_init(&b, &r, 1) == BI_OK && battle_items_normalized(&b));
    battle_begin(&b, &log);
    assert(log.events[0].kind == EV_ITEMS_NORMALIZED && log.events[0].amount == 2 && log.events[0].battler == 1);
    run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
    assert(!event_count(&log, EV_ITEM_HEAL) && !event_count(&log, EV_ITEM_DAMAGE));
    // RULE_IGNORE_ITEMS turns off even a battle of supported items only: no Leftovers.
    hold(&r.opponent[0], I_BLACK_SLUDGE);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    assert(log.events[0].kind == EV_ITEMS_NORMALIZED && log.events[0].battler == 0);
    for (unsigned t = 0; t < 3; ++t) {
        run1(&b, move(0, 0, 0), move(1, 0, 0), &log);
        assert(!event_count(&log, EV_ITEM_HEAL) && !event_count(&log, EV_ITEM_DAMAGE));
    }
    // Strict: no normalisation that drops an item effect.
    r.rules |= RULE_STRICT_ITEMS;
    assert(battle_init(&b, &r, 1) == BI_ITEMS_NORMALIZED);
    r.rules &= (uint16_t)~RULE_IGNORE_ITEMS;
    assert(battle_init(&b, &r, 1) == BI_OK);
    hold(&r.opponent[0], I_LIFE_ORB);
    assert(battle_init(&b, &r, 1) == BI_UNSUPPORTED_ITEM);
}

// The state and the timing: the HP records are the ones of the item the
// Pokemon holds now (Battler.item_from, here another entry's Black Sludge),
// the event names that item, and a record acts only at its own timing.
static void test_v18_state(void) {
    static BattleRequest r;
    static Battle b;
    static EventLog log;
    make_end_turn_items(&r, BATTLE_FORMAT_SINGLE);
    add_reserve(&r, 0);
    hold(&r.player[0], I_LEFTOVERS18);
    hold(&r.player[1], I_BLACK_SLUDGE);
    r.player[0].record.max_hp = 160;
    r.player[0].record.hp = 160;
    set_move(&r.player[0], 0, 150, wait_move(), 40);
    set_move(&r.opponent[0], 0, 150, wait_move(), 40);
    assert(battle_init(&b, &r, 1) == BI_OK);
    Battler *lead = bc_mutable_at(&b, (Position){0, 0});
    lead->item_from = &r.player[1];  // it now holds the reserve's Black Sludge (an item change of a later stage)
    log.count = 0;
    bc_item_hp(&b, (Position){0, 0}, BITIME_NONE, &log);  // not this record's timing
    bc_item_hp(&b, (Position){0, 0}, BITIME_COUNT, &log);
    assert(log.count == 0 && lead->hp == 160);
    battle_begin(&b, &log);
    run1(&b, (Action){.kind = ACT_MOVE, .user = {0, 0}, .target = {{0, 0}}},
         (Action){.kind = ACT_MOVE, .user = {1, 0}, .target = {{1, 0}}}, &log);
    const BattleEvent *hurt = find_event(&log, EV_ITEM_DAMAGE, 0);
    assert(hurt && hurt->item == 487 && hurt->amount == 20 && lead->hp == 140 && !find_event(&log, EV_ITEM_HEAL, 0));
    assert(battle_held_item(lead) == 487 && lead->entry->record.held_item == 472);
}

// The item is the Pokemon's wherever it stands: after a switch the newcomer's
// item acts, after a Shift the moved Pokemon's (at its new position), after a
// rotation the one in front; a Pokemon in the back never does.
static void test_v18_positions(void) {
    static BattleRequest r;
    static Battle b;
    static EventLog log;
    // Switch: the reserve with Leftovers comes in and heals at the end of the turn.
    make_end_turn_items(&r, BATTLE_FORMAT_SINGLE);
    add_reserve(&r, 0);
    r.player[0].record.hp = r.player[0].record.max_hp;
    hold(&r.player[1], I_LEFTOVERS18);
    r.player[1].record.max_hp = 160;
    r.player[1].record.hp = 100;
    set_move(&r.opponent[0], 0, 150, wait_move(), 40);
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    const Action sw[] = {{.kind = ACT_SWITCH, .user = {0, 0}, .reserve = 1},
                         {.kind = ACT_MOVE, .user = {1, 0}, .target = {{1, 0}}}};
    battle_run_turn(&b, sw, 2, &log);
    const BattleEvent *heal = find_event(&log, EV_ITEM_HEAL, 0);
    assert(heal && heal->target.side == 0 && heal->target.slot == 0 && heal->amount == 10 && heal->item == 472);
    assert(battle_held_item(battle_active(&b, 0)) == 472);
    // Shift (Triple): the edge Pokemon moves to the center and heals there.
    make_end_turn_items(&r, BATTLE_FORMAT_TRIPLE);
    for (unsigned i = 0; i < 3; ++i) {
        set_move(&r.player[i], 0, 150, wait_move(), 40);
        set_move(&r.opponent[i], 0, 150, wait_move(), 40);
    }
    hold(&r.player[2], I_LEFTOVERS18);
    r.player[2].record.hp = 100;
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    Action shift[6] = {{.kind = ACT_SHIFT, .user = {0, BPOS_RIGHT}}};
    unsigned count = 1;
    for (uint8_t slot = 0; slot < 2; ++slot)
        shift[count++] = (Action){.kind = ACT_MOVE, .user = {0, slot}, .target = {{0, slot}}};
    for (uint8_t slot = 0; slot < 3; ++slot)
        shift[count++] = (Action){.kind = ACT_MOVE, .user = {1, slot}, .target = {{1, slot}}};
    battle_run_turn(&b, shift, count, &log);
    heal = find_event(&log, EV_ITEM_HEAL, 0);
    assert(find_event(&log, EV_SHIFT, 0) && heal && heal->target.side == 0 && heal->target.slot == BPOS_CENTER &&
           heal->amount == 12 && event_count(&log, EV_ITEM_HEAL) == 1);
    // Rotation: only the Pokemon in front; rotating the holder to the front makes it heal.
    make_end_turn_items(&r, BATTLE_FORMAT_ROTATION);
    for (unsigned i = 0; i < 3; ++i) {
        set_move(&r.player[i], 0, 150, wait_move(), 40);
        set_move(&r.opponent[i], 0, 150, wait_move(), 40);
        hold(&r.player[i], I_LEFTOVERS18);
        r.player[i].record.hp = 100;
    }
    hold(&r.player[0], I_AMULET_COIN);  // the first Pokemon is in front (center) and holds nothing that acts
    assert(battle_init(&b, &r, 1) == BI_OK);
    battle_begin(&b, &log);
    Action rot[] = {move(0, BPOS_CENTER, BPOS_CENTER), move(1, BPOS_CENTER, BPOS_CENTER)};
    rot[0].target = (Target){{0, BPOS_CENTER}};
    rot[1].target = (Target){{1, BPOS_CENTER}};
    battle_run_turn(&b, rot, 2, &log);
    assert(!event_count(&log, EV_ITEM_HEAL));  // the holders are in the back
    rot[0].rotation = BROT_LEFT;
    battle_run_turn(&b, rot, 2, &log);
    heal = find_event(&log, EV_ITEM_HEAL, 0);
    assert(find_event(&log, EV_ROTATE, 0) && heal && heal->target.slot == BPOS_CENTER &&
           event_count(&log, EV_ITEM_HEAL) == 1 && battle_held_item(battle_at(&b, heal->target)) == 472);
}

// V18 keeps every earlier version as it was: the same Pokemon in a V17
// request (Leftovers UNSUPPORTED there, so the battle is normalised) has no
// item event, and equal speeds keep the player's Pokemon first up to V17.
static void test_v18_versions(void) {
    static BattleRequest r;
    static Battle b;
    static EventLog log;
    const EtCase tie = {"poison, equal speeds", BATTLE_FORMAT_DOUBLE,
                        {{200, 100, 50, R_NORMAL, BST_POISON}, {200, 100, 50, R_NORMAL, BST_POISON}},
                        {{200, 100, 50, R_NORMAL, BST_POISON}, {200, 100, 50, R_NORMAL, BST_POISON}}, {{0}}};
    EtEvent got[ET_MAX];
    unsigned n;
    run_et_case(&tie, BATTLE_REQUEST_VERSION_END_TURN_ITEMS, got, &n);
    assert(n == 4 && got[0].side == 0 && got[0].slot == 0 && got[1].side == 1 && got[1].slot == 0 &&
           got[2].side == 0 && got[2].slot == 1 && got[3].side == 1 && got[3].slot == 1);
    run_et_case(&tie, BATTLE_REQUEST_VERSION_ITEMS, got, &n);
    assert(n == 4 && got[0].side == 0 && got[0].slot == 0 && got[1].side == 0 && got[1].slot == 1 &&
           got[2].side == 1 && got[2].slot == 0 && got[3].side == 1 && got[3].slot == 1);
    // A gap left by a fainted Pokemon: the order is by slot, not by place in the list.
    make_end_turn_items(&r, BATTLE_FORMAT_DOUBLE);
    for (unsigned side = 0; side < 2; ++side)
        for (unsigned i = 0; i < 2; ++i) {
            BattleEntry *e = side ? &r.opponent[i] : &r.player[i];
            et_mon(e, &(EtMon){200, 100, 50, R_NORMAL, BST_POISON});
        }
    assert(battle_init(&b, &r, 1) == BI_OK);
    set_fainted(&b, 0, 0);
    Action actions[] = {move(0, 1, 1), move(1, 0, 0), move(1, 1, 1)};
    for (unsigned i = 0; i < 3; ++i) actions[i].target = (Target){{actions[i].user.side, actions[i].user.slot}};
    battle_run_turn(&b, actions, 3, &log);
    n = end_turn_events(&log, got, ET_MAX);
    assert(n >= 3 && got[0].side == 1 && got[0].slot == 0 && got[1].side == 0 && got[1].slot == 1 &&
           got[2].side == 1 && got[2].slot == 1);
}

// Random battles in the four formats with Leftovers and Black Sludge:
// deterministic, every event log fits, and a normalised battle
// (RULE_IGNORE_ITEMS) is event for event the battle without any item.
static void test_v18_formats(void) {
    static BattleRequest r, bare;
    static Battle a, b, c;
    static EventLog la, lb, lc;
    const BattleItem items[] = {I_LEFTOVERS18, I_BLACK_SLUDGE, I_CHARCOAL, I_DAMP_ROCK, I_TERRAIN_EXTENDER,
                                I_AMULET_COIN, I_LEFTOVERS18, I_BLACK_SLUDGE, {0}};
    const unsigned n = sizeof items / sizeof *items;
    const uint8_t formats[] = {BATTLE_FORMAT_SINGLE, BATTLE_FORMAT_DOUBLE, BATTLE_FORMAT_TRIPLE, BATTLE_FORMAT_ROTATION};
    unsigned battles = 0, normalised = 0, heals = 0, hurts = 0, faints = 0, most = 0;
    for (unsigned f = 0; f < 4; ++f)
        for (uint32_t seed = 1; seed <= 60; ++seed) {
            make_end_turn_items(&r, formats[f]);
            for (unsigned side = 0; side < 2; ++side) add_reserve(&r, side);
            for (unsigned side = 0; side < 2; ++side) {
                BattleEntry *e = side ? r.opponent : r.player;
                const unsigned count = side ? r.opponent_count : r.player_count;
                for (unsigned i = 0; i < count; ++i) {
                    e[i].record.max_hp = (uint16_t)(60 + (seed * 7 + i * 11 + side) % 120);
                    e[i].record.hp = (uint16_t)(e[i].record.max_hp - (seed + i) % 40);
                    e[i].mon.types[0] = (uint8_t)((seed + 3 * i + side) % (R_COUNT + 1));
                    e[i].record.speed = (uint16_t)(40 + (seed * 13 + i * 7 + side * 3) % 60);
                    e[i].record.status = (seed + i + side) % 5 == 0 ? BST_POISON : 0;
                    set_move(&e[i], 1, 240, weather_move((uint8_t)(1 + (seed + i) % 4), 5), 6);
                    set_move(&e[i], 2, 580, terrain_move((uint8_t)(1 + (seed + side) % BTERRAIN_KINDS)), 6);
                    set_move(&e[i], 3, 300 + i, ref_move(40, (uint8_t)((seed + i * 5) % R_COUNT),
                                                         (seed + i) % 2 ? BCAT_PHYSICAL : BCAT_SPECIAL), 10);
                    hold(&e[i], items[(seed * 5 + side * 3 + i + f) % n]);
                    if ((seed + i) % 4 == 0) give(&e[i], (seed + side) % 2 ? W_SAND_STREAM : T_GRASSY_SURGE);
                    if ((seed + i) % 7 == 0) give(&e[i], SHED_SKIN);
                }
            }
            bare = r;
            for (unsigned side = 0; side < 2; ++side)
                for (unsigned i = 0; i < BATTLE_MAX_PER_SIDE; ++i) {
                    BattleEntry *e = side ? &bare.opponent[i] : &bare.player[i];
                    e->mon.item = (BattleItem){0};
                    e->record.held_item = 0;
                }
            const bool ignore = seed % 3 == 0;
            if (ignore) {
                r.rules |= RULE_IGNORE_ITEMS;
                ++normalised;
            }
            assert(battle_init(&a, &r, seed) == BI_OK && battle_init(&b, &r, seed) == BI_OK &&
                   battle_init(&c, &bare, seed) == BI_OK);
            battle_begin(&a, &la);
            battle_begin(&b, &lb);
            battle_begin(&c, &lc);
            if (ignore)
                assert(la.events[0].kind == EV_ITEMS_NORMALIZED && la.count == lc.count + 1 &&
                       !memcmp(la.events + 1, lc.events, lc.count * sizeof(BattleEvent)));
            for (unsigned t = 0; t < 60 && a.outcome == OUTCOME_NONE; ++t) {
                Position pending;
                while (battle_pending_replacement(&a, &pending)) {
                    const uint8_t mask = battle_replacement_mask(&a, pending);
                    uint8_t pick = 0;
                    while (!(mask & (1u << pick))) ++pick;
                    assert(battle_replace(&a, pending, pick, &la) && battle_replace(&b, pending, pick, &lb));
                    if (ignore) assert(battle_replace(&c, pending, pick, &lc));
                }
                if (a.outcome != OUTCOME_NONE) break;
                Action actions[BC_MAX_ACTIONS];
                unsigned count = 0;
                for (uint8_t side = 0; side < 2; ++side) {
                    Position actors[BC_MAX_ACTIVE];
                    const unsigned k = battle_collect_actors(&a, side, actors);
                    for (unsigned i = 0; i < k; ++i) battle_choose_at(&a, actors[i], &actions[count++]);
                }
                for (unsigned i = 0; i < count; ++i) {
                    Action dummy;
                    battle_choose_at(&b, actions[i].user, &dummy);
                    if (ignore) battle_choose_at(&c, actions[i].user, &dummy);
                }
                battle_run_turn(&a, actions, count, &la);
                battle_run_turn(&b, actions, count, &lb);
                assert(la.count == lb.count && la.count < BC_MAX_EVENTS &&
                       memcmp(la.events, lb.events, la.count * sizeof(BattleEvent)) == 0);
                if (la.count > most) most = la.count;
                if (ignore) {
                    battle_run_turn(&c, actions, count, &lc);
                    assert(la.count == lc.count && !memcmp(la.events, lc.events, la.count * sizeof(BattleEvent)));
                }
                for (unsigned i = 0; i < la.count; ++i) {
                    const BattleEvent *e = &la.events[i];
                    if (e->kind == EV_ITEM_HEAL || e->kind == EV_ITEM_DAMAGE) {
                        // A Pokemon that fainted later in the end of the turn has left its position.
                        const Battler *x = battle_at(&a, e->target);
                        assert(!ignore && e->item && (!x || e->item == battle_held_item(x)));
                        heals += e->kind == EV_ITEM_HEAL;
                        hurts += e->kind == EV_ITEM_DAMAGE;
                        if (e->kind == EV_ITEM_DAMAGE && !e->hp_after) ++faints;
                    }
                }
            }
            // B1a never moves an item: every Pokemon still holds its own.
            for (unsigned side = 0; side < 2; ++side)
                for (unsigned i = 0; i < a.sides[side].count; ++i) {
                    const Battler *x = &a.sides[side].battlers[i];
                    assert(x->item_from == (x->entry->record.held_item ? x->entry : NULL));
                }
            assert(memcmp(&a.sides, &b.sides, sizeof a.sides) == 0 && a.rng == b.rng);
            if (ignore) assert(a.rng == c.rng && a.outcome == c.outcome);
            r.rules &= (uint16_t)~RULE_IGNORE_ITEMS;
            ++battles;
        }
    assert(normalised == 80 && heals > 50 && hurts > 20 && faints > 0);
    printf("v18 formats: %u battles (%u normalised), item heals %u, item damage %u (%u faints), most events %u\n",
           battles, normalised, heals, hurts, faints, most);
}

// M4-B1a+ (docs/items.md 11): the Frontier exporter now sends an audited
// Z-Crystal or Mega Stone of a battle without gimmicks as class NONE with
// the item's id (Snorlium Z 377, Venusaurite 292 in that build). Nothing
// changed in the core: the same binary keeps the id as the Pokemon's held
// item (not an empty hand), the item does nothing, the other items work as
// before, and a normalised battle counts it among the held items.
#define I_SNORLIUM_Z I_INERT(377)
#define I_VENUSAURITE I_INERT(292)

static void gimmick_request(BattleRequest *r, bool with_items) {
    make_end_turn_items(r, BATTLE_FORMAT_DOUBLE);
    for (unsigned side = 0; side < 2; ++side) add_reserve(r, side);
    for (unsigned side = 0; side < 2; ++side) {
        BattleEntry *e = side ? r->opponent : r->player;
        for (unsigned i = 0; i < (side ? r->opponent_count : r->player_count); ++i) {
            e[i].record.hp = (uint16_t)(e[i].record.max_hp - 7 - i);
            set_move(&e[i], 1, 300 + i, ref_move(40, (uint8_t)((side + i) % R_COUNT), BCAT_PHYSICAL), 10);
        }
    }
    hold(&r->player[0], with_items ? I_SNORLIUM_Z : (BattleItem){0});
    hold(&r->player[1], I_LEFTOVERS18);
    hold(&r->opponent[0], with_items ? I_VENUSAURITE : (BattleItem){0});
    hold(&r->opponent[1], I_BLACK_SLUDGE);
}

// Runs both battles the same way and checks every event is the same; the
// number of Leftovers / Black Sludge events (the only items that act).
static unsigned same_battles(Battle *a, EventLog *la, Battle *c, EventLog *lc) {
    unsigned items = 0;
    for (unsigned t = 0; t < 30 && a->outcome == OUTCOME_NONE; ++t) {
        Position pending;
        while (battle_pending_replacement(a, &pending)) {
            const uint8_t mask = battle_replacement_mask(a, pending);
            uint8_t pick = 0;
            while (!(mask & (1u << pick))) ++pick;
            assert(battle_replace(a, pending, pick, la) && battle_replace(c, pending, pick, lc));
        }
        if (a->outcome != OUTCOME_NONE) break;
        Action actions[BC_MAX_ACTIONS];
        unsigned count = 0;
        for (uint8_t side = 0; side < 2; ++side) {
            Position actors[BC_MAX_ACTIVE];
            const unsigned k = battle_collect_actors(a, side, actors);
            for (unsigned i = 0; i < k; ++i) battle_choose_at(a, actors[i], &actions[count++]);
        }
        for (unsigned i = 0; i < count; ++i) {
            Action dummy;
            battle_choose_at(c, actions[i].user, &dummy);
        }
        battle_run_turn(a, actions, count, la);
        battle_run_turn(c, actions, count, lc);
        assert(la->count == lc->count && !memcmp(la->events, lc->events, la->count * sizeof(BattleEvent)));
        for (unsigned i = 0; i < la->count; ++i)
            if (la->events[i].kind == EV_ITEM_HEAL || la->events[i].kind == EV_ITEM_DAMAGE) {
                assert(la->events[i].item == 472 || la->events[i].item == 487);
                ++items;
            }
    }
    assert(a->outcome == c->outcome && a->rng == c->rng);
    return items;
}

static void test_v18_gimmick_items(void) {
    static BattleRequest r, bare, d;
    static Battle a, c;
    static EventLog la, lc;
    static uint8_t bytes[4096];
    gimmick_request(&r, true);
    gimmick_request(&bare, false);
    // The wire: the id and class come back as sent; a NONE block must name the
    // record's item and carry no record (V17 rules, unchanged).
    const uint32_t n = battle_request_encode(&r, bytes, sizeof bytes);
    assert(n && battle_request_decode(bytes, n, &d));
    assert(d.player[0].record.held_item == 377 && d.player[0].mon.item.cls == BICLASS_NONE &&
           d.player[0].mon.item.source == 377 && d.player[0].mon.item.count == 0);
    assert(d.opponent[0].record.held_item == 292 && d.opponent[0].mon.item.cls == BICLASS_NONE &&
           d.opponent[0].mon.item.source == 292);
    assert(battle_item_valid(&I_SNORLIUM_Z, 377, r.type_count, BATTLE_REQUEST_VERSION_END_TURN_ITEMS));
    assert(battle_item_valid(&I_SNORLIUM_Z, 377, r.type_count, BATTLE_REQUEST_VERSION_ITEMS));
    assert(!battle_item_valid(&I_SNORLIUM_Z, 0, r.type_count, BATTLE_REQUEST_VERSION_END_TURN_ITEMS));
    assert(!battle_item_valid(&I_SNORLIUM_Z, 292, r.type_count, BATTLE_REQUEST_VERSION_END_TURN_ITEMS));
    BattleItem counted = I_SNORLIUM_Z;
    counted.count = 1;
    counted.records[0] = hp_record(BIHP_HEAL, 16, BPRED_NONE, 0);
    assert(!battle_item_valid(&counted, 377, r.type_count, BATTLE_REQUEST_VERSION_END_TURN_ITEMS));
    // Held, not an empty hand; no item of the core reads it.
    assert(battle_init(&a, &d, 11) == BI_OK && battle_init(&c, &bare, 11) == BI_OK);
    assert(battle_held_item(&a.sides[0].battlers[0]) == 377 && battle_held_item(&c.sides[0].battlers[0]) == 0);
    assert(battle_held_item(&a.sides[1].battlers[0]) == 292 && battle_held_item(&c.sides[1].battlers[0]) == 0);
    assert(bc_item(&a, &a.sides[0].battlers[0]) == NULL && bc_item(&a, &a.sides[0].battlers[1]) != NULL);
    // The battle is event for event the one with empty hands; Leftovers and
    // Black Sludge act in both.
    battle_begin(&a, &la);
    battle_begin(&c, &lc);
    assert(la.count == lc.count && !memcmp(la.events, lc.events, la.count * sizeof(BattleEvent)));
    const unsigned items = same_battles(&a, &la, &c, &lc);
    // Strict items: nothing is normalised, so the battle starts.
    d.rules |= RULE_STRICT_ITEMS;
    assert(battle_init(&a, &d, 11) == BI_OK);
    // Normalised (another item not simulated): the inert items count as held,
    // not as unsupported, and nothing acts.
    d.rules = (uint16_t)((d.rules & ~RULE_STRICT_ITEMS) | RULE_IGNORE_ITEMS);
    hold(&d.player[1], I_LIFE_ORB);
    bare.rules |= RULE_IGNORE_ITEMS;
    hold(&bare.player[1], I_LIFE_ORB);
    assert(battle_init(&a, &d, 11) == BI_OK && battle_init(&c, &bare, 11) == BI_OK);
    battle_begin(&a, &la);
    battle_begin(&c, &lc);
    assert(la.events[0].kind == EV_ITEMS_NORMALIZED && la.events[0].amount == 4 && la.events[0].battler == 1);
    assert(lc.events[0].kind == EV_ITEMS_NORMALIZED && lc.events[0].amount == 2 && lc.events[0].battler == 1);
    assert(la.count == lc.count && !memcmp(la.events + 1, lc.events + 1, (la.count - 1) * sizeof(BattleEvent)));
    // Strict and normalised: refused for the Life Orb, as before.
    d.rules |= RULE_STRICT_ITEMS;
    assert(battle_init(&a, &d, 11) == BI_ITEMS_NORMALIZED);
    // Only inert items, normalised under the strict rule: nothing is lost, so it starts.
    hold(&d.player[1], (BattleItem){0});
    hold(&d.opponent[1], (BattleItem){0});
    assert(battle_init(&a, &d, 11) == BI_OK);
    printf("v18 gimmick items: inert Z-Crystal / Mega Stone held, %u item events as with empty hands\n", items);
    assert(items > 0);
}

// ------------------------------------------------------------- V19 consumable items
// (docs/items.md section 12). The items as the Frontier exporter resolves
// them (ids of that build, diagnostic only): Oran / Sitrus Berry an HP record
// at the HP threshold, the status berries a cure record, Focus Sash an endure
// record, each used up when it acts.

static BattleItemRecord threshold_heal(uint8_t op, uint8_t amount) {
    BattleItemRecord x = {.kind = BITEM_HP, .flags = BIREC_CONSUMED};
    x.hp.timing = BITIME_HP_THRESHOLD;
    x.hp.op = op;
    x.hp.divisor = amount;
    x.hp.pred = (BattlePredicate){BPRED_HP_AT_MOST, 2};
    return x;
}
static BattleItemRecord cure_record(uint8_t status, uint8_t volatiles) {
    BattleItemRecord x = {.kind = BITEM_CURE, .flags = BIREC_CONSUMED};
    x.cure.timing = BITIME_STATUS;
    x.cure.status = status;
    x.cure.volatiles = volatiles;
    return x;
}
static BattleItemRecord endure_record(void) {
    BattleItemRecord x = {.kind = BITEM_ENDURE, .flags = BIREC_CONSUMED};
    x.endure.timing = BITIME_LETHAL_HIT;
    x.endure.condition = BIEND_FULL_HP;
    return x;
}
#define I_ORAN IHP(520, threshold_heal(BIHP_HEAL_FIXED, 10))
#define I_SITRUS IHP(523, threshold_heal(BIHP_HEAL, 4))
#define I_CHERI IHP(514, cure_record(1u << BST_PARALYSIS, 0))
#define I_CHESTO IHP(515, cure_record(1u << BST_SLEEP, 0))
#define I_PECHA IHP(516, cure_record(1u << BST_POISON | 1u << BST_TOXIC, 0))
#define I_RAWST IHP(517, cure_record(1u << BST_BURN, 0))
#define I_ASPEAR IHP(518, cure_record(1u << BST_FREEZE | 1u << BST_FROSTBITE, 0))
#define I_PERSIM IHP(521, cure_record(0, BIVOL_CONFUSION))
#define I_LUM IHP(522, cure_record(0xfe, BIVOL_CONFUSION))
#define I_SASH IHP(481, endure_record())

static void make_consumable(BattleRequest *r, uint8_t format) {
    make_end_turn_items(r, format);
    r->version = BATTLE_REQUEST_VERSION_CONSUMABLE_ITEMS;
}

static BattleMove v19_status(uint8_t kind) {
    BattleMove m = status_move(BTARGET_SINGLE, 0);
    push(&m, fx(BEFF_STATUS, BWHO_TARGET, 0, kind, 0, 0));
    return m;
}
static BattleMove v19_hit(uint16_t power) { return ref_move(power, R_NORMAL, BCAT_PHYSICAL); }
static BattleMove v19_hit_with(uint16_t power, BattleEffect e) {
    BattleMove m = v19_hit(power);
    push(&m, e);
    return m;
}
static BattleMove v19_multi(uint16_t power, uint8_t hits) {
    BattleMove m = v19_hit(power);
    m.effects[0] = fx(BEFF_DAMAGE, BWHO_TARGET, 0, hits, 0, hits);
    return m;
}
static BattleMove v19_spread(uint16_t power) {
    BattleMove m = v19_hit(power);
    m.target = BTARGET_ALL_FOES;
    return m;
}

// One Pokemon: stats, its first move (id 150) and the wait move in slot 1 (id 151).
static void v19_mon(BattleEntry *e, uint16_t max_hp, uint16_t hp, uint16_t speed, BattleItem item, BattleMove first) {
    et_mon(e, &(EtMon){max_hp, hp, speed, R_NORMAL, 0, item, R_NO_ABILITY, first});
    set_move(e, 1, 151, wait_move(), 40);
}

typedef struct { EventKind kind; uint8_t side, slot; } Ev19;
#define E19(kind, side, slot) {kind, side, slot}
static bool v19_kind(EventKind k) {
    switch (k) {
    case EV_MOVE_USED: case EV_DAMAGE: case EV_RECOIL: case EV_DRAIN: case EV_SUBSTITUTE: case EV_CONFUSION_HIT:
    case EV_STATUS: case EV_CONFUSE: case EV_ABILITY: case EV_ABILITY_DAMAGE: case EV_RESIDUAL:
    case EV_WEATHER_DAMAGE: case EV_ITEM_HEAL: case EV_ITEM_CURE: case EV_ITEM_ENDURE: case EV_ITEM_CONSUMED:
    case EV_FAINT: case EV_DRAG_OUT: case EV_WITHDRAW: case EV_SEND_OUT: case EV_STAGE:
        return true;
    default:
        return false;
    }
}

typedef struct {
    Battle b;
    EventLog log;
    Ev19 seen[512];
    BattleEvent full[512];
    unsigned n;
} V19Run;

static void v19_take(V19Run *r) {
    for (unsigned i = 0; i < r->log.count && r->n < 512; ++i) {
        const BattleEvent *e = &r->log.events[i];
        if (!v19_kind(e->kind)) continue;
        // An ability's announcement is the holder's; every other event its target's.
        const Position p = e->kind == EV_ABILITY ? e->user : e->target;
        r->full[r->n] = *e;
        r->seen[r->n++] = (Ev19){e->kind, p.side, p.slot};
    }
}
static void v19_begin(V19Run *r, const BattleRequest *req, uint32_t seed) {
    r->n = 0;
    assert(battle_init(&r->b, req, seed) == BI_OK);
    battle_begin(&r->b, &r->log);
    v19_take(r);
}
static void v19_turn(V19Run *r, const Action *a, unsigned n) {
    battle_run_turn(&r->b, a, n, &r->log);
    v19_take(r);
}
static Action v19_wait(uint8_t side, uint8_t slot) {
    Action a = slot_move(side, slot, slot, 1);
    a.target = (Target){{side, slot}};
    return a;
}

static bool v19_item_event(EventKind k) {
    return k == EV_ITEM_HEAL || k == EV_ITEM_CURE || k == EV_ITEM_ENDURE || k == EV_ITEM_CONSUMED;
}

// The expected events appear in this order; the item events of the run are
// exactly the expected ones (the reference's NOT checks).
static void v19_expect(const char *name, const V19Run *r, const Ev19 *exp, unsigned n) {
    unsigned j = 0, items = 0, expected_items = 0;
    for (unsigned i = 0; i < r->n && j < n; ++i)
        if (r->seen[i].kind == exp[j].kind && r->seen[i].side == exp[j].side && r->seen[i].slot == exp[j].slot) ++j;
    for (unsigned i = 0; i < r->n; ++i) items += v19_item_event(r->seen[i].kind);
    for (unsigned i = 0; i < n; ++i) expected_items += v19_item_event(exp[i].kind);
    if (j != n || items != expected_items) {
        printf("v19 case \"%s\": matched %u of %u, item events %u (expected %u); got", name, j, n, items, expected_items);
        for (unsigned i = 0; i < r->n; ++i) printf(" [%u %u:%u]", r->seen[i].kind, r->seen[i].side, r->seen[i].slot);
        printf("\n");
        fflush(stdout);
    }
    assert(j == n && items == expected_items);
}
#define V19_EXPECT(name, run, ...) \
    v19_expect(name, run, (Ev19[]){__VA_ARGS__}, sizeof((Ev19[]){__VA_ARGS__}) / sizeof(Ev19))
#define HEAL_USED(side, slot) E19(EV_ITEM_HEAL, side, slot), E19(EV_ITEM_CONSUMED, side, slot)
#define CURE_USED(side, slot) E19(EV_ITEM_CURE, side, slot), E19(EV_ITEM_CONSUMED, side, slot)

static const BattleEvent *v19_event(const V19Run *r, EventKind kind, unsigned nth) {
    for (unsigned i = 0; i < r->n; ++i)
        if (r->full[i].kind == kind && nth-- == 0) return &r->full[i];
    return NULL;
}

// The cases of test/battle/gc_consumable_item_reference.c (frontier-gc-m4b1b):
// the same situations, the same order of events.
static void test_v19_reference(void) {
    static BattleRequest r;
    static V19Run run;
    const Action wait2[] = {v19_wait(0, 0), v19_wait(1, 0)};

    // Sitrus: half HP or less, max HP / 4 (at least 1), once; Oran 10 up to max HP.
    static const struct { uint16_t max_hp, hp, healed; bool oran; } th[] = {
        {200, 125, 50, false}, {200, 126, 0, false}, {201, 125, 50, false}, {3, 2, 1, false},
        {30, 18, 10, true}, {16, 10, 8, true},
    };
    for (unsigned i = 0; i < sizeof th / sizeof *th; ++i) {
        make_consumable(&r, BATTLE_FORMAT_SINGLE);
        v19_mon(&r.player[0], th[i].max_hp, th[i].hp, 50, th[i].oran ? I_ORAN : I_SITRUS, wait_move());
        r.player[0].record.status = BST_POISON;
        v19_mon(&r.opponent[0], 200, 200, 40, (BattleItem){0}, wait_move());
        v19_begin(&run, &r, 1);
        v19_turn(&run, wait2, 2);
        if (th[i].healed) {
            V19_EXPECT("threshold", &run, E19(EV_RESIDUAL, 0, 0), HEAL_USED(0, 0));
            const BattleEvent *h = v19_event(&run, EV_ITEM_HEAL, 0);
            assert(h->amount == th[i].healed && h->item == (th[i].oran ? 520 : 523));
            assert(battle_held_item(&run.b.sides[0].battlers[0]) == 0);
        } else {
            V19_EXPECT("threshold, not yet", &run, E19(EV_RESIDUAL, 0, 0));
            assert(battle_held_item(&run.b.sides[0].battlers[0]) == 523);
        }
    }
    // Eaten once over three turns of poison.
    make_consumable(&r, BATTLE_FORMAT_SINGLE);
    v19_mon(&r.player[0], 200, 125, 50, I_SITRUS, wait_move());
    r.player[0].record.status = BST_POISON;
    v19_mon(&r.opponent[0], 200, 200, 40, (BattleItem){0}, wait_move());
    v19_begin(&run, &r, 1);
    for (unsigned t = 0; t < 3; ++t) v19_turn(&run, wait2, 2);
    V19_EXPECT("once", &run, E19(EV_RESIDUAL, 0, 0), HEAL_USED(0, 0), E19(EV_RESIDUAL, 0, 0), E19(EV_RESIDUAL, 0, 0));
    assert(run.b.sides[0].battlers[0].hp == 100);
    assert(run.b.sides[0].battlers[0].item_used == &r.player[0] && !run.b.sides[0].battlers[0].item_from);
    assert(run.b.sides[0].battlers[0].item_used_timing == BITIME_HP_THRESHOLD);

    // Sandstorm: the holder's damage, its berry, then the next Pokemon's damage.
    make_consumable(&r, BATTLE_FORMAT_SINGLE);
    v19_mon(&r.player[0], 160, 90, 100, I_SITRUS, wait_move());
    v19_mon(&r.opponent[0], 200, 200, 50, (BattleItem){0}, weather_move(BWEATHER_SANDSTORM, 5));
    v19_begin(&run, &r, 1);
    v19_turn(&run, (Action[]){v19_wait(0, 0), {.kind = ACT_MOVE, .user = {1, 0}, .target = {{1, 0}}}}, 2);
    V19_EXPECT("sandstorm", &run, E19(EV_WEATHER_DAMAGE, 0, 0), HEAL_USED(0, 0), E19(EV_WEATHER_DAMAGE, 1, 0));

    // Poison of the faster first, its berry, then the slower's poison and berry.
    make_consumable(&r, BATTLE_FORMAT_SINGLE);
    v19_mon(&r.player[0], 200, 125, 50, I_SITRUS, wait_move());
    v19_mon(&r.opponent[0], 200, 125, 100, I_SITRUS, wait_move());
    r.player[0].record.status = r.opponent[0].record.status = BST_POISON;
    v19_begin(&run, &r, 1);
    v19_turn(&run, wait2, 2);
    V19_EXPECT("poison order", &run, E19(EV_RESIDUAL, 1, 0), HEAL_USED(1, 0), E19(EV_RESIDUAL, 0, 0), HEAL_USED(0, 0));

    // A recoil move: target hit, recoil, the user's berry, then the target's.
    make_consumable(&r, BATTLE_FORMAT_SINGLE);
    v19_mon(&r.player[0], 40, 22, 100, I_SITRUS, v19_hit_with(90, fx(BEFF_RECOIL, BWHO_USER, 0, 25, 0, 0)));
    v19_mon(&r.opponent[0], 200, 101, 50, I_SITRUS, wait_move());
    v19_begin(&run, &r, 1);
    v19_turn(&run, (Action[]){move(0, 0, 0), v19_wait(1, 0)}, 2);
    V19_EXPECT("recoil", &run, E19(EV_DAMAGE, 1, 0), E19(EV_RECOIL, 0, 0), HEAL_USED(0, 0), HEAL_USED(1, 0));

    // A multi-hit move: the berry between the hits.
    make_consumable(&r, BATTLE_FORMAT_SINGLE);
    v19_mon(&r.player[0], 200, 200, 100, (BattleItem){0}, v19_multi(30, 2));
    v19_mon(&r.opponent[0], 200, 101, 50, I_SITRUS, wait_move());
    v19_begin(&run, &r, 1);
    v19_turn(&run, (Action[]){move(0, 0, 0), v19_wait(1, 0)}, 2);
    V19_EXPECT("multi-hit", &run, E19(EV_DAMAGE, 1, 0), HEAL_USED(1, 0), E19(EV_DAMAGE, 1, 0));

    // A spread move: both hits, then the berries in battler order.
    make_consumable(&r, BATTLE_FORMAT_DOUBLE);
    v19_mon(&r.player[0], 200, 200, 100, (BattleItem){0}, v19_spread(40));
    v19_mon(&r.player[1], 200, 200, 90, (BattleItem){0}, wait_move());
    v19_mon(&r.opponent[0], 200, 101, 50, I_SITRUS, wait_move());
    v19_mon(&r.opponent[1], 200, 101, 40, I_SITRUS, wait_move());
    v19_begin(&run, &r, 1);
    v19_turn(&run, (Action[]){move(0, 0, 0), v19_wait(0, 1), v19_wait(1, 0), v19_wait(1, 1)}, 4);
    V19_EXPECT("spread berries", &run, E19(EV_DAMAGE, 1, 0), E19(EV_DAMAGE, 1, 1), HEAL_USED(1, 0), HEAL_USED(1, 1));

    // A draining move: the drain, then the target's berry.
    make_consumable(&r, BATTLE_FORMAT_SINGLE);
    v19_mon(&r.player[0], 200, 50, 100, (BattleItem){0}, v19_hit_with(75, fx(BEFF_DRAIN, BWHO_USER, 0, 50, 0, 0)));
    v19_mon(&r.opponent[0], 200, 101, 50, I_SITRUS, wait_move());
    v19_begin(&run, &r, 1);
    v19_turn(&run, (Action[]){move(0, 0, 0), v19_wait(1, 0)}, 2);
    V19_EXPECT("drain", &run, E19(EV_DAMAGE, 1, 0), E19(EV_DRAIN, 0, 0), HEAL_USED(1, 0));

    // Substitute's HP cost, then the user's berry, before the next action.
    make_consumable(&r, BATTLE_FORMAT_SINGLE);
    v19_mon(&r.player[0], 200, 140, 100, I_SITRUS, effect_move(BTARGET_SELF, 0, fx(BEFF_SUBSTITUTE, BWHO_USER, 0, 0, 0, 0)));
    v19_mon(&r.opponent[0], 200, 200, 50, (BattleItem){0}, wait_move());
    v19_begin(&run, &r, 1);
    v19_turn(&run, (Action[]){slot_move(0, 0, 0, 0), v19_wait(1, 0)}, 2);
    V19_EXPECT("substitute", &run, E19(EV_SUBSTITUTE, 0, 0), HEAL_USED(0, 0), E19(EV_MOVE_USED, 1, 0));
    assert(run.b.sides[0].battlers[0].hp == 140);

    // Rough Skin's damage, then the attacker's berry.
    make_consumable(&r, BATTLE_FORMAT_SINGLE);
    v19_mon(&r.player[0], 160, 85, 100, I_SITRUS, contact(40, 1));
    v19_mon(&r.opponent[0], 200, 200, 50, (BattleItem){0}, wait_move());
    give(&r.opponent[0], HOOKS(24, hk(BABIL_CONTACT_DAMAGE, 0, 0, 8, 0, 0, 0, 0)));
    v19_begin(&run, &r, 1);
    v19_turn(&run, (Action[]){move(0, 0, 0), v19_wait(1, 0)}, 2);
    V19_EXPECT("rough skin", &run, E19(EV_DAMAGE, 1, 0), E19(EV_ABILITY_DAMAGE, 0, 0), HEAL_USED(0, 0));

    // A switch-in at half HP eats on entry; the berry is not back after a switch.
    make_consumable(&r, BATTLE_FORMAT_SINGLE);
    v19_mon(&r.player[0], 200, 200, 50, (BattleItem){0}, wait_move());
    add_reserve(&r, 0);
    v19_mon(&r.player[1], 200, 100, 50, I_SITRUS, wait_move());
    v19_mon(&r.opponent[0], 200, 200, 40, (BattleItem){0}, wait_move());
    v19_begin(&run, &r, 1);
    v19_turn(&run, (Action[]){{.kind = ACT_SWITCH, .user = {0, 0}, .reserve = 1}, v19_wait(1, 0)}, 2);
    V19_EXPECT("entry", &run, E19(EV_SEND_OUT, 0, 0), HEAL_USED(0, 0));
    v19_turn(&run, (Action[]){{.kind = ACT_SWITCH, .user = {0, 0}, .reserve = 0}, v19_wait(1, 0)}, 2);
    v19_turn(&run, (Action[]){{.kind = ACT_SWITCH, .user = {0, 0}, .reserve = 1}, v19_wait(1, 0)}, 2);
    V19_EXPECT("entry once", &run, E19(EV_SEND_OUT, 0, 0), HEAL_USED(0, 0), E19(EV_SEND_OUT, 0, 0), E19(EV_SEND_OUT, 0, 0));

    // Battle start: each lead's entry ability, then its berry, in speed order.
    for (unsigned fast = 0; fast < 2; ++fast) {
        make_consumable(&r, BATTLE_FORMAT_SINGLE);
        v19_mon(&r.player[0], 200, 100, fast ? 100 : 10, I_SITRUS, wait_move());
        v19_mon(&r.opponent[0], 200, 200, 50, (BattleItem){0}, wait_move());
        give(&r.opponent[0], HOOKS(22, hk(BABIL_ENTRY_STAT, BSTAT_ATK, -1, BRANGE_ADJACENT_FOES, 0, 0, 0, 0)));
        v19_begin(&run, &r, 1);
        if (fast) V19_EXPECT("start, faster holder", &run, HEAL_USED(0, 0), E19(EV_ABILITY, 1, 0));
        else V19_EXPECT("start, slower holder", &run, E19(EV_ABILITY, 1, 0), HEAL_USED(0, 0));
    }

    // Status berries: the status a move gives is cured at once.
    static const struct { uint8_t kind; uint8_t berry; } st[] = {
        {BST_PARALYSIS, 0}, {BST_PARALYSIS, 1}, {BST_SLEEP, 2}, {BST_TOXIC, 3}, {BST_BURN, 4}, {BST_BURN, 1},
    };
    for (unsigned i = 0; i < sizeof st / sizeof *st; ++i) {
        const BattleItem berries[] = {I_CHERI, I_LUM, I_CHESTO, I_PECHA, I_RAWST};
        make_consumable(&r, BATTLE_FORMAT_SINGLE);
        v19_mon(&r.player[0], 200, 200, 100, (BattleItem){0}, v19_status(st[i].kind));
        v19_mon(&r.opponent[0], 200, 200, 50, berries[st[i].berry], wait_move());
        v19_begin(&run, &r, 1);
        v19_turn(&run, (Action[]){move(0, 0, 0), v19_wait(1, 0)}, 2);
        V19_EXPECT("status cured", &run, E19(EV_STATUS, 1, 0), CURE_USED(1, 0), E19(EV_MOVE_USED, 1, 0));
        assert(!run.b.sides[1].battlers[0].status && v19_event(&run, EV_ITEM_CURE, 0)->condition == st[i].kind);
    }
    // Aspear: a freeze from a hit (a certain chance here).
    make_consumable(&r, BATTLE_FORMAT_SINGLE);
    v19_mon(&r.player[0], 200, 200, 100, (BattleItem){0}, v19_hit_with(40, fx(BEFF_STATUS, BWHO_TARGET, 100, BST_FREEZE, 0, 0)));
    v19_mon(&r.opponent[0], 200, 200, 50, I_ASPEAR, wait_move());
    v19_begin(&run, &r, 1);
    v19_turn(&run, (Action[]){move(0, 0, 0), v19_wait(1, 0)}, 2);
    V19_EXPECT("freeze", &run, E19(EV_DAMAGE, 1, 0), E19(EV_STATUS, 1, 0), CURE_USED(1, 0));
    // Synchronize: the status given back is cured first, then the target's.
    make_consumable(&r, BATTLE_FORMAT_SINGLE);
    v19_mon(&r.player[0], 200, 200, 100, I_CHERI, v19_status(BST_PARALYSIS));
    v19_mon(&r.opponent[0], 200, 200, 50, I_LUM, wait_move());
    give(&r.opponent[0], SYNCHRONIZE);
    v19_begin(&run, &r, 1);
    v19_turn(&run, (Action[]){move(0, 0, 0), v19_wait(1, 0)}, 2);
    V19_EXPECT("synchronize", &run, E19(EV_STATUS, 1, 0), E19(EV_STATUS, 0, 0), CURE_USED(0, 0), CURE_USED(1, 0));
    // Static paralyses the attacker; its berry at once, before the foe's move.
    make_consumable(&r, BATTLE_FORMAT_SINGLE);
    v19_mon(&r.player[0], 200, 200, 100, I_CHERI, contact(40, 1));
    v19_mon(&r.opponent[0], 200, 200, 50, (BattleItem){0}, wait_move());
    give(&r.opponent[0], HOOKS(9, hk(BABIL_CONTACT_STATUS, BST_PARALYSIS, 0, 0, 1, 1, 0, 0)));
    v19_begin(&run, &r, 1);
    v19_turn(&run, (Action[]){move(0, 0, 0), v19_wait(1, 0)}, 2);
    V19_EXPECT("static", &run, E19(EV_DAMAGE, 1, 0), E19(EV_STATUS, 0, 0), CURE_USED(0, 0), E19(EV_MOVE_USED, 1, 0));
    // Confusion is cured at the end of the move (Confuse Ray; Swagger after its +2).
    for (unsigned i = 0; i < 3; ++i) {
        make_consumable(&r, BATTLE_FORMAT_SINGLE);
        BattleMove m = effect_move(BTARGET_SINGLE, 0, fx(BEFF_CONFUSE, BWHO_TARGET, 0, 0, 0, 0));
        if (i == 2) {
            m = status_move(BTARGET_SINGLE, 0);
            push(&m, fx(BEFF_STAT, BWHO_TARGET, 0, BSTAT_ATK, 2, 0));
            push(&m, fx(BEFF_CONFUSE, BWHO_TARGET, 0, 0, 0, 0));
        }
        v19_mon(&r.player[0], 200, 200, 100, (BattleItem){0}, m);
        v19_mon(&r.opponent[0], 200, 200, 50, i == 0 ? I_PERSIM : I_LUM, wait_move());
        v19_begin(&run, &r, 1);
        v19_turn(&run, (Action[]){move(0, 0, 0), v19_wait(1, 0)}, 2);
        if (i == 2) V19_EXPECT("swagger", &run, E19(EV_STAGE, 1, 0), E19(EV_CONFUSE, 1, 0), CURE_USED(1, 0), E19(EV_MOVE_USED, 1, 0));
        else V19_EXPECT("confuse", &run, E19(EV_CONFUSE, 1, 0), CURE_USED(1, 0), E19(EV_MOVE_USED, 1, 0));
        assert(!run.b.sides[1].battlers[0].vol.confusion);
        assert(v19_event(&run, EV_ITEM_CURE, 0)->condition == BATTLE_CURED_CONFUSION);
    }
    // A status the Pokemon enters with.
    make_consumable(&r, BATTLE_FORMAT_SINGLE);
    v19_mon(&r.player[0], 200, 200, 50, (BattleItem){0}, wait_move());
    add_reserve(&r, 0);
    v19_mon(&r.player[1], 200, 200, 50, I_PECHA, wait_move());
    r.player[1].record.status = BST_POISON;
    v19_mon(&r.opponent[0], 200, 200, 40, (BattleItem){0}, wait_move());
    v19_begin(&run, &r, 1);
    v19_turn(&run, (Action[]){{.kind = ACT_SWITCH, .user = {0, 0}, .reserve = 1}, v19_wait(1, 0)}, 2);
    V19_EXPECT("status on entry", &run, E19(EV_SEND_OUT, 0, 0), CURE_USED(0, 0));
    // A berry used up does not cure the next status.
    make_consumable(&r, BATTLE_FORMAT_SINGLE);
    v19_mon(&r.player[0], 200, 200, 100, (BattleItem){0}, v19_status(BST_PARALYSIS));
    v19_mon(&r.opponent[0], 200, 200, 50, I_CHERI, wait_move());
    v19_begin(&run, &r, 1);
    v19_turn(&run, (Action[]){move(0, 0, 0), v19_wait(1, 0)}, 2);
    v19_turn(&run, (Action[]){move(0, 0, 0), v19_wait(1, 0)}, 2);
    V19_EXPECT("cured once", &run, E19(EV_STATUS, 1, 0), CURE_USED(1, 0), E19(EV_STATUS, 1, 0));
    assert(bc_status_kind(&run.b, &run.b.sides[1].battlers[0]) == BST_PARALYSIS);

    // Focus Sash: at full HP 1 HP is left and the sash is used up; not below full HP.
    for (unsigned below = 0; below < 2; ++below) {
        make_consumable(&r, BATTLE_FORMAT_SINGLE);
        v19_mon(&r.player[0], 200, 200, 100, (BattleItem){0}, v19_hit(250));
        v19_mon(&r.opponent[0], 20, below ? 19 : 20, 50, I_SASH, wait_move());
        add_reserve(&r, 1);
        v19_begin(&run, &r, 1);
        v19_turn(&run, (Action[]){move(0, 0, 0), v19_wait(1, 0)}, 2);
        if (below) {
            V19_EXPECT("sash below full", &run, E19(EV_DAMAGE, 1, 0), E19(EV_FAINT, 1, 0));
        } else {
            V19_EXPECT("sash", &run, E19(EV_DAMAGE, 1, 0), E19(EV_ITEM_ENDURE, 1, 0), E19(EV_ITEM_CONSUMED, 1, 0));
            assert(run.b.sides[1].battlers[0].hp == 1);
        }
    }
    // Once in a multi-hit move.
    make_consumable(&r, BATTLE_FORMAT_SINGLE);
    v19_mon(&r.player[0], 200, 200, 100, (BattleItem){0}, v19_multi(250, 2));
    v19_mon(&r.opponent[0], 20, 20, 50, I_SASH, wait_move());
    add_reserve(&r, 1);
    v19_begin(&run, &r, 1);
    v19_turn(&run, (Action[]){move(0, 0, 0), v19_wait(1, 0)}, 2);
    V19_EXPECT("sash multi-hit", &run, E19(EV_DAMAGE, 1, 0), E19(EV_ITEM_ENDURE, 1, 0), E19(EV_ITEM_CONSUMED, 1, 0),
               E19(EV_DAMAGE, 1, 0), E19(EV_FAINT, 1, 0));
    // Sturdy first; the sash is kept.
    make_consumable(&r, BATTLE_FORMAT_SINGLE);
    v19_mon(&r.player[0], 200, 200, 100, (BattleItem){0}, v19_hit(250));
    v19_mon(&r.opponent[0], 20, 20, 50, I_SASH, wait_move());
    give(&r.opponent[0], HOOKS(5, hk(BABIL_STURDY, 0, 0, 0, 0, 0, 0, 0)));
    v19_begin(&run, &r, 1);
    v19_turn(&run, (Action[]){move(0, 0, 0), v19_wait(1, 0)}, 2);
    V19_EXPECT("sturdy first", &run, E19(EV_DAMAGE, 1, 0), E19(EV_ABILITY, 1, 0));
    assert(battle_held_item(&run.b.sides[1].battlers[0]) == 481);
    // Not against poison damage.
    make_consumable(&r, BATTLE_FORMAT_SINGLE);
    v19_mon(&r.player[0], 1, 1, 50, I_SASH, wait_move());
    r.player[0].record.status = BST_POISON;
    v19_mon(&r.opponent[0], 200, 200, 40, (BattleItem){0}, wait_move());
    v19_begin(&run, &r, 1);
    v19_turn(&run, wait2, 2);
    V19_EXPECT("sash vs poison", &run, E19(EV_RESIDUAL, 0, 0), E19(EV_FAINT, 0, 0));
    // A spread move: every target is hit, then Focus Sash; then Sturdy; then status and berry.
    for (unsigned kind = 0; kind < 3; ++kind) {
        make_consumable(&r, BATTLE_FORMAT_DOUBLE);
        BattleMove m = v19_spread(kind == 2 ? 40 : 250);
        if (kind == 2) push(&m, fx(BEFF_STATUS, BWHO_TARGET, 100, BST_PARALYSIS, 0, 0));
        v19_mon(&r.player[0], 200, 200, 100, (BattleItem){0}, m);
        v19_mon(&r.player[1], 200, 200, 90, (BattleItem){0}, wait_move());
        v19_mon(&r.opponent[0], kind == 2 ? 200 : 20, kind == 2 ? 200 : 20, 50,
                kind == 0 ? I_SASH : kind == 2 ? I_CHERI : (BattleItem){0}, wait_move());
        v19_mon(&r.opponent[1], kind == 2 ? 200 : 20, kind == 2 ? 200 : 20, 40, kind == 2 ? I_CHERI : (BattleItem){0},
                wait_move());
        if (kind == 1) give(&r.opponent[0], HOOKS(5, hk(BABIL_STURDY, 0, 0, 0, 0, 0, 0, 0)));
        add_reserve(&r, 1);
        add_reserve(&r, 1);
        v19_begin(&run, &r, 1);
        v19_turn(&run, (Action[]){move(0, 0, 0), v19_wait(0, 1), v19_wait(1, 0), v19_wait(1, 1)}, 4);
        if (kind == 0)
            V19_EXPECT("spread sash", &run, E19(EV_DAMAGE, 1, 0), E19(EV_DAMAGE, 1, 1), E19(EV_ITEM_ENDURE, 1, 0),
                       E19(EV_ITEM_CONSUMED, 1, 0));
        else if (kind == 1)
            V19_EXPECT("spread sturdy", &run, E19(EV_DAMAGE, 1, 0), E19(EV_DAMAGE, 1, 1), E19(EV_ABILITY, 1, 0));
        else
            V19_EXPECT("spread status", &run, E19(EV_DAMAGE, 1, 0), E19(EV_DAMAGE, 1, 1), E19(EV_STATUS, 1, 0),
                       CURE_USED(1, 0), E19(EV_STATUS, 1, 1), CURE_USED(1, 1));
    }
    printf("v19 reference: consumable item cases passed\n");
}

// More V19 situations of the reference: the confusion self-hit (berry, Focus
// Sash), Struggle's recoil, Solar Power's sun damage (no check until the next
// move end), a turn lost to sleep (its move end checks too), a pivot move
// and Dragon Tail; and the used-up item across Shift and Rotation.
static void test_v19_more(void) {
    static BattleRequest r;
    static V19Run run;
    // A confusion self-hit, then the berry, before the next action; Focus Sash holds.
    for (unsigned sash = 0; sash < 2; ++sash) {
        make_consumable(&r, BATTLE_FORMAT_SINGLE);
        r.volatiles.confusion_hit_percent = 100;
        v19_mon(&r.player[0], sash ? 2 : 200, sash ? 2 : 105, 50, sash ? I_SASH : I_SITRUS, wait_move());
        v19_mon(&r.opponent[0], 200, 200, 100, (BattleItem){0}, effect_move(BTARGET_SINGLE, 0, fx(BEFF_CONFUSE, BWHO_TARGET, 0, 0, 0, 0)));
        r.player[0].record.level = 100;
        v19_begin(&run, &r, 1);
        v19_turn(&run, (Action[]){v19_wait(0, 0), move(1, 0, 0)}, 2);  // the slower holder hits itself
        if (sash) {
            V19_EXPECT("confusion sash", &run, E19(EV_CONFUSION_HIT, 0, 0), E19(EV_ITEM_ENDURE, 0, 0),
                       E19(EV_ITEM_CONSUMED, 0, 0));
            assert(run.b.sides[0].battlers[0].hp == 1);
        } else {
            v19_turn(&run, (Action[]){v19_wait(0, 0), v19_wait(1, 0)}, 2);
            V19_EXPECT("confusion berry", &run, E19(EV_CONFUSION_HIT, 0, 0), HEAL_USED(0, 0), E19(EV_MOVE_USED, 1, 0));
        }
    }
    // Struggle's recoil, then the user's berry.
    make_consumable(&r, BATTLE_FORMAT_SINGLE);
    v19_mon(&r.player[0], 200, 110, 100, I_SITRUS, v19_hit(40));
    r.player[0].record.pp[0] = r.player[0].record.pp[1] = 0;
    v19_mon(&r.opponent[0], 200, 200, 50, (BattleItem){0}, wait_move());
    v19_begin(&run, &r, 1);
    v19_turn(&run, (Action[]){{.kind = ACT_STRUGGLE, .user = {0, 0}, .target = {{1, 0}}}, v19_wait(1, 0)}, 2);
    V19_EXPECT("struggle", &run, E19(EV_DAMAGE, 1, 0), E19(EV_RECOIL, 0, 0), HEAL_USED(0, 0));
    // Solar Power's sun damage: no check then; the berry at the end of the
    // next move (here the faster foe's), before the holder's own move.
    make_consumable(&r, BATTLE_FORMAT_SINGLE);
    v19_mon(&r.player[0], 160, 90, 50, I_SITRUS, wait_move());
    give(&r.player[0], W_SOLAR_POWER);
    v19_mon(&r.opponent[0], 200, 200, 100, (BattleItem){0}, weather_move(BWEATHER_SUN, 5));
    v19_begin(&run, &r, 1);
    v19_turn(&run, (Action[]){v19_wait(0, 0), {.kind = ACT_MOVE, .user = {1, 0}, .target = {{1, 0}}}}, 2);
    v19_turn(&run, (Action[]){v19_wait(0, 0), v19_wait(1, 0)}, 2);
    V19_EXPECT("solar power", &run, E19(EV_ABILITY_DAMAGE, 0, 0), E19(EV_MOVE_USED, 1, 0), HEAL_USED(0, 0),
               E19(EV_MOVE_USED, 0, 0));
    // A turn lost to sleep still ends with the checks (here the pending berry).
    make_consumable(&r, BATTLE_FORMAT_SINGLE);
    v19_mon(&r.player[0], 160, 90, 50, I_SITRUS, wait_move());
    give(&r.player[0], W_SOLAR_POWER);
    v19_mon(&r.opponent[0], 200, 200, 100, (BattleItem){0}, weather_move(BWEATHER_SUN, 5));
    v19_begin(&run, &r, 1);
    v19_turn(&run, (Action[]){v19_wait(0, 0), {.kind = ACT_MOVE, .user = {1, 0}, .target = {{1, 0}}}}, 2);
    run.b.sides[1].battlers[0].status = BATTLE_CONDITION(BST_SLEEP, 3);
    run.b.sides[1].battlers[0].sleep_start = 3;
    v19_turn(&run, (Action[]){v19_wait(0, 0), v19_wait(1, 0)}, 2);
    V19_EXPECT("lost turn", &run, E19(EV_ABILITY_DAMAGE, 0, 0), HEAL_USED(0, 0), E19(EV_MOVE_USED, 0, 0));
    // A pivot move: the target's berry before the user leaves.
    make_consumable(&r, BATTLE_FORMAT_SINGLE);
    v19_mon(&r.player[0], 200, 200, 100, (BattleItem){0}, v19_hit_with(70, fx(BEFF_PIVOT, BWHO_TARGET, 0, 0, 0, 0)));
    add_reserve(&r, 0);
    v19_mon(&r.opponent[0], 200, 101, 50, I_SITRUS, wait_move());
    v19_begin(&run, &r, 1);
    v19_turn(&run, (Action[]){move(0, 0, 0), v19_wait(1, 0)}, 2);
    assert(battle_pending_pivot(&run.b, NULL));
    assert(battle_resume_pivot(&run.b, 1, &run.log));
    v19_take(&run);
    V19_EXPECT("pivot", &run, E19(EV_DAMAGE, 1, 0), HEAL_USED(1, 0), E19(EV_WITHDRAW, 0, 0));
    // Dragon Tail drags the target out before its berry.
    make_consumable(&r, BATTLE_FORMAT_SINGLE);
    v19_mon(&r.player[0], 200, 200, 100, (BattleItem){0}, v19_hit_with(60, fx(BEFF_FORCE_OUT, BWHO_TARGET, 0, 0, 0, 0)));
    v19_mon(&r.opponent[0], 200, 101, 50, I_SITRUS, wait_move());
    add_reserve(&r, 1);
    hold(&r.opponent[1], (BattleItem){0});
    r.opponent[1].record.hp = r.opponent[1].record.max_hp = 200;
    v19_begin(&run, &r, 1);
    v19_turn(&run, (Action[]){move(0, 0, 0), v19_wait(1, 0)}, 2);
    V19_EXPECT("dragon tail", &run, E19(EV_DAMAGE, 1, 0), E19(EV_DRAG_OUT, 1, 0));
    assert(battle_held_item(&run.b.sides[1].battlers[0]) == 523);
    // Shift (Triple): the berry used at the edge is not back in the center.
    make_consumable(&r, BATTLE_FORMAT_TRIPLE);
    for (unsigned i = 0; i < 3; ++i) {
        v19_mon(&r.player[i], 200, 200, 50, (BattleItem){0}, wait_move());
        v19_mon(&r.opponent[i], 200, 200, 40, (BattleItem){0}, wait_move());
    }
    v19_mon(&r.player[2], 200, 125, 50, I_SITRUS, wait_move());
    r.player[2].record.status = BST_POISON;
    v19_begin(&run, &r, 1);
    Action triple[6];
    for (uint8_t side = 0; side < 2; ++side)
        for (uint8_t slot = 0; slot < 3; ++slot) triple[side * 3 + slot] = v19_wait(side, slot);
    v19_turn(&run, triple, 6);  // poison 25 -> 100: the berry
    triple[2] = (Action){.kind = ACT_SHIFT, .user = {0, BPOS_RIGHT}};
    v19_turn(&run, triple, 6);
    triple[2] = v19_wait(0, BPOS_RIGHT);
    v19_turn(&run, triple, 6);
    unsigned heals = 0;
    for (unsigned i = 0; i < run.n; ++i) heals += run.seen[i].kind == EV_ITEM_HEAL;
    assert(heals == 1 && !battle_held_item(&run.b.sides[0].battlers[2]));
    // Rotation: the front eats; rotated away and back it holds nothing.
    make_consumable(&r, BATTLE_FORMAT_ROTATION);
    for (unsigned i = 0; i < 3; ++i) {
        v19_mon(&r.player[i], 200, 200, 50, (BattleItem){0}, wait_move());
        v19_mon(&r.opponent[i], 200, 200, 40, (BattleItem){0}, wait_move());
    }
    v19_mon(&r.player[0], 200, 125, 50, I_SITRUS, wait_move());
    r.player[0].record.status = BST_POISON;
    v19_begin(&run, &r, 1);
    Action rot[] = {v19_wait(0, BPOS_CENTER), v19_wait(1, BPOS_CENTER)};
    v19_turn(&run, rot, 2);
    rot[0].rotation = BROT_LEFT;
    v19_turn(&run, rot, 2);
    rot[0].rotation = BROT_RIGHT;
    v19_turn(&run, rot, 2);
    heals = 0;
    for (unsigned i = 0; i < run.n; ++i) heals += run.seen[i].kind == EV_ITEM_HEAL;
    assert(heals == 1 && !battle_held_item(&run.b.sides[0].battlers[0]));
    assert(run.b.sides[0].battlers[0].item_used == &r.player[0]);
    printf("v19 more: confusion, Struggle, Solar Power, lost turn, pivot, Dragon Tail, Shift, Rotation\n");
}

// The V19 records on the wire: every byte checked, V18 refuses them, V18
// requests are as before; a request built in memory gets the same check.
static void test_v19_wire(void) {
    static BattleRequest r, d;
    static Battle b;
    static uint8_t bytes[4096];
    make_consumable(&r, BATTLE_FORMAT_DOUBLE);
    hold(&r.player[0], I_SITRUS);
    hold(&r.player[1], I_LUM);
    hold(&r.opponent[0], I_SASH);
    hold(&r.opponent[1], I_ORAN);
    const uint32_t n = battle_request_encode(&r, bytes, sizeof bytes);
    assert(n && bytes[0] == BATTLE_REQUEST_VERSION_CONSUMABLE_ITEMS && battle_request_decode(bytes, n, &d));
    for (unsigned side = 0; side < 2; ++side)
        for (unsigned i = 0; i < 2; ++i) {
            const BattleItem *a = side ? &r.opponent[i].mon.item : &r.player[i].mon.item;
            const BattleItem *x = side ? &d.opponent[i].mon.item : &d.player[i].mon.item;
            assert(a->cls == x->cls && a->count == x->count && a->source == x->source &&
                   !memcmp(&a->records[0], &x->records[0], sizeof a->records[0]));
        }
    // The same blocks in a V18 request are refused; so is every bad byte in V19.
    const unsigned types = r.type_count;
    const BattleItem good[] = {I_SITRUS, I_ORAN, I_LUM, I_PERSIM, I_SASH};
    for (unsigned i = 0; i < sizeof good / sizeof *good; ++i) {
        assert(battle_item_valid(&good[i], good[i].source, types, BATTLE_REQUEST_VERSION_CONSUMABLE_ITEMS));
        assert(!battle_item_valid(&good[i], good[i].source, types, BATTLE_REQUEST_VERSION_END_TURN_ITEMS));
    }
    BattleItem bad;
#define BAD(edit) do { bad = good[0]; edit; assert(!battle_item_valid(&bad, bad.source, types, BATTLE_REQUEST_VERSION_CONSUMABLE_ITEMS)); } while (0)
#define BADC(edit) do { bad = good[2]; edit; assert(!battle_item_valid(&bad, bad.source, types, BATTLE_REQUEST_VERSION_CONSUMABLE_ITEMS)); } while (0)
#define BADE(edit) do { bad = good[4]; edit; assert(!battle_item_valid(&bad, bad.source, types, BATTLE_REQUEST_VERSION_CONSUMABLE_ITEMS)); } while (0)
    BAD(bad.records[0].hp.op = BIHP_DAMAGE);
    BAD(bad.records[0].hp.divisor = 0);
    BAD(bad.records[0].hp.pred = ((BattlePredicate){BPRED_NONE, 0}));
    BAD(bad.records[0].hp.pred.arg = 0);
    BAD(bad.records[0].hp.reserved = 1);
    BAD(bad.records[0].flags = 2);
    BAD(bad.records[0].hp.timing = BITIME_STATUS);
    BAD(bad.records[0].hp.timing = BITIME_COUNT);
    BAD((bad.records[0].hp.timing = BITIME_END_TURN, bad.records[0].hp.op = BIHP_HEAL_FIXED,
         bad.records[0].hp.pred = ((BattlePredicate){BPRED_NONE, 0})));
    BADC(bad.records[0].cure.status = 1);   // BST_NONE's bit
    BADC((bad.records[0].cure.status = 0, bad.records[0].cure.volatiles = 0));
    BADC(bad.records[0].cure.volatiles = 2);
    BADC(bad.records[0].cure.timing = BITIME_HP_THRESHOLD);
    BADC((bad.count = 2, bad.records[1] = bad.records[0]));  // two cure records
    BADE(bad.records[0].endure.condition = 0);
    BADE(bad.records[0].endure.reserved = 1);
    BADE(bad.records[0].endure.timing = BITIME_END_TURN);
    BADE((bad.count = 2, bad.records[1] = bad.records[0]));
    BADE(bad.records[0].kind = BITEM_COUNT);
    // The consumed flag is for HP, cure and endure records only, and only in V19.
    BattleItem flagged = I_LEFTOVERS18;
    flagged.records[0].flags = BIREC_CONSUMED;
    assert(battle_item_valid(&flagged, flagged.source, types, BATTLE_REQUEST_VERSION_CONSUMABLE_ITEMS));
    assert(!battle_item_valid(&flagged, flagged.source, types, BATTLE_REQUEST_VERSION_END_TURN_ITEMS));
    flagged = I_CHARCOAL;
    flagged.records[0].flags = BIREC_CONSUMED;
    assert(!battle_item_valid(&flagged, flagged.source, types, BATTLE_REQUEST_VERSION_CONSUMABLE_ITEMS));
    // On the wire, a byte the decoder carries but validation refuses stops the request.
    hold(&r.player[0], I_SASH);
    r.player[0].mon.item.records[0].endure.reserved = 7;
    assert(!battle_request_encode(&r, bytes, sizeof bytes));
    // In memory: the core refuses it too.
    assert(battle_init(&b, &r, 1) == BI_CONDITIONS);
    // V18 bytes are unchanged: a V18 request has no V19 meaning to carry.
    make_end_turn_items(&r, BATTLE_FORMAT_DOUBLE);
    hold(&r.player[0], I_LEFTOVERS18);
    const uint32_t n18 = battle_request_encode(&r, bytes, sizeof bytes);
    assert(n18 && bytes[0] == BATTLE_REQUEST_VERSION_END_TURN_ITEMS);
    printf("v19 wire: records, flags, versions\n");
}

// Counts the item events of a log; a Pokemon (by roster index) whose item
// was used up before this log gets no item event in it; the uses in the log
// are exactly the Pokemon that newly hold nothing (Battler.item_used).
typedef struct { uint8_t gone[2]; unsigned used, heals, cures, endures; } V19Uses;
static void v19_scan(const Battle *b, const EventLog *log, V19Uses *u) {
    unsigned consumed = 0;
    for (unsigned i = 0; i < log->count; ++i) {
        const BattleEvent *e = &log->events[i];
        if (e->kind != EV_ITEM_HEAL && e->kind != EV_ITEM_CURE && e->kind != EV_ITEM_ENDURE && e->kind != EV_ITEM_CONSUMED)
            continue;
        const uint8_t index = b->sides[e->target.side].active[e->target.slot];
        if (index < BATTLE_MAX_PER_SIDE) assert(!(u->gone[e->target.side] & (1u << index)));
        consumed += e->kind == EV_ITEM_CONSUMED;
        u->heals += e->kind == EV_ITEM_HEAL;
        u->cures += e->kind == EV_ITEM_CURE;
        u->endures += e->kind == EV_ITEM_ENDURE;
    }
    unsigned newly = 0;
    for (uint8_t side = 0; side < 2; ++side)
        for (uint8_t i = 0; i < b->sides[side].count; ++i) {
            const Battler *x = &b->sides[side].battlers[i];
            if (x->item_used && !(u->gone[side] & (1u << i))) {
                assert(!x->item_from && x->item_used == x->entry);
                u->gone[side] |= (uint8_t)(1u << i);
                ++newly;
            }
        }
    assert(newly == consumed);
    u->used += consumed;
}

// Random battles in the four formats with consumable items: deterministic,
// every log fits, each item used at most once and never again after, a
// normalised battle is event for event the battle without items, and V18
// requests of the same Pokemon (items unsupported there: normalised) have
// none of it.
static void test_v19_formats(void) {
    static BattleRequest r, bare;
    static Battle a, c, bb;
    static EventLog la, lb, lc;
    const BattleItem items[] = {I_SITRUS, I_ORAN, I_LUM, I_CHERI, I_PECHA, I_SASH, I_PERSIM, I_LEFTOVERS18,
                                I_BLACK_SLUDGE, I_CHESTO, I_RAWST, {0}};
    const unsigned n = sizeof items / sizeof *items;
    const uint8_t formats[] = {BATTLE_FORMAT_SINGLE, BATTLE_FORMAT_DOUBLE, BATTLE_FORMAT_TRIPLE, BATTLE_FORMAT_ROTATION};
    unsigned battles = 0, normalised = 0, used = 0, heals = 0, cures = 0, endures = 0, most = 0;
    V19Uses u;
    for (unsigned f = 0; f < 4; ++f)
        for (uint32_t seed = 1; seed <= 60; ++seed) {
            make_consumable(&r, formats[f]);
            for (unsigned side = 0; side < 2; ++side) add_reserve(&r, side);
            for (unsigned side = 0; side < 2; ++side) {
                BattleEntry *e = side ? r.opponent : r.player;
                const unsigned count = side ? r.opponent_count : r.player_count;
                for (unsigned i = 0; i < count; ++i) {
                    e[i].record.max_hp = (uint16_t)(40 + (seed * 7 + i * 11 + side) % 120);
                    e[i].record.hp = (uint16_t)(e[i].record.max_hp - (seed + i) % 30);
                    e[i].mon.types[0] = (uint8_t)((seed + 3 * i + side) % (R_COUNT + 1));
                    e[i].record.speed = (uint16_t)(40 + (seed * 13 + i * 7 + side * 3) % 60);
                    e[i].record.status = (seed + i + side) % 6 == 0 ? BST_POISON : 0;
                    set_move(&e[i], 0, 300 + i, ref_move(60 + (seed % 5) * 20, (uint8_t)((seed + i) % R_COUNT),
                                                         BCAT_PHYSICAL), 10);
                    set_move(&e[i], 1, 240, v19_status((uint8_t)(1 + (seed + i) % 5)), 6);
                    BattleMove spread = v19_spread(50);
                    if ((seed + i) % 3 == 0) push(&spread, fx(BEFF_STATUS, BWHO_TARGET, 30, BST_PARALYSIS, 0, 0));
                    set_move(&e[i], 2, 250, spread, 6);
                    set_move(&e[i], 3, 260, (seed + i) % 2 ? v19_multi(25, 2) :
                             effect_move(BTARGET_SINGLE, 0, fx(BEFF_CONFUSE, BWHO_TARGET, 0, 0, 0, 0)), 6);
                    hold(&e[i], items[(seed * 5 + side * 3 + i + f) % n]);
                    if (e[i].mon.item.source == 481) e[i].record.hp = e[i].record.max_hp;  // Focus Sash at full HP
                    if ((seed + i) % 5 == 0) give(&e[i], SYNCHRONIZE);
                    if ((seed + i) % 7 == 0) give(&e[i], HOOKS(5, hk(BABIL_STURDY, 0, 0, 0, 0, 0, 0, 0)));
                }
            }
            bare = r;
            for (unsigned side = 0; side < 2; ++side)
                for (unsigned i = 0; i < BATTLE_MAX_PER_SIDE; ++i) {
                    BattleEntry *e = side ? &bare.opponent[i] : &bare.player[i];
                    e->mon.item = (BattleItem){0};
                    e->record.held_item = 0;
                }
            const bool ignore = seed % 3 == 0;
            if (ignore) {
                r.rules |= RULE_IGNORE_ITEMS;
                ++normalised;
            }
            assert(battle_init(&a, &r, seed) == BI_OK && battle_init(&bb, &r, seed) == BI_OK &&
                   battle_init(&c, &bare, seed) == BI_OK);
            battle_begin(&a, &la);
            battle_begin(&bb, &lb);
            battle_begin(&c, &lc);
            if (ignore)
                assert(la.events[0].kind == EV_ITEMS_NORMALIZED && la.count == lc.count + 1 &&
                       !memcmp(la.events + 1, lc.events, lc.count * sizeof(BattleEvent)));
            u = (V19Uses){{0}};
            v19_scan(&a, &la, &u);
            for (unsigned t = 0; t < 40 && a.outcome == OUTCOME_NONE; ++t) {
                Position pending;
                while (battle_pending_replacement(&a, &pending)) {
                    const uint8_t mask = battle_replacement_mask(&a, pending);
                    uint8_t pick = 0;
                    while (!(mask & (1u << pick))) ++pick;
                    assert(battle_replace(&a, pending, pick, &la) && battle_replace(&bb, pending, pick, &lb));
                    if (ignore) assert(battle_replace(&c, pending, pick, &lc));
                    assert(la.count == lb.count && !memcmp(la.events, lb.events, la.count * sizeof(BattleEvent)));
                    v19_scan(&a, &la, &u);
                }
                if (a.outcome != OUTCOME_NONE) break;
                Action actions[BC_MAX_ACTIONS];
                unsigned count = 0;
                for (uint8_t side = 0; side < 2; ++side) {
                    Position actors[BC_MAX_ACTIVE];
                    const unsigned k = battle_collect_actors(&a, side, actors);
                    for (unsigned i = 0; i < k; ++i) battle_choose_at(&a, actors[i], &actions[count++]);
                }
                for (unsigned i = 0; i < count; ++i) {
                    Action dummy;
                    battle_choose_at(&bb, actions[i].user, &dummy);
                    if (ignore) battle_choose_at(&c, actions[i].user, &dummy);
                }
                battle_run_turn(&a, actions, count, &la);
                battle_run_turn(&bb, actions, count, &lb);
                assert(la.count == lb.count && la.count < BC_MAX_EVENTS &&
                       !memcmp(la.events, lb.events, la.count * sizeof(BattleEvent)));
                if (la.count > most) most = la.count;
                if (ignore) {
                    battle_run_turn(&c, actions, count, &lc);
                    assert(la.count == lc.count && !memcmp(la.events, lc.events, la.count * sizeof(BattleEvent)));
                }
                if (ignore) assert(!u.used);
                v19_scan(&a, &la, &u);
            }
            for (unsigned side = 0; side < 2; ++side)
                for (unsigned i = 0; i < a.sides[side].count; ++i) {
                    const Battler *x = &a.sides[side].battlers[i];
                    if (u.gone[side] & (1u << i)) assert(!x->item_from && !battle_held_item(x) && x->item_used);
                    else assert(x->item_from == (x->entry->record.held_item ? x->entry : NULL));
                }
            assert(!memcmp(&a.sides, &bb.sides, sizeof a.sides) && a.rng == bb.rng);
            if (ignore) assert(a.rng == c.rng && a.outcome == c.outcome);
            r.rules &= (uint16_t)~RULE_IGNORE_ITEMS;
            used += u.used;
            heals += u.heals;
            cures += u.cures;
            endures += u.endures;
            ++battles;
        }
    printf("v19 formats: %u battles (%u normalised), items used %u (heals %u, cures %u, endures %u), most events %u\n",
           battles, normalised, used, heals, cures, endures, most);
    fflush(stdout);
    assert(normalised == 80 && used > 50 && cures > 10 && endures > 0 && heals > 20);
}
#pragma GCC diagnostic pop

int main(void) {
    test_request_roundtrip();
    test_messages();
    test_core();
    test_multi_wire();
    test_double();
    test_triple();
    test_rotation();
    test_roster_wire();
    test_single_replacement();
    test_double_replacements();
    test_triple_center();
    test_rotation_replacement();
    test_voluntary_switch();
    test_switch_wire();
    test_struggle_wire();
    test_struggle();
    test_drag();
    test_pivot();
    test_pivot_wire();
    test_condition_wire();
    test_stat_stages();
    test_major_status();
    test_condition_application();
    test_residual_lifecycle();
    test_pivot_then_residual();
    test_switch_rotation_conditions();
    test_effects_wire();
    test_effect_status_and_stats();
    test_effect_heal_drain_recoil();
    test_effect_secondary();
    test_effect_multi_hit();
    test_effect_priority_protect();
    test_effect_formats();
    test_effect_lifecycle();
    test_volatile_wire();
    test_flinch();
    test_confusion();
    test_substitute();
    test_charge();
    test_recharge();
    test_volatile_lifecycle();
    test_effect_timing_spread_self();
    test_effect_timing_units();
    test_struggle_shared_checks();
    test_refused_turn_keeps_state();
    test_auto_target_normalized();
    test_state_decode_defaults();
    test_version_schema();
    test_ability_wire();
    test_ability_damage();
    test_ability_immunity();
    test_ability_accuracy();
    test_ability_sturdy();
    test_ability_contact();
    test_ability_effect_level();
    test_ability_entry();
    test_ability_end_turn();
    test_ability_unsupported();
    test_ability_formats();
    test_v12_wire();
    test_triple_range();
    test_guts_marvel_adaptability();
    test_moxie();
    test_shed_skin();
    test_flash_fire();
    test_scrappy();
    test_synchronize();
    test_strict_mode();
    test_v12_determinism();
    test_v13_validation();
    test_v13_wire();
    test_v13_largest_request();
    test_v13_hex_and_custom();
    test_v13_shared_record();
    test_v13_order_rounding();
    test_v13_expresses_v12();
    test_v13_no_rng();
    test_v13_determinism();
    test_v14_arithmetic();
    test_v14_battle();
    test_v14_order();
    test_v14_union_decode();
    test_v14_expansion_reference();
    test_v15_wire();
    test_v15_set_and_end();
    test_v15_entry();
    test_v15_suppress();
    test_v15_damage();
    test_v15_accuracy();
    test_v15_speed_order();
    test_v15_residual();
    test_v15_moves();
    test_v15_no_freeze();
    test_v15_strict_moves();
    test_v15_formats();
    test_v15_expansion_reference();
    test_v16_wire();
    test_v16_set_and_end();
    test_v16_entry();
    test_v16_grounded();
    test_v16_status();
    test_v16_priority();
    test_v16_damage();
    test_v16_speed();
    test_v16_order();
    test_v16_formats();
    test_v16_expansion_reference();
    test_v17_expansion_reference();
    test_v17_wire();
    test_v17_init();
    test_v17_power();
    test_v17_field_turns();
    test_v17_formats();
    test_v18_wire();
    test_v18_init();
    test_v18_end_turn_reference();
    test_v18_state();
    test_v18_positions();
    test_v18_versions();
    test_v18_formats();
    test_v18_gimmick_items();
    test_v19_reference();
    test_v19_more();
    test_v19_wire();
    test_v19_formats();
    puts("battle tests passed");
    return 0;
}
