// Desktop tests for the battle protocol codecs and the battle core.
#include "battle_core.h"
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
    puts("battle tests passed");
    return 0;
}
