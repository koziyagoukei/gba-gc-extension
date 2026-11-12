// GC_MODE_BATTLE orchestration: link, core, and presentation lifecycle.
#include "battle_view.h"
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include <string.h>
#include "../battle/battle_core.h"
#include "battle_protocol.h"
#include "../joybus.h"
#include "cache.h"
#include "render.h"
#include "../presentation/assets.h"
#include "../presentation/scene.h"

#define MAX_PLAN (2 * BATTLE_MAX_PER_SIDE * (1 + 1 + 4))
#define WHITE RGBA(242, 249, 255, 255)
#define DIM RGBA(165, 191, 208, 255)

enum BState { B_REQUEST, B_ASSETS, B_START, B_INTRO, B_CHOOSE, B_REPLACE, B_PLAY, B_RESULT, B_WAIT_EXIT };
static const char *const bstate_names[] = {"REQUEST", "ASSETS", "START", "INTRO", "CHOOSE", "REPLACE", "PLAY", "RESULT",
                                           "WAIT_EXIT"};

static struct {
    HostLink *link;
    ModeInfo mode;
    enum BState state;
    BattleRequest request;
    Battle battle;
    int init_error;
    AssetKey plan[MAX_PLAN];
    unsigned plan_count, plan_index;
    bool fetching, state_sent;
    EventLog log;
    Position players[BC_MAX_ACTIVE];
    Position replacement;
    unsigned player_commands, choosing;
    Action actions[BC_MAX_ACTIONS];
    uint16_t token;
    uint8_t reserved;
    uint8_t outcome;
    uint16_t result_status;
    uint64_t started, turn_started, last_play_poll;
    uint32_t request_us, assets_us, asset_bytes, wire_bytes;
    uint32_t peak_core_us, peak_events, peak_play_gap_us;
    const char *status;
    bool digest_logged;
} b;
static AssetFetch fetch;

static uint32_t us_since(uint64_t t) { return (uint32_t)ticks_to_microsecs(gettime() - t); }
static uint32_t build_id(void) { return b.link->client.build_id; }
static Battler *battler(uint8_t side) { return battle_active(&b.battle, side); }
static bool roster(void) { return b.mode.app_version >= BATTLE_APP_VERSION_ROSTER; }
static bool multi(void) { return roster() || b.request.format != BATTLE_FORMAT_SINGLE; }

static void set_bstate(enum BState s) {
    SYS_Report("PH3 BATTLE %s -> %s\n", bstate_names[b.state], bstate_names[s]);
    b.state = s;
}

// ------------------------------------------------------------------ link

static bool pkmn_app(uint16_t op, const uint8_t *args, unsigned n) {
    uint16_t status;
    return host_app_to(b.link, APP_POKEMON, op, args, n, NULL, NULL, &status) && status == APP_OK;
}

static bool set_phase(uint8_t phase, unsigned done, unsigned total) {
    const uint8_t args[3] = {phase, (uint8_t)done, (uint8_t)total};
    return pkmn_app(PKMN_SET_PHASE, args, 3);
}

static bool log_digest(const char *when) {
    const uint8_t *d;
    unsigned n;
    uint16_t status;
    if (!host_app_to(b.link, APP_POKEMON, PKMN_STATE_DIGEST, NULL, 0, &d, &n, &status) || status != APP_OK ||
        n < STATE_DIGEST_BYTES)
        return false;
    SYS_Report("PH3 DIGEST %s party=%08x bag=%08x flags=%08x vars=%08x sb1=%08x match=%02x%s\n", when,
               wire_get32(d), wire_get32(d + 4), wire_get32(d + 8), wire_get32(d + 12), wire_get32(d + 16), d[20],
               d[20] == STATE_DIGEST_ALL_MATCH ? " UNCHANGED" : " CHANGED");
    return true;
}

static bool send_multi_state(uint8_t phase) {
    BattleRosterState extended = {.field_state = {
        .phase = phase,
        .turn = (uint8_t)(b.battle.turn + (phase == BPHASE_CHOOSE)),
        .outcome = b.battle.outcome ? b.battle.outcome : b.outcome,
        .token = b.token,
    }};
    BattleMultiState *state = &extended.field_state;
    if (phase == BPHASE_CHOOSE) {
        const Position user = b.players[b.choosing];
        state->actor = user.slot;
        state->flags = battle_can_shift(&b.battle, user) ? BATTLE_MULTI_SHIFT : 0;
        extended.reserves = battle_switch_mask(&b.battle, user) & ~b.reserved;
    } else if (phase == BPHASE_REPLACE) {
        state->actor = b.replacement.slot;
        extended.reserves = battle_replacement_mask(&b.battle, b.replacement);
    }
    for (uint8_t side = 0; side < 2; ++side)
        for (uint8_t slot = 0; slot < BC_MAX_ACTIVE; ++slot) {
            const Position pos = {side, slot};
            const Battler *x = b.init_error == BI_OK ? battle_at(&b.battle, pos) : NULL;
            BattleFieldMon *m = &state->field[side][slot];
            m->party_slot = x ? x->party_slot : BATTLE_SLOT_NONE;
            if (!x) continue;
            m->hp = x->hp;
            m->max_hp = x->max_hp;
            if (side != 0) continue;
            memcpy(state->pp[slot], x->pp, sizeof(state->pp[slot]));
            for (unsigned move = 0; move < 4; ++move)
                state->targets[slot][move] = battle_command_targets(&b.battle, pos, move);
        }
    uint8_t bytes[BATTLE_ROSTER_STATE_BYTES];
    uint16_t status;
    unsigned size;
    const uint16_t opcode = roster() ? BATTLE_OP_STATE_ROSTER : BATTLE_OP_STATE_MULTI;
    if (roster()) {
        for (unsigned i = 0; i < BATTLE_MAX_PER_SIDE; ++i) {
            const PokemonRecord *record = i < b.request.player_count ? &b.request.player[i].record : NULL;
            const Battler *mon = record && b.init_error == BI_OK ? &b.battle.sides[0].battlers[i] : NULL;
            extended.player[i] = (BattleFieldMon){record ? record->slot : BATTLE_SLOT_NONE,
                                                  mon ? mon->hp : record ? record->hp : 0,
                                                  record ? record->max_hp : 0};
        }
        size = battle_roster_state_encode(&extended, bytes);
    } else {
        size = battle_multi_state_encode(state, bytes);
    }
    return host_app_to(b.link, b.mode.app_id, opcode, bytes, size, NULL, NULL, &status) && status == APP_OK;
}

static bool send_state(uint8_t phase) {
    if (multi()) return send_multi_state(phase);
    BattleState s = {0};
    uint8_t bytes[BATTLE_STATE_BYTES];
    uint16_t status;
    const Battler *p = battler(0), *o = battler(1);
    s.phase = phase;
    s.turn = (uint8_t)(b.battle.turn + (phase == BPHASE_CHOOSE ? 1 : 0));
    s.outcome = b.battle.outcome ? b.battle.outcome : b.outcome;
    const PokemonRecord *initial_player = &b.request.player[0].record;
    const PokemonRecord *initial_foe = &b.request.opponent[0].record;
    s.player_slot = initial_player->slot;
    s.player_hp = p ? p->hp : initial_player->hp;
    s.player_max_hp = initial_player->max_hp;
    s.opponent_hp = o ? o->hp : initial_foe->hp;
    s.opponent_max_hp = initial_foe->max_hp;
    memcpy(s.pp, p ? p->pp : initial_player->pp, 4);
    battle_state_encode(&s, bytes);
    return host_app_to(b.link, b.mode.app_id, BATTLE_OP_STATE, bytes, sizeof(bytes), NULL, NULL, &status) &&
           status == APP_OK;
}

static bool poll_action(BattleAction *a) {
    const uint8_t *d;
    unsigned n;
    uint16_t status;
    if (!host_app_to(b.link, b.mode.app_id, BATTLE_OP_ACTION, NULL, 0, &d, &n, &status)) {
        SYS_Report("PH3 ACTION poll: request failed (seq=%u si=%u)\n", b.link->session.seq, joy_errors());
        return false;
    }
    if (status != APP_OK || !battle_action_decode(d, n, a)) {
        SYS_Report("PH3 ACTION poll: status=%u length=%u\n", status, n);
        return false;
    }
    return true;
}

static bool poll_multi_action(BattleMultiAction *a) {
    const uint8_t *d;
    unsigned n;
    uint16_t status;
    const uint16_t opcode = roster() ? BATTLE_OP_ACTION_ROSTER : BATTLE_OP_ACTION_MULTI;
    if (!host_app_to(b.link, b.mode.app_id, opcode, NULL, 0, &d, &n, &status)) return false;
    if (status == APP_OK) {
        switch (b.mode.app_version) {
        case BATTLE_APP_VERSION_SWITCH: return battle_switch_action_decode(d, n, a);
        case BATTLE_APP_VERSION_ROSTER: return battle_roster_action_decode(d, n, a);
        default: return battle_multi_action_decode(d, n, a);
        }
    }
    SYS_Report("MULTI ACTION poll: status=%u length=%u\n", status, n);
    return false;
}

// Keep SI traffic flowing during each presentation frame.
static bool keepalive(bool *exit) {
    BattleAction a;
    BattleMultiAction m;
    if (multi() ? !poll_multi_action(&m) : !poll_action(&a)) return false;
    if (exit) *exit = ((multi() ? m.flags : a.flags) & BACTION_FLAG_EXIT) != 0;
    return true;
}

// ------------------------------------------------------------------ assets

static void plan_add(AssetKey key) {
    for (unsigned i = 0; i < b.plan_count; ++i)
        if (asset_key_equal(&b.plan[i], &key)) return;
    if (b.plan_count < MAX_PLAN) b.plan[b.plan_count++] = key;
}

static void build_plan(void) {
    b.plan_count = b.plan_index = 0;
    for (unsigned side = 0; side < 2; ++side) {
        const BattleEntry *entries = side ? b.request.opponent : b.request.player;
        const unsigned count = side ? b.request.opponent_count : b.request.player_count;
        for (unsigned i = 0; i < count; ++i) {
            const PokemonRecord *record = &entries[i].record;
            plan_add(battle_sprite_key(record, (Position){side, i}));
            plan_add(pkmn_nickname_label(record));
            for (unsigned m = 0; m < 4; ++m)
                if (record->moves[m]) plan_add(battle_move_label_key(record->moves[m]));
        }
    }
}

// One fetch step over a plan; returns 1 when the plan is complete, -1 on link loss.
static int fetch_plan(void) {
    while (!b.fetching && b.plan_index < b.plan_count) {
        if (cache_find(build_id(), &b.plan[b.plan_index])) { cache_count_hit(); ++b.plan_index; continue; }
        fetch_start(&fetch, &b.plan[b.plan_index]);
        b.fetching = true;
    }
    if (!b.fetching) return 1;
    switch (fetch_step(b.link, &fetch)) {
    case FETCH_PENDING: return 0;
    case FETCH_DONE:
        if (!cache_insert(build_id(), &fetch.info, fetch.data))
            SYS_Report("PRESENTATION ASSET %04x:%08x not cached (format %u)\n", fetch.key.type, fetch.key.id, fetch.info.pixel_format);
        b.asset_bytes += fetch.info.length;
        b.wire_bytes += fetch.wire_length;
        SYS_Report("PRESENTATION ASSET %04x:%08x:%08x %ux%u raw=%u wire=%u%s us=%u\n", fetch.key.type, fetch.key.id,
                   fetch.key.variant, fetch.info.width, fetch.info.height, fetch.info.length, fetch.wire_length,
                   fetch.info.encoding == ASSET_ENCODING_RLE ? " rle" : "", fetch_elapsed_us(&fetch));
        break;
    case FETCH_MISSING:
        SYS_Report("PRESENTATION ASSET %04x:%08x missing (status %u)\n", fetch.key.type, fetch.key.id, fetch.info.status);
        break;
    case FETCH_FAILED:
        return -1;
    }
    b.fetching = false;
    ++b.plan_index;
    return 0;
}

// ------------------------------------------------------------------ flow

void battle_view_begin(HostLink *link, const ModeInfo *mode) {
    memset(&b, 0, sizeof(b));
    b.link = link;
    b.mode = *mode;
    b.status = "Receiving battle request";
    b.started = gettime();
    battle_scene_reset();
    const AssetKey key = {ASSET_MODE_REQUEST, mode->mode_id, 0, mode->nonce};
    fetch_start(&fetch, &key);
    b.state = B_REQUEST;
    SYS_Report("PH3 BATTLE begin request=%u bytes\n", mode->request_length);
}

static void log_entry(const char *side, const BattleEntry *e) {
    const PokemonRecord *r = &e->record;
    SYS_Report("PH3 %s slot=%u species=%u lv=%u hp=%u/%u types=%u/%u\n", side, r->slot, r->species,
               r->level, r->hp, r->max_hp, e->mon.types[0], e->mon.type_count > 1 ? e->mon.types[1] : e->mon.types[0]);
    for (unsigned m = 0; m < 4; ++m)
        if (r->moves[m])
            SYS_Report("PH3   move%u id=%u power=%u type=%u cat=%u acc=%u target=%u pp=%u/%u %s(%02x)\n", m,
                       r->moves[m], e->mon.moves[m].power, e->mon.moves[m].type, e->mon.moves[m].category,
                       e->mon.moves[m].accuracy, e->mon.moves[m].target, r->pp[m], r->max_pp[m],
                       battle_move_supported(&b.request, &e->mon.moves[m]) ? "supported" : "UNSUPPORTED",
                       e->mon.moves[m].unsupported);
}

static BattleViewStatus step_result(void) {
    BattleResult r = {0};
    uint8_t bytes[RESULT_MAX_PAYLOAD];
    r.outcome = b.outcome;
    r.turns = b.battle.turn;
    r.player_count = b.request.player_count;
    r.opponent_count = b.request.opponent_count;
    r.battle_id = b.request.battle_id;
    for (unsigned i = 0; i < r.player_count; ++i) {
        const PokemonRecord *entry = &b.request.player[i].record;
        const Battler *x = b.init_error == BI_OK ? &b.battle.sides[0].battlers[i] : NULL;
        r.player[i].slot = entry->slot;
        r.player[i].hp = x ? x->hp : entry->hp;
        r.player[i].flags = r.player[i].hp ? 0 : BATTLE_RESULT_FAINTED;
        r.player[i].status = x ? x->status : entry->status;
        memcpy(r.player[i].pp, x ? x->pp : entry->pp, 4);
    }
    for (unsigned i = 0; i < r.opponent_count; ++i)
        r.opponent_hp[i] = b.init_error == BI_OK ? b.battle.sides[1].battlers[i].hp : b.request.opponent[i].record.hp;
    const unsigned n = battle_result_encode(&r, bytes);
    if (!send_state(BPHASE_ENDED)) return BV_LOST;
    if (!host_result(b.link, GC_MODE_BATTLE, BATTLE_RESULT_VERSION, r.battle_id, bytes, n, &b.result_status))
        return BV_LOST;
    SYS_Report("PH3 RESULT outcome=%u turns=%u player_hp=%u/%u opponent_hp=%u/%u pp=%u,%u,%u,%u battle=%08x status=%u total_us=%u format=%u\n",
               r.outcome, r.turns, r.player[0].hp, b.request.player[0].record.max_hp, r.opponent_hp[0],
               b.request.opponent[0].record.max_hp, r.player[0].pp[0], r.player[0].pp[1], r.player[0].pp[2], r.player[0].pp[3],
               r.battle_id, b.result_status, us_since(b.started), b.request.format);
    if (multi())
        for (unsigned i = 0; i < r.player_count; ++i)
            SYS_Report("MULTI RESULT entry=%u party=%u hp=%u foe_hp=%u pp=%u,%u,%u,%u\n", i, r.player[i].slot,
                       r.player[i].hp, r.opponent_hp[i], r.player[i].pp[0], r.player[i].pp[1], r.player[i].pp[2], r.player[i].pp[3]);
    SYS_Report("BATTLE PERF core_peak_us=%u event_peak=%u play_poll_gap_peak_us=%u\n",
               b.peak_core_us, b.peak_events, b.peak_play_gap_us);
    set_bstate(B_WAIT_EXIT);
    return BV_RUNNING;
}

static void resolve_turn(const Action *actions, unsigned count) {
    const uint64_t start = gettime();
    battle_run_turn(&b.battle, actions, count, &b.log);
    const uint32_t elapsed = us_since(start);
    if (elapsed > b.peak_core_us) b.peak_core_us = elapsed;
}

static BattleViewStatus play_turn(unsigned turn) {
    if (b.log.count > b.peak_events) b.peak_events = b.log.count;
    b.last_play_poll = 0;
    for (unsigned i = 0; i < b.log.count; ++i) {
        const BattleEvent *e = &b.log.events[i];
        SYS_Report("PH3 EVENT turn=%u kind=%u user=%u target=%u move=%u amount=%u hp_after=%u eff=%u outcome=%u pos=%u:%u rotation=%u battler=%u\n",
                   turn, e->kind, e->user.side, e->target.side, e->move_id, e->amount, e->hp_after, e->effectiveness,
                   e->outcome, e->user.slot, e->target.slot, e->rotation, e->battler);
    }
    if (!send_state(BPHASE_WAIT)) return BV_LOST;
    battle_scene_play(&b.log);
    set_bstate(B_PLAY);
    return BV_RUNNING;
}

static BattleViewStatus step_choose(void) {
    BattleAction a;
    if (!b.state_sent) {
        if (!send_state(BPHASE_CHOOSE)) return BV_LOST;
        b.state_sent = true;
        b.turn_started = gettime();
        battle_scene_choose((uint8_t)(b.battle.turn + 1), (Position){0, 0});
        battle_scene_show_target((Position){1, 0});
    }
    if (!poll_action(&a)) return BV_LOST;
    const uint8_t turn = (uint8_t)(b.battle.turn + 1);
    if ((a.flags & BACTION_FLAG_EXIT) || (a.action == BACTION_FORFEIT && a.turn == turn)) {
        SYS_Report("PH3 ACTION turn=%u forfeit\n", turn);
        Action f = {.kind = ACT_FORFEIT, .user = {0, 0}};
        resolve_turn(&f, 1);
    } else if (a.action == BACTION_MOVE && a.turn == turn) {
        Action acts[2] = {{.kind = ACT_MOVE, .user = {0, 0}, .move_slot = a.move_slot, .target = {{1, 0}}}, {0}};
        if (!battle_validate_action(&b.battle, &acts[0])) {
            SYS_Report("PH3 ACTION turn=%u slot=%u REJECTED (unsupported or no PP)\n", turn, a.move_slot);
            b.state_sent = false;
            return BV_RUNNING;
        }
        battle_choose_ai(&b.battle, 1, &acts[1]);
        SYS_Report("PH3 ACTION turn=%u player_slot=%u move=%u ai_slot=%u move=%u input_us=%u\n", turn, a.move_slot,
                   battler(0)->move_ids[a.move_slot], acts[1].move_slot, battler(1)->move_ids[acts[1].move_slot],
                   us_since(b.turn_started));
        resolve_turn(acts, 2);
    } else {
        return BV_RUNNING;
    }
    return play_turn(turn);
}

static bool player_can_act(Position user) {
    if (battle_can_shift(&b.battle, user) || (battle_switch_mask(&b.battle, user) & ~b.reserved)) return true;
    const bool rotation = b.request.format == BATTLE_FORMAT_ROTATION;
    for (uint8_t slot = 0; slot < b.battle.sides[0].active_count; ++slot) {
        if (!rotation && slot != user.slot) continue;
        for (unsigned move = 0; move < 4; ++move)
            if (battle_command_targets(&b.battle, (Position){0, slot}, move)) return true;
    }
    return false;
}

static BattleViewStatus step_multi_choose(void) {
    const uint8_t turn = (uint8_t)(b.battle.turn + 1);
    if (!b.player_commands) {
        b.player_commands = battle_collect_actors(&b.battle, 0, b.players);
        b.choosing = 0;
        b.reserved = 0;
        b.turn_started = gettime();
    }
    const Position user = b.players[b.choosing];
    if (!b.state_sent) {
        if (!player_can_act(user)) {
            const Action none = {.user = user};
            resolve_turn(&none, 1);
            return play_turn(turn);
        }
        if (++b.token == 0) ++b.token;
        if (!send_state(BPHASE_CHOOSE)) return BV_LOST;
        b.state_sent = true;
        battle_scene_choose(turn, user);
        SYS_Report("MULTI PROMPT turn=%u actor=%u token=%u\n", turn, user.slot, b.token);
    }
    BattleMultiAction a;
    if (!poll_multi_action(&a)) return BV_LOST;
    if ((a.flags & BACTION_FLAG_EXIT) ||
        (a.token == b.token && a.actor == user.slot && a.action == BACTION_FORFEIT)) {
        const Action forfeit = {.kind = ACT_FORFEIT, .user = user};
        resolve_turn(&forfeit, 1);
        return play_turn(turn);
    }
    if (a.token != b.token || a.actor != user.slot || a.action == BACTION_NONE) return BV_RUNNING;
    const bool switching = a.action == BACTION_SWITCH;
    const Action act = {
        .kind = switching ? ACT_SWITCH : a.action == BACTION_SHIFT ? ACT_SHIFT : ACT_MOVE,
        .user = user, .move_slot = a.move_slot, .target = {{1, a.target}}, .rotation = a.rotation, .reserve = a.target,
    };
    if ((a.action > BACTION_SHIFT && !switching) || !battle_validate_action(&b.battle, &act) ||
        (switching && (b.reserved & (1u << act.reserve)))) {
        SYS_Report("MULTI ACTION turn=%u actor=%u target=%u rejected\n", turn, user.slot, a.target);
        b.state_sent = false;
        return BV_RUNNING;
    }
    SYS_Report("MULTI ACTION turn=%u actor=%u kind=%u move_slot=%u target=%u rotation=%u\n", turn, user.slot,
               act.kind, act.move_slot, a.target, act.rotation);
    if (switching) b.reserved |= 1u << act.reserve;
    b.actions[b.choosing++] = act;
    b.state_sent = false;
    if (b.choosing < b.player_commands) return BV_RUNNING;
    Position opponents[BC_MAX_ACTIVE];
    const unsigned count = battle_collect_actors(&b.battle, 1, opponents);
    for (unsigned i = 0; i < count; ++i)
        battle_choose_at(&b.battle, opponents[i], &b.actions[b.player_commands + i]);
    resolve_turn(b.actions, b.player_commands + count);
    b.player_commands = 0;
    return play_turn(turn);
}

static void next_prompt(void) {
    b.state_sent = false;
    set_bstate(battle_pending_replacement(&b.battle, &b.replacement) ? B_REPLACE : B_CHOOSE);
}

static BattleViewStatus step_replacement(void) {
    const Position pos = b.replacement;
    const uint8_t mask = battle_replacement_mask(&b.battle, pos);
    if (pos.side == 1) {
        for (uint8_t i = 0; i < BATTLE_MAX_PER_SIDE; ++i)
            if (battle_replace(&b.battle, pos, i, &b.log)) return play_turn(b.battle.turn);
        return BV_LOST;
    }
    if (!b.state_sent) {
        if (++b.token == 0) ++b.token;
        if (!send_state(BPHASE_REPLACE)) return BV_LOST;
        b.state_sent = true;
        battle_scene_choose(b.battle.turn, pos);
        SYS_Report("ROSTER REPLACE pos=%u mask=%02x token=%u\n", pos.slot, mask, b.token);
    }
    BattleMultiAction a;
    if (!poll_multi_action(&a)) return BV_LOST;
    const bool current = a.token == b.token && a.actor == pos.slot;
    if ((a.flags & BACTION_FLAG_EXIT) || (current && a.action == BACTION_FORFEIT)) {
        const Action forfeit = {.kind = ACT_FORFEIT, .user = pos};
        resolve_turn(&forfeit, 1);
        return play_turn(b.battle.turn);
    }
    if (!current || a.action == BACTION_NONE) return BV_RUNNING;
    if (a.action != BACTION_REPLACE || !battle_replace(&b.battle, pos, a.target, &b.log)) {
        b.state_sent = false;
        return BV_RUNNING;
    }
    SYS_Report("ROSTER PLAYER SEND OUT pos=%u roster=%u\n", pos.slot, a.target);
    return play_turn(b.battle.turn);
}

BattleViewStatus battle_view_update(bool start_pressed, const char **message) {
    if (start_pressed && b.state != B_WAIT_EXIT) {
        SYS_Report("PH3 BATTLE aborted from the GameCube in %s (no result sent)\n", bstate_names[b.state]);
        *message = host_bye(b.link, BYE_HOST_EXIT) ? "Battle aborted from the GameCube" : "Battle aborted (BYE lost)";
        return BV_ENDED;
    }
    switch (b.state) {
    case B_REQUEST:
        if (!b.digest_logged) {
            if (!log_digest("start")) return BV_LOST;
            b.digest_logged = true;
        }
        switch (fetch_step(b.link, &fetch)) {
        case FETCH_PENDING: return BV_RUNNING;
        case FETCH_DONE: break;
        case FETCH_MISSING: *message = "Battle request missing"; return BV_ENDED;
        case FETCH_FAILED: return BV_LOST;
        }
        b.request_us = fetch_elapsed_us(&fetch);
        if (!battle_request_decode(fetch.data, fetch.info.length, &b.request) ||
            b.request.version != b.mode.app_version) {
            SYS_Report("PH3 REQUEST rejected (decode)\n");
            *message = "Battle request rejected";
            host_bye(b.link, BYE_HOST_EXIT);
            return BV_ENDED;
        }
        SYS_Report("PH3 REQUEST format=%u rules=%04x trainer=%u class=%u env=%u policy=%u types=%u battle=%08x bytes=%u wire=%u us=%u\n",
                   b.request.format, b.request.rules, b.request.trainer_id, b.request.trainer_class,
                   b.request.environment, b.request.result_policy, b.request.type_count, b.request.battle_id,
                   fetch.info.length, fetch.wire_length, b.request_us);
        for (unsigned i = 0; i < b.request.player_count; ++i) log_entry("PLAYER", &b.request.player[i]);
        for (unsigned i = 0; i < b.request.opponent_count; ++i) log_entry("OPPONENT", &b.request.opponent[i]);
        build_plan();
        b.status = "Receiving battle sprites and labels";
        if (!set_phase(PHASE_ASSETS, 0, b.plan_count)) return BV_LOST;
        b.turn_started = gettime();
        set_bstate(B_ASSETS);
        return BV_RUNNING;
    case B_ASSETS: {
        const unsigned before = b.plan_index;
        const int r = fetch_plan();
        if (r < 0) return BV_LOST;
        if (r == 0) {
            if (b.plan_index != before && !set_phase(PHASE_ASSETS, b.plan_index, b.plan_count)) return BV_LOST;
            return BV_RUNNING;
        }
        b.assets_us = us_since(b.turn_started);
        const CacheStats c = cache_stats();
        SYS_Report("PRESENTATION ASSETS %u keys raw=%u wire=%u us=%u cache_entries=%u cache_arena=%u\n", b.plan_count,
                   b.asset_bytes, b.wire_bytes, b.assets_us, c.entries, c.arena_used);
        set_bstate(B_START);
        return BV_RUNNING;
    }
    case B_START: {
        const uint32_t seed = (uint32_t)gettime() ^ b.request.battle_id;
        b.init_error = battle_init(&b.battle, &b.request, seed);
        SYS_Report("PH3 CORE init=%d (%s) seed=%08x\n", b.init_error, battle_init_error_name(b.init_error), seed);
        if (!set_phase(PHASE_READY, b.plan_count, b.plan_count)) return BV_LOST;
        if (b.init_error != BI_OK) {
            b.outcome = OUTCOME_UNSUPPORTED;
            set_bstate(B_RESULT);
            return BV_RUNNING;
        }
        battle_scene_begin(build_id(), &b.battle);
        set_bstate(B_INTRO);
        return BV_RUNNING;
    }
    case B_INTRO:
        if (!keepalive(NULL)) return BV_LOST;
        if (battle_scene_tick()) next_prompt();
        return BV_RUNNING;
    case B_CHOOSE: return multi() ? step_multi_choose() : step_choose();
    case B_REPLACE: return step_replacement();
    case B_PLAY:
        if (b.last_play_poll) {
            const uint32_t gap = us_since(b.last_play_poll);
            if (gap > b.peak_play_gap_us) b.peak_play_gap_us = gap;
        }
        b.last_play_poll = gettime();
        if (!keepalive(NULL)) return BV_LOST;
        if (battle_scene_tick()) {
            if (b.battle.outcome != OUTCOME_NONE) {
                b.outcome = b.battle.outcome;
                set_bstate(B_RESULT);
            } else {
                next_prompt();
            }
        }
        return BV_RUNNING;
    case B_RESULT: return step_result();
    case B_WAIT_EXIT: {
        bool exit;
        if (!keepalive(&exit)) return BV_LOST;
        if (exit) {
            log_digest("end");
            *message = host_bye(b.link, BYE_CLIENT_EXIT) ? "Battle finished" : "Battle finished (BYE lost)";
            SYS_Report("PH3 BYE after battle\n");
            return BV_ENDED;
        }
        return BV_RUNNING;
    }
    }
    return BV_RUNNING;
}

// ------------------------------------------------------------------ drawing


void battle_view_draw(void) {
    render_begin();
    render_rect(0, 0, 640, 480, RGBA(12, 21, 34, 255));
    if (b.state >= B_START && b.init_error == BI_OK) {
        battle_scene_draw();
    } else if (b.init_error != BI_OK) {
        render_text(24, 220, 1.25f, WHITE, "Battle cannot run: %s", battle_init_error_name(b.init_error));
    } else {
        render_text(24, 220, 1.5f, WHITE, "%s", b.status ? b.status : "");
        if (b.state == B_ASSETS) render_text(24, 250, 1, DIM, "%u / %u", b.plan_index, b.plan_count);
    }
    render_end();
}
