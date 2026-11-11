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
    const uint32_t size = battle_request_encode(&in, bytes, sizeof(bytes));
    assert(size == BATTLE_REQUEST_HEADER + 2 * BATTLE_ENTRY_BYTES + NTYPES * NTYPES);
    assert(battle_request_encode(&in, bytes, size - 1) == 0);
    assert(battle_request_decode(bytes, size, &out));
    assert(out.format == BATTLE_FORMAT_SINGLE && out.trainer_id == 42 && out.battle_id == 0x1234);
    assert(out.player[0].record.species == 7 && out.opponent[0].record.slot == 0x80);
    assert(out.player[0].mon.moves[1].unsupported == UNSUP_SECONDARY && out.player[0].mon.moves[0].power == 40);
    assert(out.type_chart[T_WATER * NTYPES + T_FIRE] == 8);
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
    assert(!battle_move_supported(&r, &spread));
    r.format = BATTLE_FORMAT_SINGLE;
    Action bad = {ACT_MOVE, {0, 0}, 1, {{1, 0}}};
    assert(!battle_validate_action(&b, &bad));
    Action acts[2] = {{ACT_MOVE, {0, 0}, 0, {{1, 0}}}, {ACT_NONE, {1, 0}, 0, {{0, 0}}}};
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
    Action forfeit[1] = {{ACT_FORFEIT, {0, 0}, 0, {{1, 0}}}};
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

int main(void) {
    test_request_roundtrip();
    test_messages();
    test_core();
    puts("battle tests passed");
    return 0;
}
