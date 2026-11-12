// GC_MODE_BATTLE host: runs the battle core and presents it like the GBA does.
//
// The scene is laid out in GBA screen coordinates (240x160) with the game's own
// graphics, sent by the client: terrain background, textbox, healthbox frames
// with name/level printed by the game, HP bar tiles, healthbox digits, battle
// messages rendered from the game's text templates, and front/back Pokemon
// pictures. Coordinates follow battle_anim_mons.c / battle_interface.c. The
// whole scene is scaled to the TV. Animations reproduce the GBA's generic
// battle effects (send-out, attack nudge, hit blink, HP drain, faint drop);
// per-move animation scripts are not reproduced.
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

#define MAX_PLAN 40
#define MAX_STEPS 64
#define SCALE (8.0f / 3.0f)                       // 240x160 -> 640x427
#define ORIGIN_Y ((480.0f - 160.0f * SCALE) / 2)  // letterbox

enum BState { B_REQUEST, B_ASSETS, B_START, B_INTRO, B_CHOOSE, B_MESSAGES, B_PLAY, B_RESULT, B_WAIT_EXIT };
static const char *const bstate_names[] = {"REQUEST", "ASSETS", "START", "INTRO", "CHOOSE", "MESSAGES", "PLAY", "RESULT",
                                           "WAIT_EXIT"};

enum StepKind { S_FADE_IN, S_MESSAGE, S_WAIT, S_APPEAR, S_LUNGE, S_HIT, S_DRAIN, S_FAINT };
typedef struct {
    uint8_t kind, side;
    uint16_t frames;    // S_WAIT / fixed-length steps
    uint16_t value;     // S_DRAIN target HP
    AssetKey message;   // S_MESSAGE
    const char *fallback;
} Step;

typedef struct {
    bool visible;
    float dx, dy, scale, drop;  // drop: faint slide in pixels
    bool blink_hidden;
} ActorView;

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
    Step steps[MAX_STEPS];
    unsigned step_count, step_index, step_frame;
    ActorView actor[2];
    float healthbox_dx[2];
    bool healthbox_visible[2];
    float shown_hp[2];
    float fade;  // 1 = black
    AssetKey message;
    bool message_set;
    const char *message_fallback;
    uint8_t outcome;
    uint16_t result_status;
    uint64_t started, turn_started;
    uint32_t request_us, assets_us, asset_bytes, wire_bytes;
    const char *status;
    bool digest_logged;
    uint8_t digit_width[11];
    bool digits_measured;
} b;
static AssetFetch fetch;

static uint32_t us_since(uint64_t t) { return (uint32_t)ticks_to_microsecs(gettime() - t); }
static uint32_t build_id(void) { return b.link->client.build_id; }
static Battler *battler(uint8_t side) { return battle_active(&b.battle, side); }
static const BattleEntry *entry(uint8_t side) { return side ? &b.request.opponent[0] : &b.request.player[0]; }

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
        SYS_Report("PH3 ACTION poll: status=%u length=%u\n", status, n);
        return false;
    }
    return true;
}

// Dolphin fails the first SI write after a pause in JoyBus traffic, so every
// frame that does not otherwise talk to the GBA polls it.
static bool keepalive(BattleAction *out) {
    BattleAction a;
    if (!poll_action(&a)) return false;
    if (out) *out = a;
    return true;
}

// ------------------------------------------------------------------ assets

static AssetKey label(uint32_t kind, uint32_t value) { return (AssetKey){ASSET_TEXT_LABEL, LABEL_ID(kind, value), 0, 0}; }

static AssetKey sprite_key(uint8_t side) {
    AssetKey k = entry(side)->record.sprite;
    if (side == 0) k.variant |= PKMN_SPRITE_BACK;
    return k;
}

static AssetKey healthbox_key(uint8_t side) {
    const PokemonRecord *r = &entry(side)->record;
    return (AssetKey){ASSET_BATTLE_HEALTHBOX, side, r->level, r->personality};
}

static AssetKey message_key(uint8_t msg, uint8_t side, uint16_t move) {
    uint32_t extra = 0;
    switch (msg) {
    case BMSG_TRAINER_INTRO: case BMSG_TRAINER_SENT_OUT: case BMSG_WIN: case BMSG_LOSE:
        extra = b.request.trainer_id | (uint32_t)entry(1)->record.personality << 16;
        break;
    case BMSG_SUPER_EFFECTIVE: case BMSG_NOT_VERY_EFFECTIVE:
        side = 0;
        break;
    case BMSG_FORFEIT:
        side = 0;
        break;
    default:
        extra = entry(side)->record.personality;
        break;
    }
    return (AssetKey){ASSET_BATTLE_MESSAGE, BMSG_KEY(msg, side), move, extra};
}

static void plan_add(AssetKey key) {
    for (unsigned i = 0; i < b.plan_count; ++i)
        if (asset_key_equal(&b.plan[i], &key)) return;
    if (b.plan_count < MAX_PLAN) b.plan[b.plan_count++] = key;
}

static void build_plan(void) {
    b.plan_count = b.plan_index = 0;
    plan_add((AssetKey){ASSET_BATTLE_BG, b.request.environment, 0, 0});
    plan_add((AssetKey){ASSET_BATTLE_TEXTBOX, 0, 0, 0});
    plan_add((AssetKey){ASSET_BATTLE_HPBAR, 0, 0, 0});
    plan_add((AssetKey){ASSET_BATTLE_DIGITS, 0, 0, 0});
    for (unsigned side = 0; side < 2; ++side) {
        plan_add(sprite_key(side));
        plan_add(healthbox_key(side));
        plan_add(pkmn_nickname_label(&entry(side)->record));
        for (unsigned m = 0; m < 4; ++m)
            if (entry(side)->record.moves[m]) plan_add(label(LABEL_MOVE, entry(side)->record.moves[m]));
    }
    plan_add(message_key(BMSG_TRAINER_INTRO, 1, 0));
    plan_add(message_key(BMSG_TRAINER_SENT_OUT, 1, 0));
    plan_add(message_key(BMSG_GO_PKMN, 0, 0));
    plan_add(message_key(BMSG_WHAT_WILL_DO, 0, 0));
}

// Healthbox digits: measure each glyph so numbers are spaced like the game's font.
static void measure_digits(const uint8_t *d) {
    for (unsigned c = 0; c < 11; ++c) {
        unsigned right = 0;
        for (unsigned row = 0; row < 16; ++row)
            for (unsigned x = 0; x < 8; ++x) {
                const uint8_t byte = d[((row / 8) * 11 + c) * 32 + (row % 8) * 4 + x / 2];
                if ((byte >> (4 * (x & 1))) & 15) right = x + 1 > right ? x + 1 : right;
            }
        b.digit_width[c] = right ? (uint8_t)(right + 1) : 4;
    }
    b.digits_measured = true;
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
        if (fetch.key.type == ASSET_BATTLE_DIGITS) measure_digits(fetch.data);
        if (!cache_insert(build_id(), &fetch.info, fetch.data))
            SYS_Report("PH4 ASSET %04x:%08x not cached (format %u)\n", fetch.key.type, fetch.key.id, fetch.info.pixel_format);
        b.asset_bytes += fetch.info.length;
        b.wire_bytes += fetch.wire_length;
        SYS_Report("PH4 ASSET %04x:%08x:%08x %ux%u raw=%u wire=%u%s us=%u\n", fetch.key.type, fetch.key.id,
                   fetch.key.variant, fetch.info.width, fetch.info.height, fetch.info.length, fetch.wire_length,
                   fetch.info.encoding == ASSET_ENCODING_RLE ? " rle" : "", fetch_elapsed_us(&fetch));
        break;
    case FETCH_MISSING:
        SYS_Report("PH4 ASSET %04x:%08x missing (status %u)\n", fetch.key.type, fetch.key.id, fetch.info.status);
        break;
    case FETCH_FAILED:
        return -1;
    }
    b.fetching = false;
    ++b.plan_index;
    return 0;
}

// ------------------------------------------------------------------ timeline

static void add_step(Step s) { if (b.step_count < MAX_STEPS) b.steps[b.step_count++] = s; }
static void add_message(uint8_t msg, uint8_t side, uint16_t move, const char *fallback, uint16_t hold) {
    add_step((Step){S_MESSAGE, side, 0, 0, message_key(msg, side, move), fallback});
    if (hold) add_step((Step){S_WAIT, 0, hold, 0, {0}, NULL});
}
static void start_timeline(void) { b.step_index = 0; b.step_frame = 0; }

static void build_intro(void) {
    b.step_count = 0;
    add_step((Step){S_FADE_IN, 0, 20, 0, {0}, NULL});
    add_message(BMSG_TRAINER_INTRO, 1, 0, "The trainer wants to battle!", 80);
    add_message(BMSG_TRAINER_SENT_OUT, 1, 0, "The trainer sent out a Pokemon!", 0);
    add_step((Step){S_APPEAR, 1, 18, 0, {0}, NULL});
    add_step((Step){S_WAIT, 0, 40, 0, {0}, NULL});
    add_message(BMSG_GO_PKMN, 0, 0, "Go!", 0);
    add_step((Step){S_APPEAR, 0, 18, 0, {0}, NULL});
    add_step((Step){S_WAIT, 0, 40, 0, {0}, NULL});
    start_timeline();
}

static void build_turn_timeline(void) {
    b.step_count = 0;
    for (unsigned i = 0; i < b.log.count; ++i) {
        const BattleEvent *e = &b.log.events[i];
        switch (e->kind) {
        case EV_MOVE_USED:
            add_message(BMSG_USED_MOVE, e->user.side, e->move_id, "used a move!", 0);
            add_step((Step){S_LUNGE, e->user.side, 16, 0, {0}, NULL});
            add_step((Step){S_WAIT, 0, 16, 0, {0}, NULL});
            break;
        case EV_MISS:
            add_message(BMSG_MISSED, e->target.side, 0, "The attack missed!", 50);
            break;
        case EV_DAMAGE:
            add_step((Step){S_HIT, e->target.side, 28, 0, {0}, NULL});
            add_step((Step){S_DRAIN, e->target.side, 0, e->hp_after, {0}, NULL});
            if (e->effectiveness > 4) add_message(BMSG_SUPER_EFFECTIVE, 0, 0, "It's super effective!", 50);
            else if (e->effectiveness && e->effectiveness < 4) add_message(BMSG_NOT_VERY_EFFECTIVE, 0, 0, "It's not very effective...", 50);
            else add_step((Step){S_WAIT, 0, 12, 0, {0}, NULL});
            break;
        case EV_NO_EFFECT:
            add_message(BMSG_NO_EFFECT, e->target.side, 0, "It doesn't affect the target...", 50);
            break;
        case EV_FAINT:
            add_step((Step){S_FAINT, e->target.side, 24, 0, {0}, NULL});
            add_message(BMSG_FAINTED, e->target.side, 0, "fainted!", 60);
            break;
        case EV_FORFEIT:
            add_message(BMSG_FORFEIT, 0, 0, "The battle was cancelled.", 40);
            break;
        case EV_NO_MOVES:
            add_step((Step){S_WAIT, 0, 30, 0, {0}, NULL});
            break;
        case EV_END:
            if (e->outcome == OUTCOME_WIN) add_message(BMSG_WIN, 1, 0, "You won!", 30);
            else if (e->outcome == OUTCOME_LOSE) add_message(BMSG_LOSE, 1, 0, "You lost...", 30);
            break;
        }
    }
    start_timeline();
}

// Messages of the timeline not cached yet become the fetch plan.
static void plan_timeline_messages(void) {
    b.plan_count = b.plan_index = 0;
    for (unsigned i = 0; i < b.step_count; ++i)
        if (b.steps[i].kind == S_MESSAGE) plan_add(b.steps[i].message);
}

static float ease(float t) { return t < 0 ? 0 : t > 1 ? 1 : t * (2 - t); }

// Advances one frame; returns true when the timeline is finished.
static bool run_timeline(void) {
    while (b.step_index < b.step_count) {
        Step *s = &b.steps[b.step_index];
        const unsigned f = b.step_frame++;
        ActorView *a = &b.actor[s->side];
        bool done = false;
        switch (s->kind) {
        case S_FADE_IN:
            b.fade = 1.0f - (float)(f + 1) / s->frames;
            done = f + 1 >= s->frames;
            break;
        case S_MESSAGE:
            b.message = s->message;
            b.message_set = true;
            b.message_fallback = s->fallback;
            done = true;
            break;
        case S_WAIT:
            done = f + 1 >= s->frames;
            break;
        case S_APPEAR: {
            const float t = ease((float)(f + 1) / s->frames);
            a->visible = true;
            a->scale = t;
            b.healthbox_visible[s->side] = true;
            b.healthbox_dx[s->side] = (1.0f - t) * (s->side ? -128.0f : 128.0f);
            done = f + 1 >= s->frames;
            if (done) { a->scale = 1; b.healthbox_dx[s->side] = 0; }
            break;
        }
        case S_LUNGE: {
            const float t = f < s->frames / 2 ? (float)f / (s->frames / 2) : (float)(s->frames - f) / (s->frames / 2);
            a->dx = (s->side ? -8.0f : 8.0f) * t;
            a->dy = (s->side ? 4.0f : -4.0f) * t;
            done = f + 1 >= s->frames;
            if (done) a->dx = a->dy = 0;
            break;
        }
        case S_HIT:  // the GBA's hit flicker: three blinks with a small shake
            a->blink_hidden = (f / 4) % 2 == 0 && f < 24;
            a->dx = f < 24 ? ((f / 2) % 2 ? 2.0f : -2.0f) : 0;
            done = f + 1 >= s->frames;
            if (done) { a->blink_hidden = false; a->dx = 0; }
            break;
        case S_DRAIN: {  // about one bar pixel (of 48) per frame, like the GBA
            float *hp = &b.shown_hp[s->side];
            const float step = entry(s->side)->record.max_hp / 48.0f;
            const float target = s->value;
            *hp = *hp - (step < 1 ? 1 : step) > target ? *hp - (step < 1 ? 1 : step) : target;
            done = *hp <= target;
            break;
        }
        case S_FAINT:
            a->drop = 64.0f * (float)(f + 1) / s->frames;
            done = f + 1 >= s->frames;
            if (done) { a->visible = false; b.healthbox_visible[s->side] = false; }
            break;
        }
        if (!done) return false;
        ++b.step_index;
        b.step_frame = 0;
        if (s->kind != S_MESSAGE) return false;  // messages take no frame of their own
    }
    return true;
}

// ------------------------------------------------------------------ flow

void battle_view_begin(HostLink *link, const ModeInfo *mode) {
    memset(&b, 0, sizeof(b));
    b.link = link;
    b.mode = *mode;
    b.status = "Receiving battle request";
    b.started = gettime();
    b.fade = 1;
    const AssetKey key = {ASSET_MODE_REQUEST, mode->mode_id, 0, mode->nonce};
    fetch_start(&fetch, &key);
    b.state = B_REQUEST;
    SYS_Report("PH3 BATTLE begin request=%u bytes\n", mode->request_length);
}

static void log_entry(const char *side, const BattleEntry *e) {
    const PokemonRecord *r = &e->record;
    SYS_Report("PH3 %s slot=%u species=%u lv=%u hp=%u/%u types=%u/%u y=%d/%d elev=%u\n", side, r->slot, r->species,
               r->level, r->hp, r->max_hp, e->mon.types[0], e->mon.type_count > 1 ? e->mon.types[1] : e->mon.types[0],
               e->mon.front_y, e->mon.back_y, e->mon.elevation);
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
    set_bstate(B_WAIT_EXIT);
    return BV_RUNNING;
}

static BattleViewStatus step_choose(void) {
    BattleAction a;
    if (!b.state_sent) {
        if (!send_state(BPHASE_CHOOSE)) return BV_LOST;
        b.state_sent = true;
        b.turn_started = gettime();
        b.message = message_key(BMSG_WHAT_WILL_DO, 0, 0);
        b.message_set = true;
        b.message_fallback = "What will it do?";
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
            b.state_sent = false;
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
    b.message_set = false;
    build_turn_timeline();
    plan_timeline_messages();
    b.turn_started = gettime();
    set_bstate(B_MESSAGES);
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
        SYS_Report("PH3 REQUEST format=%u rules=%04x trainer=%u class=%u env=%u policy=%u types=%u battle=%08x bytes=%u wire=%u us=%u\n",
                   b.request.format, b.request.rules, b.request.trainer_id, b.request.trainer_class,
                   b.request.environment, b.request.result_policy, b.request.type_count, b.request.battle_id,
                   fetch.info.length, fetch.wire_length, b.request_us);
        log_entry("PLAYER", &b.request.player[0]);
        log_entry("OPPONENT", &b.request.opponent[0]);
        build_plan();
        b.status = "Receiving battle graphics";
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
        SYS_Report("PH4 ASSETS %u keys raw=%u wire=%u us=%u cache_entries=%u cache_arena=%u\n", b.plan_count,
                   b.asset_bytes, b.wire_bytes, b.assets_us, c.entries, c.arena_used);
        set_bstate(B_START);
        return BV_RUNNING;
    }
    case B_START: {
        const uint32_t seed = (uint32_t)gettime() ^ b.request.battle_id;
        b.init_error = battle_init(&b.battle, &b.request, seed);
        SYS_Report("PH3 CORE init=%d (%s) seed=%08x\n", b.init_error, battle_init_error_name(b.init_error), seed);
        if (!set_phase(PHASE_READY, b.plan_count, b.plan_count)) return BV_LOST;
        b.shown_hp[0] = battler(0)->hp;
        b.shown_hp[1] = battler(1)->hp;
        if (b.init_error != BI_OK) {
            b.outcome = OUTCOME_UNSUPPORTED;
            b.fade = 0;
            set_bstate(B_RESULT);
            return BV_RUNNING;
        }
        build_intro();
        set_bstate(B_INTRO);
        return BV_RUNNING;
    }
    case B_INTRO:
        if (!keepalive(NULL)) return BV_LOST;
        if (run_timeline()) {
            b.state_sent = false;
            set_bstate(B_CHOOSE);
        }
        return BV_RUNNING;
    case B_CHOOSE: return step_choose();
    case B_MESSAGES: {
        const int r = fetch_plan();
        if (r < 0) return BV_LOST;
        if (r == 1) {
            SYS_Report("PH4 TURN messages ready us=%u\n", us_since(b.turn_started));
            set_bstate(B_PLAY);
        }
        return BV_RUNNING;
    }
    case B_PLAY:
        if (!keepalive(NULL)) return BV_LOST;
        if (run_timeline()) {
            b.shown_hp[0] = battler(0)->hp;
            b.shown_hp[1] = battler(1)->hp;
            if (b.battle.outcome != OUTCOME_NONE) {
                b.outcome = b.battle.outcome;
                set_bstate(B_RESULT);
            } else {
                b.state_sent = false;
                set_bstate(B_CHOOSE);
            }
        }
        return BV_RUNNING;
    case B_RESULT: return step_result();
    case B_WAIT_EXIT: {
        BattleAction a;
        if (!keepalive(&a)) return BV_LOST;
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

static float tx(float gx) { return gx * SCALE; }
static float ty(float gy) { return ORIGIN_Y + gy * SCALE; }

static const CacheEntry *asset(const AssetKey *key) { return cache_find(build_id(), key); }

static void draw_gba(const CacheEntry *e, float gx, float gy) {
    if (e) render_texture((GXTexObj *)&e->texture, tx(gx), ty(gy), e->width * SCALE, e->height * SCALE, WHITE);
}

static const int8_t kFallbackY = 0;

// battle_anim_mons.c: singles player (72, 80), opponent (176, 40); final Y
// adds the picture offset, the opponent's elevation is subtracted and the
// player's sprite sits 8 pixels lower (clamped to 104).
static float actor_center_y(uint8_t side) {
    const BattleMon *m = &entry(side)->mon;
    if (side) return 40.0f + m->front_y - m->elevation;
    float y = 80.0f + m->back_y + 8;
    return y > 104 ? 104 : y;
}

static void draw_actor(uint8_t side) {
    const ActorView *a = &b.actor[side];
    const AssetKey key = sprite_key(side);
    const CacheEntry *e = asset(&key);
    if (!e || !a->visible || a->blink_hidden) return;
    const float cx = (side ? 176.0f : 72.0f) + a->dx, cy = actor_center_y(side) + a->dy;
    const float size = 64.0f * a->scale;
    const float left = cx - size / 2, top = cy + 32 - size + a->drop;  // grows from and drops below its base
    render_scissor(tx(0), ty(0), tx(240), ty(cy + 32) - ty(0));        // the faint drop disappears at the base
    render_texture_part((GXTexObj *)&e->texture, e->width, e->height, 0, 0, 64, 64, tx(left), ty(top), size * SCALE,
                        size * SCALE, WHITE);
    render_scissor_reset();
}

static void draw_number(unsigned value, float right, float gy) {
    const AssetKey key = {ASSET_BATTLE_DIGITS, 0, 0, 0};
    const CacheEntry *e = asset(&key);
    char text[8];
    snprintf(text, sizeof(text), "%u", value);
    if (!e) return;
    float x = right;
    for (int i = (int)strlen(text) - 1; i >= 0; --i) {
        const unsigned c = (unsigned)(text[i] - '0');
        x -= b.digits_measured ? b.digit_width[c] : 6;
        render_texture_part((GXTexObj *)&e->texture, e->width, e->height, c * 8, 0, 8, 16, tx(x), ty(gy), 8 * SCALE,
                            16 * SCALE, WHITE);
    }
}

static float number_width(unsigned value) {
    char text[8];
    float w = 0;
    snprintf(text, sizeof(text), "%u", value);
    for (const char *p = text; *p; ++p) w += b.digits_measured ? b.digit_width[*p - '0'] : 6;
    return w;
}

// battle_interface.c: healthbox sprites are created 64x32 (corner vector
// -32,-16) at (158, 88) player / (44, 30) opponent; the player's are then
// switched to 64x64 without recomputing the vector, so both tops are y-16.
// The HP bar sprite sits at healthbox x +16 (player) / +8 (opponent) with 32x8
// subsprites at -16 and +16, top at the sprite's y: [H][P][six bar tiles].
// HP text is right-aligned at x = 96 of the healthbox, y = 21.
static void draw_healthbox(uint8_t side) {
    if (!b.healthbox_visible[side]) return;
    const AssetKey key = healthbox_key(side);
    const CacheEntry *box = asset(&key);
    const float left = (side ? 12.0f : 126.0f) + b.healthbox_dx[side], top = side ? 14.0f : 72.0f;
    draw_gba(box, left, top);
    const AssetKey bar_key = {ASSET_BATTLE_HPBAR, 0, 0, 0};
    const CacheEntry *bar = asset(&bar_key);
    const float bar_x = left + (side ? 24.0f : 32.0f), bar_y = top + 16.0f;
    const unsigned max = entry(side)->record.max_hp ? entry(side)->record.max_hp : 1;
    const unsigned hp = (unsigned)(b.shown_hp[side] + 0.5f);
    unsigned filled = hp * 48 / max;
    if (hp && !filled) filled = 1;
    const unsigned percent = hp * 100 / max;
    const unsigned base = percent > 50 ? HPBAR_TILE_GREEN : percent > 20 ? HPBAR_TILE_YELLOW : HPBAR_TILE_RED;
    if (bar) {
        for (unsigned t = 0; t < 8; ++t) {
            unsigned tile = t < 2 ? HPBAR_TILE_H + t : base + (filled > (t - 2) * 8 ? (filled - (t - 2) * 8 > 8 ? 8 : filled - (t - 2) * 8) : 0);
            render_texture_part((GXTexObj *)&bar->texture, bar->width, bar->height, tile * 8, 0, 8, 8, tx(bar_x + t * 8),
                                ty(bar_y), 8 * SCALE, 8 * SCALE, WHITE);
        }
    }
    if (side == 0) {  // PrintHpOnHealthbox(..., 0, 16): "cur/max" right-aligned at x = 96, y = 21
        const float right = left + 96, y = top + 21;
        const AssetKey dkey = {ASSET_BATTLE_DIGITS, 0, 0, 0};
        const CacheEntry *digits = asset(&dkey);
        const float max_w = number_width(max), slash_w = b.digits_measured ? b.digit_width[10] : 6;
        draw_number(max, right, y);
        if (digits)
            render_texture_part((GXTexObj *)&digits->texture, digits->width, digits->height, 80, 0, 8, 16,
                                tx(right - max_w - slash_w), ty(y), 8 * SCALE, 16 * SCALE, WHITE);
        draw_number(hp, right - max_w - slash_w, y);
    }
}

static void draw_message(void) {
    if (!b.message_set) return;
    const CacheEntry *e = asset(&b.message);
    if (e) draw_gba(e, 16, 120);  // B_WIN_MSG: tilemap (2, 15)
    else if (b.message_fallback) render_text(tx(16), ty(122), 1.5f, WHITE, "%s", b.message_fallback);
}

void battle_view_draw(void) {
    render_begin();
    render_rect(0, 0, 640, 480, RGBA(0, 0, 0, 255));
    if (b.state >= B_START) {
        const AssetKey bg_key = {ASSET_BATTLE_BG, b.request.environment, 0, 0};
        const AssetKey box_key = {ASSET_BATTLE_TEXTBOX, 0, 0, 0};
        const CacheEntry *bg = asset(&bg_key);
        if (bg) draw_gba(bg, 0, 0);
        else render_rect(tx(0), ty(0), tx(240), 160 * SCALE, RGBA(40, 70, 60, 255));
        draw_actor(1);
        draw_actor(0);
        draw_healthbox(1);
        draw_healthbox(0);
        const CacheEntry *box = asset(&box_key);
        if (box) draw_gba(box, 0, 112);
        else render_rect(tx(0), ty(112), tx(240), 48 * SCALE, RGBA(30, 40, 64, 255));
        draw_message();
        if (b.fade > 0) render_rect(0, 0, 640, 480, RGBA(0, 0, 0, (unsigned)(b.fade * 255)));
        if (b.state == B_WAIT_EXIT && b.init_error != BI_OK)
            render_text(16, 8, 1, WHITE, "Battle cannot run: %s", battle_init_error_name(b.init_error));
    } else {
        render_text(24, 220, 1.5f, WHITE, "%s", b.status ? b.status : "");
        if (b.state == B_ASSETS) render_text(24, 250, 1, DIM, "%u / %u", b.plan_index, b.plan_count);
    }
    (void)kFallbackY;
    render_end();
}
