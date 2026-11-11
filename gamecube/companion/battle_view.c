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

#define EVENT_FRAMES 45
#define KEEPALIVE_FRAMES 1
#define MAX_PLAN 24

enum BState { B_REQUEST, B_ASSETS, B_START, B_CHOOSE, B_PLAY, B_RESULT, B_WAIT_EXIT };
static const char *const bstate_names[] = {"REQUEST", "ASSETS", "START", "CHOOSE", "PLAY", "RESULT", "WAIT_EXIT"};

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
    unsigned event_index, event_frames, keepalive;
    float shown_hp[2];
    uint8_t outcome;
    uint16_t result_status;
    uint64_t started, turn_started;
    uint32_t request_us, assets_us, asset_bytes;
    const char *status;
    bool digest_logged;
} b;
static AssetFetch fetch;

static uint32_t us_since(uint64_t t) { return (uint32_t)ticks_to_microsecs(gettime() - t); }
static uint32_t build_id(void) { return b.link->client.build_id; }

static void set_bstate(enum BState s) {
    SYS_Report("PH3 BATTLE %s -> %s\n", bstate_names[b.state], bstate_names[s]);
    b.state = s;
}

static bool pkmn_app(uint16_t op, const uint8_t *args, unsigned n) {
    uint16_t status;
    return host_app_to(b.link, APP_POKEMON, op, args, n, NULL, NULL, &status) && status == APP_OK;
}

static bool set_phase(uint8_t phase, unsigned done, unsigned total) {
    const uint8_t args[3] = {phase, (uint8_t)done, (uint8_t)total};
    return pkmn_app(PKMN_SET_PHASE, args, 3);
}

static Battler *battler(uint8_t side) { return battle_active(&b.battle, side); }

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

static bool send_state(uint8_t phase) {
    BattleState s = {0};
    uint8_t bytes[BATTLE_STATE_BYTES];
    uint16_t status;
    const Battler *p = battler(0), *o = battler(1);
    s.phase = phase;
    s.turn = (uint8_t)(b.battle.turn + (phase == BPHASE_CHOOSE ? 1 : 0));
    s.outcome = b.battle.outcome ? b.battle.outcome : b.outcome;
    s.player_slot = p->party_slot;
    s.player_hp = p->hp;
    s.player_max_hp = p->max_hp;
    s.opponent_hp = o->hp;
    s.opponent_max_hp = o->max_hp;
    memcpy(s.pp, p->pp, 4);
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
        SYS_Report("PH3 ACTION poll: status=%u length=%u bytes=%02x %02x %02x %02x\n", status, n, n > 0 ? d[0] : 0,
                   n > 1 ? d[1] : 0, n > 2 ? d[2] : 0, n > 3 ? d[3] : 0);
        return false;
    }
    return true;
}

static AssetKey label(uint32_t kind, uint32_t value) { return (AssetKey){ASSET_TEXT_LABEL, LABEL_ID(kind, value), 0, 0}; }

static void plan_add(AssetKey key) {
    for (unsigned i = 0; i < b.plan_count; ++i)
        if (asset_key_equal(&b.plan[i], &key)) return;
    if (b.plan_count < MAX_PLAN) b.plan[b.plan_count++] = key;
}

static void build_plan(void) {
    b.plan_count = b.plan_index = 0;
    for (unsigned side = 0; side < 2; ++side) {
        const BattleEntry *e = side ? &b.request.opponent[0] : &b.request.player[0];
        plan_add(e->record.sprite);
        plan_add(pkmn_nickname_label(&e->record));
        plan_add(label(LABEL_SPECIES, e->record.species));
        for (unsigned m = 0; m < 4; ++m)
            if (e->record.moves[m]) plan_add(label(LABEL_MOVE, e->record.moves[m]));
    }
}

void battle_view_begin(HostLink *link, const ModeInfo *mode) {
    memset(&b, 0, sizeof(b));
    b.link = link;
    b.mode = *mode;
    b.status = "Receiving battle request";
    b.started = gettime();
    const AssetKey key = {ASSET_MODE_REQUEST, mode->mode_id, 0, mode->nonce};
    fetch_start(&fetch, &key);
    b.state = B_REQUEST;
    SYS_Report("PH3 BATTLE begin request=%u bytes\n", mode->request_length);
}

static void log_entry(const char *side, const BattleEntry *e) {
    const PokemonRecord *r = &e->record;
    SYS_Report("PH3 %s slot=%u species=%u lv=%u hp=%u/%u atk=%u def=%u spa=%u spd=%u spe=%u types=%u/%u ability=%u item=%u status=%08x\n",
               side, r->slot, r->species, r->level, r->hp, r->max_hp, r->attack, r->defense, r->sp_attack,
               r->sp_defense, r->speed, e->mon.types[0], e->mon.type_count > 1 ? e->mon.types[1] : e->mon.types[0],
               r->ability, r->held_item, r->status);
    for (unsigned m = 0; m < 4; ++m)
        if (r->moves[m])
            SYS_Report("PH3   move%u id=%u power=%u type=%u cat=%u acc=%u prio=%d target=%u pp=%u/%u %s(%02x)\n", m,
                       r->moves[m], e->mon.moves[m].power, e->mon.moves[m].type, e->mon.moves[m].category,
                       e->mon.moves[m].accuracy, e->mon.moves[m].priority, e->mon.moves[m].target, r->pp[m],
                       r->max_pp[m], battle_move_supported(&b.request, &e->mon.moves[m]) ? "supported" : "UNSUPPORTED",
                       e->mon.moves[m].unsupported);
}

static void finish_battle(uint8_t outcome) {
    b.outcome = outcome;
    set_bstate(B_RESULT);
}

static BattleViewStatus step_result(void) {
    BattleResult r = {0};
    uint8_t bytes[RESULT_MAX_PAYLOAD];
    const Battler *p = battler(0), *o = battler(1);
    r.outcome = b.outcome;
    r.turns = b.battle.turn;
    r.player_count = 1;
    r.opponent_count = 1;
    r.battle_id = b.request.battle_id;
    r.player[0].slot = p->party_slot;
    r.player[0].flags = p->fainted ? BATTLE_RESULT_FAINTED : 0;
    r.player[0].hp = p->hp;
    r.player[0].status = p->status;
    memcpy(r.player[0].pp, p->pp, 4);
    r.opponent_hp[0] = o->hp;
    const unsigned n = battle_result_encode(&r, bytes);
    if (!send_state(BPHASE_ENDED)) return BV_LOST;
    if (!host_result(b.link, GC_MODE_BATTLE, BATTLE_RESULT_VERSION, r.battle_id, bytes, n, &b.result_status))
        return BV_LOST;
    SYS_Report("PH3 RESULT outcome=%u turns=%u player_hp=%u/%u opponent_hp=%u/%u pp=%u,%u,%u,%u battle=%08x status=%u total_us=%u\n",
               r.outcome, r.turns, p->hp, p->max_hp, o->hp, o->max_hp, p->pp[0], p->pp[1], p->pp[2], p->pp[3],
               r.battle_id, b.result_status, us_since(b.started));
    b.status = b.outcome == OUTCOME_WIN ? "You won! (A on the GBA to return)" :
               b.outcome == OUTCOME_LOSE ? "You lost... (A on the GBA to return)" :
               b.outcome == OUTCOME_CANCELLED ? "Battle cancelled on the GBA" : "Battle cannot run (unsupported)";
    set_bstate(B_WAIT_EXIT);
    return BV_RUNNING;
}

static BattleViewStatus step_choose(void) {
    BattleAction a;
    if (!b.state_sent) {
        if (!send_state(BPHASE_CHOOSE)) return BV_LOST;
        b.state_sent = true;
        b.turn_started = gettime();
        b.status = "Choose a move on the GBA";
    }
    if (!poll_action(&a)) return BV_LOST;
    const uint8_t turn = (uint8_t)(b.battle.turn + 1);
    if ((a.flags & BACTION_FLAG_EXIT) || (a.action == BACTION_FORFEIT && a.turn == turn)) {
        SYS_Report("PH3 ACTION turn=%u forfeit\n", turn);
        Action f = {ACT_FORFEIT, {0, 0}, 0, {{1, 0}}};
        battle_run_turn(&b.battle, &f, 1, &b.log);
    } else if (a.action == BACTION_MOVE && a.turn == turn) {
        Action acts[2] = {{ACT_MOVE, {0, 0}, a.move_slot, {{1, 0}}}, {0}};
        if (!battle_validate_action(&b.battle, &acts[0])) {
            SYS_Report("PH3 ACTION turn=%u slot=%u REJECTED (unsupported or no PP)\n", turn, a.move_slot);
            b.state_sent = false;  // re-send CHOOSE; the GBA clears its selection
            return BV_RUNNING;
        }
        battle_choose_ai(&b.battle, 1, &acts[1]);
        SYS_Report("PH3 ACTION turn=%u player_slot=%u move=%u ai_slot=%u move=%u input_us=%u\n", turn, a.move_slot,
                   battler(0)->move_ids[a.move_slot], acts[1].move_slot, battler(1)->move_ids[acts[1].move_slot],
                   us_since(b.turn_started));
        battle_run_turn(&b.battle, acts, 2, &b.log);
    } else {
        return BV_RUNNING;
    }
    for (unsigned i = 0; i < b.log.count; ++i) {
        const BattleEvent *e = &b.log.events[i];
        SYS_Report("PH3 EVENT turn=%u kind=%u user=%u target=%u move=%u amount=%u hp_after=%u eff=%u outcome=%u\n",
                   turn, e->kind, e->user.side, e->target.side, e->move_id, e->amount, e->hp_after, e->effectiveness,
                   e->outcome);
    }
    if (!send_state(BPHASE_WAIT)) return BV_LOST;
    b.event_index = 0;
    b.event_frames = 0;
    b.keepalive = 0;
    set_bstate(B_PLAY);
    return BV_RUNNING;
}

static BattleViewStatus step_play(void) {
    if (++b.keepalive >= KEEPALIVE_FRAMES) {  // the GBA times out after 2 s of silence
        BattleAction a;
        b.keepalive = 0;
        if (!poll_action(&a)) {
            SYS_Report("PH3 keepalive failed in PLAY event=%u/%u\n", b.event_index, b.log.count);
            return BV_LOST;
        }
    }
    if (b.event_index < b.log.count) {
        const BattleEvent *e = &b.log.events[b.event_index];
        if (e->kind == EV_DAMAGE) {
            const float target = e->hp_after, step = battler(e->target.side)->max_hp / 30.0f + 0.5f;
            float *hp = &b.shown_hp[e->target.side];
            *hp = *hp - step > target ? *hp - step : target;
        }
        if (++b.event_frames >= EVENT_FRAMES) {
            b.event_frames = 0;
            ++b.event_index;
        }
        return BV_RUNNING;
    }
    b.shown_hp[0] = battler(0)->hp;
    b.shown_hp[1] = battler(1)->hp;
    if (b.battle.outcome != OUTCOME_NONE) {
        finish_battle(b.battle.outcome);
    } else {
        b.state_sent = false;
        set_bstate(B_CHOOSE);
    }
    return BV_RUNNING;
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
        if (!battle_request_decode(fetch.data, fetch.info.length, &b.request)) {
            SYS_Report("PH3 REQUEST rejected (decode)\n");
            *message = "Battle request rejected";
            host_bye(b.link, BYE_HOST_EXIT);
            return BV_ENDED;
        }
        SYS_Report("PH3 REQUEST format=%u type=%u players=%u opponents=%u active=%u rules=%04x trainer=%u class=%u reward=%u policy=%u types=%u battle=%08x bytes=%u us=%u\n",
                   b.request.format, b.request.battle_type, b.request.player_count, b.request.opponent_count,
                   b.request.active_per_side, b.request.rules, b.request.trainer_id, b.request.trainer_class,
                   b.request.reward_mode, b.request.result_policy, b.request.type_count, b.request.battle_id,
                   fetch.info.length, b.request_us);
        log_entry("PLAYER", &b.request.player[0]);
        log_entry("OPPONENT", &b.request.opponent[0]);
        build_plan();
        b.assets_us = 0;
        b.status = "Receiving battle assets";
        if (!set_phase(PHASE_ASSETS, 0, b.plan_count)) return BV_LOST;
        set_bstate(B_ASSETS);
        b.turn_started = gettime();
        return BV_RUNNING;
    case B_ASSETS:
        while (!b.fetching && b.plan_index < b.plan_count) {
            if (cache_find(build_id(), &b.plan[b.plan_index])) { cache_count_hit(); ++b.plan_index; continue; }
            fetch_start(&fetch, &b.plan[b.plan_index]);
            b.fetching = true;
        }
        if (b.fetching) {
            switch (fetch_step(b.link, &fetch)) {
            case FETCH_PENDING: return BV_RUNNING;
            case FETCH_DONE: cache_insert(build_id(), &fetch.info, fetch.data); b.asset_bytes += fetch.info.length; break;
            case FETCH_MISSING: SYS_Report("PH3 ASSET %04x:%08x missing\n", fetch.key.type, fetch.key.id); break;
            case FETCH_FAILED: return BV_LOST;
            }
            b.fetching = false;
            ++b.plan_index;
            return set_phase(PHASE_ASSETS, b.plan_index, b.plan_count) ? BV_RUNNING : BV_LOST;
        }
        b.assets_us = us_since(b.turn_started);
        SYS_Report("PH3 ASSETS %u keys bytes=%u us=%u\n", b.plan_count, b.asset_bytes, b.assets_us);
        set_bstate(B_START);
        return BV_RUNNING;
    case B_START: {
        const uint32_t seed = (uint32_t)gettime() ^ b.request.battle_id;
        b.init_error = battle_init(&b.battle, &b.request, seed);
        SYS_Report("PH3 CORE init=%d (%s) seed=%08x\n", b.init_error, battle_init_error_name(b.init_error), seed);
        if (!set_phase(PHASE_READY, b.plan_count, b.plan_count)) return BV_LOST;
        b.shown_hp[0] = battler(0)->hp;
        b.shown_hp[1] = battler(1)->hp;
        if (b.init_error != BI_OK) {
            finish_battle(OUTCOME_UNSUPPORTED);
        } else {
            b.state_sent = false;
            set_bstate(B_CHOOSE);
        }
        return BV_RUNNING;
    }
    case B_CHOOSE: return step_choose();
    case B_PLAY: return step_play();
    case B_RESULT: return step_result();
    case B_WAIT_EXIT: {
        BattleAction a;
        if (!poll_action(&a)) return BV_LOST;
        if (a.flags & BACTION_FLAG_EXIT) {
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

#define WHITE RGBA(255, 255, 255, 255)
#define DIM RGBA(150, 160, 185, 255)
#define ACCENT RGBA(255, 214, 90, 255)

static float draw_label(const AssetKey *key, float x, float y, float scale, const char *fallback) {
    const CacheEntry *e = cache_find(build_id(), key);
    if (e) {
        render_texture((GXTexObj *)&e->texture, x, y, e->width * scale, e->height * scale, WHITE);
        return e->width * scale;
    }
    render_text(x, y, scale, DIM, "%s", fallback);
    return 8 * scale * strlen(fallback);
}

static float draw_name(uint8_t side, float x, float y) {
    const BattleEntry *e = side ? &b.request.opponent[0] : &b.request.player[0];
    AssetKey key = pkmn_nickname_label(&e->record);
    if (side) {
        render_text(x, y + 4, 1.5f, WHITE, "Foe ");
        x += 48;
    }
    return (side ? 48 : 0) + draw_label(&key, x, y, 1.5f, "?");
}

static void draw_info(uint8_t side, float x, float y) {
    const BattleEntry *e = side ? &b.request.opponent[0] : &b.request.player[0];
    const float hp = b.shown_hp[side], max = e->record.max_hp ? e->record.max_hp : 1;
    render_rect(x, y, 248, 70, RGBA(30, 40, 64, 235));
    draw_name(side, x + 8, y + 4);
    render_text(x + 190, y + 8, 1, WHITE, "Lv%u", e->record.level);
    render_rect(x + 8, y + 36, 232, 8, RGBA(20, 24, 36, 255));
    render_rect(x + 8, y + 36, 232 * hp / max, 8, hp * 5 > max ? RGBA(90, 220, 110, 255) : RGBA(240, 80, 70, 255));
    render_text(x + 8, y + 48, 1, WHITE, "HP %u/%u", (unsigned)(hp + 0.5f), e->record.max_hp);
}

static void draw_event(const BattleEvent *e, float y) {
    const char *eff = e->effectiveness > 4 ? "It's super effective!" : e->effectiveness && e->effectiveness < 4 ? "It's not very effective..." : NULL;
    float x = 24;
    AssetKey move = label(LABEL_MOVE, e->move_id);
    switch (e->kind) {
    case EV_MOVE_USED:
        x += draw_name(e->user.side, x, y);
        render_text(x, y + 4, 1.5f, WHITE, " used ");
        x += 72;
        x += draw_label(&move, x, y, 1.5f, "move");
        render_text(x, y + 4, 1.5f, WHITE, "!");
        break;
    case EV_MISS:
        x += draw_name(e->user.side, x, y);
        render_text(x, y + 4, 1.5f, WHITE, "'s attack missed!");
        break;
    case EV_DAMAGE:
        x += draw_name(e->target.side, x, y);
        render_text(x, y + 4, 1.5f, WHITE, " took %u damage.", e->amount);
        if (eff) render_text(24, y + 30, 1.5f, ACCENT, "%s", eff);
        break;
    case EV_NO_EFFECT:
        render_text(x, y + 4, 1.5f, WHITE, "It doesn't affect ");
        x += 18 * 12;
        draw_name(e->target.side, x, y);
        break;
    case EV_FAINT:
        x += draw_name(e->target.side, x, y);
        render_text(x, y + 4, 1.5f, WHITE, " fainted!");
        break;
    case EV_FORFEIT: render_text(x, y + 4, 1.5f, WHITE, "The battle was cancelled on the GBA."); break;
    case EV_NO_MOVES: render_text(x, y + 4, 1.5f, WHITE, "No usable move left (Struggle unsupported)."); break;
    case EV_END:
        render_text(x, y + 4, 1.5f, ACCENT, "%s", e->outcome == OUTCOME_WIN ? "You won!" : e->outcome == OUTCOME_LOSE ? "You lost..." : "Battle over.");
        break;
    }
}

void battle_view_draw(void) {
    render_begin();
    render_rect(0, 0, 640, 44, RGBA(22, 28, 48, 255));
    render_text(16, 10, 1.5f, WHITE, "GC BATTLE  SINGLE");
    render_text(300, 14, 1, DIM, "%s  turn %u", bstate_names[b.state], b.battle.turn);
    if (b.state >= B_START) {
        render_rect(0, 44, 640, 200, RGBA(40, 70, 60, 255));
        render_rect(0, 244, 640, 140, RGBA(52, 92, 70, 255));
        render_rect(380, 190, 220, 24, RGBA(70, 110, 80, 255));
        render_rect(40, 352, 240, 24, RGBA(70, 110, 80, 255));
        const CacheEntry *o = cache_find(build_id(), &b.request.opponent[0].record.sprite);
        const CacheEntry *p = cache_find(build_id(), &b.request.player[0].record.sprite);
        if (o && !(battler(1)->fainted && b.shown_hp[1] <= 0)) render_texture((GXTexObj *)&o->texture, 410, 60, 160, 160, WHITE);
        if (p && !(battler(0)->fainted && b.shown_hp[0] <= 0)) render_texture_mirrored((GXTexObj *)&p->texture, 64, 192, 192, 192, WHITE);
        draw_info(1, 24, 60);
        draw_info(0, 368, 270);
    }
    render_rect(0, 384, 640, 72, RGBA(10, 12, 20, 240));
    if (b.state == B_PLAY && b.event_index < b.log.count) draw_event(&b.log.events[b.event_index], 396);
    else render_text(24, 400, 1.5f, ACCENT, "%s", b.status ? b.status : "");
    if (b.state == B_ASSETS) render_text(24, 430, 1, DIM, "%u / %u", b.plan_index, b.plan_count);
    if (b.state == B_WAIT_EXIT && b.init_error != BI_OK)
        render_text(24, 430, 1, DIM, "%s", battle_init_error_name(b.init_error));
    render_text(16, 458, 1, DIM, "rules: no abilities/items/status/gimmicks/crits  START: abort");
    render_end();
}
