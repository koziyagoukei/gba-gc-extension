// Desktop tests for the battle protocol codecs and the battle core.
#include "battle_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { T_NORMAL = 0, T_FIRE = 1, T_WATER = 2, T_GHOST = 3, NTYPES = 4 };

static BattleMove hit(uint16_t power, uint8_t type, uint8_t category) {
    return (BattleMove){power, type, category, 100, 0, BTARGET_SINGLE, 0};
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
    buffer[0] = BATTLE_REQUEST_VERSION_STRUGGLE + 1;
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
    puts("battle tests passed");
    return 0;
}
