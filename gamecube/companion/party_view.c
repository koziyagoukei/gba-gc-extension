#include "party_view.h"
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include <string.h>
#include "cache.h"
#include "host.h"
#include "pkmn_protocol.h"
#include "raw_link.h"
#include "render.h"

#define MAX_PARTY 6
#define MAX_PLAN (MAX_PARTY * 9)

enum ViewState { V_PROBE, V_SESSION, V_PARTY, V_ASSETS, V_ACTIVE, V_ENDED };
static const char *const state_names[] = {"PROBE", "SESSION", "PARTY", "ASSETS", "ACTIVE", "ENDED"};

static struct {
    enum ViewState state;
    HostLink link;
    unsigned probe_wait, sessions;
    PartyUi ui;
    unsigned count, record_index;
    PokemonRecord records[MAX_PARTY];
    bool have_record[MAX_PARTY];
    AssetKey plan[MAX_PLAN];
    unsigned plan_count, plan_index, fetched, hits, missing;
    uint32_t fetched_bytes;
    uint64_t party_started, assets_started, ui_changed_at;
    uint32_t party_us, assets_us;
    const char *message;
    bool fetching;
    uint64_t silent_until;
} v;
static AssetFetch fetch;

static uint32_t build_id(void) { return v.link.client.build_id; }
static uint32_t us_since(uint64_t t) { return (uint32_t)ticks_to_microsecs(gettime() - t); }

static void set_state(enum ViewState state, const char *message) {
    if (state != v.state) SYS_Report("PH2 STATE %s -> %s%s%s\n", state_names[v.state], state_names[state],
                                     message ? " : " : "", message ? message : "");
    v.state = state;
    if (message) v.message = message;
}

static void lost(const char *why) {
    SYS_Report("PH2 LINK LOST during %s (%s) seq=%u retries=%u timeouts=%u nacks=%u\n", state_names[v.state], why,
               v.link.session.seq, v.link.session.stats.retries, v.link.session.stats.timeouts,
               v.link.session.stats.nacks);
    v.fetching = false;
    v.probe_wait = 0;
    set_state(V_PROBE, "Link lost - reconnecting");
}

static bool app(uint16_t op, const uint8_t *args, unsigned length, const uint8_t **data, unsigned *data_length) {
    uint16_t status;
    if (!host_app(&v.link, op, args, length, data, data_length, &status)) return false;
    if (status != APP_OK) SYS_Report("PH2 APP op=%u status=%u\n", op, status);
    return status == APP_OK;
}

static bool set_phase(uint8_t phase, unsigned done, unsigned total) {
    const uint8_t args[3] = {phase, (uint8_t)done, (uint8_t)total};
    return app(PKMN_SET_PHASE, args, 3, NULL, NULL);
}

static bool log_digest(const char *when) {
    const uint8_t *d;
    unsigned n;
    if (!app(PKMN_STATE_DIGEST, NULL, 0, &d, &n) || n < STATE_DIGEST_BYTES) return false;
    SYS_Report("PH2 DIGEST %s party=%08x bag=%08x flags=%08x vars=%08x sb1=%08x match=%02x%s\n", when,
               wire_get32(d), wire_get32(d + 4), wire_get32(d + 8), wire_get32(d + 12), wire_get32(d + 16), d[20],
               d[20] == STATE_DIGEST_ALL_MATCH ? " UNCHANGED" : " CHANGED");
    return true;
}

static AssetKey label(uint32_t kind, uint32_t value) {
    return (AssetKey){ASSET_TEXT_LABEL, LABEL_ID(kind, value), 0, 0};
}

static void plan_add(const AssetKey *key) {
    for (unsigned i = 0; i < v.plan_count; ++i)
        if (asset_key_equal(&v.plan[i], key)) return;
    if (v.plan_count < MAX_PLAN) v.plan[v.plan_count++] = *key;
}

// Selected slot first, so the mon on screen is complete as early as possible.
static void build_plan(void) {
    v.plan_count = v.plan_index = 0;
    for (unsigned n = 0; n < v.count; ++n) {
        const unsigned slot = (v.ui.cursor + n) % v.count;
        const PokemonRecord *r = &v.records[slot];
        if (!v.have_record[slot]) continue;
        AssetKey key = r->sprite;
        plan_add(&key);
        key = pkmn_nickname_label(r);
        plan_add(&key);
        key = label(LABEL_SPECIES, r->species);
        plan_add(&key);
        for (unsigned m = 0; m < 4; ++m)
            if (r->moves[m]) { key = label(LABEL_MOVE, r->moves[m]); plan_add(&key); }
        key = label(LABEL_ABILITY, r->ability);
        plan_add(&key);
        if (r->held_item) { key = label(LABEL_ITEM, r->held_item); plan_add(&key); }
    }
}

static void log_record(const PokemonRecord *r) {
    char nick[3 * PKMN_NICKNAME_BYTES + 1] = "";
    for (unsigned i = 0; i < r->nickname_length; ++i) sprintf(nick + 2 * i, "%02x", r->nickname[i]);
    SYS_Report("PH2 RECORD slot=%u species=%u dex=%u form=%u lv=%u hp=%u/%u atk=%u def=%u spe=%u spa=%u spd=%u "
               "moves=%u,%u,%u,%u pp=%u/%u,%u/%u,%u/%u,%u/%u ability=%u(slot %u) item=%u status=%08x nature=%u "
               "tera=%u gender=%u flags=%x pid=%08x nick=%s sprite=%04x:%08x:%08x:%08x\n",
               r->slot, r->species, r->national_dex, r->form, r->level, r->hp, r->max_hp, r->attack, r->defense,
               r->speed, r->sp_attack, r->sp_defense, r->moves[0], r->moves[1], r->moves[2], r->moves[3],
               r->pp[0], r->max_pp[0], r->pp[1], r->max_pp[1], r->pp[2], r->max_pp[2], r->pp[3], r->max_pp[3],
               r->ability, r->ability_slot, r->held_item, r->status, r->nature, r->tera_type, r->gender, r->flags,
               r->personality, nick, r->sprite.type, r->sprite.id, r->sprite.variant, r->sprite.extra);
}

static void step_session(void) {
    const uint8_t *d;
    unsigned n;
    if (!host_open(&v.link)) { lost("HELLO/CAPS"); return; }
    if (v.link.client.peer_type != PEER_POKEEMERALD_EXPANSION || v.link.client.app_id != APP_POKEMON ||
        v.link.client.app_version != PKMN_APP_VERSION || !(v.link.client.caps & CAP_ASSETS)) {
        set_state(V_ENDED, "Unsupported client");
        return;
    }
    ++v.sessions;
    if (!app(PKMN_PARTY_INFO, NULL, 0, &d, &n) || !party_ui_decode(d, n, true, &v.ui)) { lost("PARTY_INFO"); return; }
    if (v.ui.record_version != PKMN_RECORD_VERSION || v.ui.record_length != PKMN_RECORD_BYTES) {
        set_state(V_ENDED, "Unsupported record version");
        return;
    }
    v.count = v.ui.count > MAX_PARTY ? MAX_PARTY : v.ui.count;
    if (v.ui.cursor >= v.count) v.ui.cursor = 0;
    SYS_Report("PH2 SESSION %u party=%u cursor=%u\n", v.sessions, v.count, v.ui.cursor);
    if (!log_digest("start")) { lost("DIGEST"); return; }
    memset(v.have_record, 0, sizeof(v.have_record));
    v.record_index = 0;
    v.party_started = gettime();
    set_state(V_PARTY, "Receiving party");
}

static void step_party(void) {
    if (v.record_index < v.count) {
        const uint8_t slot = (uint8_t)v.record_index, *d;
        unsigned n;
        if (!set_phase(PHASE_PARTY, v.record_index, v.count)) { lost("SET_PHASE"); return; }
        if (!app(PKMN_RECORD, &slot, 1, &d, &n)) { lost("RECORD"); return; }
        if (pkmn_record_decode(d, n, &v.records[slot]) && v.records[slot].slot == slot) {
            v.have_record[slot] = true;
            log_record(&v.records[slot]);
        } else {
            SYS_Report("PH2 RECORD slot=%u rejected (length %u)\n", slot, n);
        }
        ++v.record_index;
        return;
    }
    v.party_us = us_since(v.party_started);
    SYS_Report("PH2 PARTY DONE records=%u bytes=%u us=%u\n", v.count, v.count * PKMN_RECORD_BYTES, v.party_us);
    build_plan();
    v.fetched = v.hits = v.missing = 0;
    v.fetched_bytes = 0;
    v.assets_started = gettime();
    if (!set_phase(PHASE_ASSETS, 0, v.plan_count)) { lost("SET_PHASE"); return; }
    set_state(V_ASSETS, "Receiving assets");
}

static void step_assets(void) {
    while (!v.fetching && v.plan_index < v.plan_count) {
        const AssetKey *key = &v.plan[v.plan_index];
        if (cache_find(build_id(), key)) {
            cache_count_hit();
            ++v.hits;
            ++v.plan_index;
            continue;
        }
        fetch_start(&fetch, key);
        v.fetching = true;
    }
    if (v.fetching) {
        switch (fetch_step(&v.link, &fetch)) {
        case FETCH_PENDING:
            return;
        case FETCH_DONE: {
            const CacheEntry *e = cache_insert(build_id(), &fetch.info, fetch.data);
            SYS_Report("PH2 ASSET type=%04x id=%08x var=%08x extra=%08x %ux%u bytes=%u crc=%08x us=%u visible=%u %s\n",
                       fetch.key.type, fetch.key.id, fetch.key.variant, fetch.key.extra, fetch.info.width,
                       fetch.info.height, fetch.info.length, fetch.info.crc, fetch_elapsed_us(&fetch),
                       e ? e->visible : 0, e ? "cached" : "CACHE FULL");
            ++v.fetched;
            v.fetched_bytes += fetch.info.length;
            break;
        }
        case FETCH_MISSING:
            SYS_Report("PH2 ASSET type=%04x id=%08x missing status=%u\n", fetch.key.type, fetch.key.id,
                       fetch.info.status);
            ++v.missing;
            break;
        case FETCH_FAILED:
            lost("ASSET");
            return;
        }
        v.fetching = false;
        ++v.plan_index;
        if (!set_phase(PHASE_ASSETS, v.plan_index, v.plan_count)) { lost("SET_PHASE"); return; }
        return;
    }
    v.assets_us = us_since(v.assets_started);
    const CacheStats c = cache_stats();
    SYS_Report("PH2 READY session=%u assets=%u fetched=%u hits=%u missing=%u bytes=%u us=%u cache_entries=%u "
               "cache_arena=%u cache_source=%u\n", v.sessions, v.plan_count, v.fetched, v.hits, v.missing,
               v.fetched_bytes, v.assets_us, c.entries, c.arena_used, c.source_bytes);
    if (!set_phase(PHASE_READY, v.plan_count, v.plan_count)) { lost("SET_PHASE"); return; }
    v.ui_changed_at = gettime();
    set_state(V_ACTIVE, NULL);
}

static void end_session(uint16_t reason, const char *message) {
    if (!log_digest("end") || !host_bye(&v.link, reason)) SYS_Report("PH2 BYE not acknowledged\n");
    else SYS_Report("PH2 BYE reason=%u\n", reason);
    set_state(V_ENDED, message);
}

static void step_active(bool start_pressed) {
    const uint8_t *d;
    unsigned n;
    PartyUi ui;
    if (start_pressed) { end_session(BYE_HOST_EXIT, "Session ended from the GameCube"); return; }
    if (!app(PKMN_UI_STATE, NULL, 0, &d, &n) || !party_ui_decode(d, n, false, &ui)) { lost("UI_STATE"); return; }
    if (ui.cursor >= v.count) ui.cursor = 0;
    if (ui.cursor != v.ui.cursor || ui.confirmed != v.ui.confirmed || ui.screen != v.ui.screen ||
        ui.input_seq != v.ui.input_seq || ui.flags != v.ui.flags) {
        SYS_Report("PH2 UI cursor=%u confirmed=%u screen=%u seq=%u flags=%x since_last_us=%u\n", ui.cursor,
                   ui.confirmed, ui.screen, ui.input_seq, ui.flags, us_since(v.ui_changed_at));
        v.ui_changed_at = gettime();
    }
    v.ui.cursor = ui.cursor;
    v.ui.confirmed = ui.confirmed;
    v.ui.screen = ui.screen;
    v.ui.input_seq = ui.input_seq;
    v.ui.flags = ui.flags;
    if (ui.flags & UI_EXIT_REQUESTED) end_session(BYE_CLIENT_EXIT, "The GBA closed the session");
}

void party_view_init(void) {
    memset(&v, 0, sizeof(v));
    v.message = "Waiting for GBA (GC Extension Mode)";
    SYS_Report("PH2 companion host boot build=%08x\n", HOST_BUILD_ID);
}

void party_view_update(bool start_pressed, bool a_pressed, bool z_pressed) {
    if (z_pressed && v.state == V_ACTIVE) {
        // Test hook: stop talking to the GBA for 3 s (> its 2 s silence timeout).
        v.silent_until = gettime() + secs_to_ticks(3);
        SYS_Report("PH2 TEST host silent for 3 s\n");
    }
    if (v.silent_until) {
        if (gettime() < v.silent_until) return;
        v.silent_until = 0;
    }
    switch (v.state) {
    case V_PROBE: {
        uint32_t words[3];
        if (v.probe_wait) { --v.probe_wait; return; }
        if (raw_round_trip(words)) {
            SYS_Report("PH2 RAW OK %08x %08x %08x\n", words[0], words[1], words[2]);
            set_state(V_SESSION, "GBA connected");
        } else {
            v.probe_wait = 20;
        }
        break;
    }
    case V_SESSION: step_session(); break;
    case V_PARTY: step_party(); break;
    case V_ASSETS: step_assets(); break;
    case V_ACTIVE: step_active(start_pressed); break;
    case V_ENDED:
        if (a_pressed) { v.probe_wait = 0; set_state(V_PROBE, "Waiting for GBA (GC Extension Mode)"); }
        break;
    }
}

// ------------------------------------------------------------------ drawing

#define WHITE RGBA(255, 255, 255, 255)
#define DIM RGBA(150, 160, 185, 255)
#define ACCENT RGBA(255, 214, 90, 255)
#define PANEL RGBA(30, 40, 64, 255)
#define HILITE RGBA(70, 96, 160, 255)

static const CacheEntry *asset(const AssetKey *key) { return cache_find(build_id(), key); }

static float draw_label(const AssetKey *key, float x, float y, float scale, const char *fallback) {
    const CacheEntry *e = asset(key);
    if (e) {
        render_texture((GXTexObj *)&e->texture, x, y, e->width * scale, e->height * scale, WHITE);
        return e->width * scale;
    }
    render_text(x, y, scale, DIM, "%s", fallback);
    return 8 * scale * strlen(fallback);
}

static void status_text(uint32_t status, char *out) {
    if (!status) strcpy(out, "OK");
    else if (status & 7) strcpy(out, "SLP");
    else if (status & (1 << 7)) strcpy(out, "TOX");
    else if (status & (1 << 3)) strcpy(out, "PSN");
    else if (status & (1 << 4)) strcpy(out, "BRN");
    else if (status & (1 << 5)) strcpy(out, "FRZ");
    else if (status & (1 << 6)) strcpy(out, "PAR");
    else if (status & (1 << 12)) strcpy(out, "FRB");
    else sprintf(out, "%08x", status);
}

static void draw_detail(const PokemonRecord *r) {
    const float px = 256, py = 64;
    char status[16];
    render_rect(px, py, 368, 388, PANEL);
    const CacheEntry *sprite = asset(&r->sprite);
    if (sprite) render_texture((GXTexObj *)&sprite->texture, px + 8, py + 8, 192, 192, WHITE);
    else render_text(px + 48, py + 96, 1, DIM, "loading sprite");
    AssetKey key = pkmn_nickname_label(r);
    draw_label(&key, px + 208, py + 12, 2, "?");
    if (r->flags & PKMN_FLAG_EGG) {
        // The game does not reveal an egg's contents; neither does the TV.
        render_text(px + 208, py + 60, 1.5f, WHITE, "EGG");
        return;
    }
    key = label(LABEL_SPECIES, r->species);
    draw_label(&key, px + 208, py + 52, 1.5f, "?");
    render_text(px + 208, py + 84, 1, DIM, "No.%04u  form %u", r->national_dex, r->form);
    render_text(px + 208, py + 104, 1.5f, WHITE, "Lv.%u %c%s", r->level,
                r->gender == 0 ? 0x0b : r->gender == 1 ? 0x0c : ' ', r->flags & PKMN_FLAG_SHINY ? " SHINY" : "");
    status_text(r->status, status);
    render_text(px + 208, py + 136, 1, WHITE, "HP %u/%u  %s", r->hp, r->max_hp, status);
    const float bar = r->max_hp ? 144.0f * r->hp / r->max_hp : 0;
    render_rect(px + 208, py + 156, 144, 6, RGBA(20, 24, 36, 255));
    render_rect(px + 208, py + 156, bar, 6, r->hp * 5 > r->max_hp ? RGBA(90, 220, 110, 255) : RGBA(240, 80, 70, 255));
    render_text(px + 208, py + 170, 1, DIM, "ATK %u DEF %u", r->attack, r->defense);
    render_text(px + 208, py + 188, 1, DIM, "SPA %u SPD %u", r->sp_attack, r->sp_defense);
    render_text(px + 208, py + 206, 1, DIM, "SPE %u  NAT %u", r->speed, r->nature);
    for (unsigned m = 0; m < 4; ++m) {
        const float y = py + 224 + m * 28;
        if (!r->moves[m]) { render_text(px + 16, y + 4, 1, DIM, "-"); continue; }
        key = label(LABEL_MOVE, r->moves[m]);
        draw_label(&key, px + 16, y, 1.5f, "move");
        render_text(px + 248, y + 4, 1, WHITE, "PP %2u/%2u", r->pp[m], r->max_pp[m]);
    }
    render_text(px + 16, py + 344, 1, DIM, "ABILITY");
    key = label(LABEL_ABILITY, r->ability);
    draw_label(&key, px + 80, py + 340, 1.5f, "?");
    render_text(px + 16, py + 366, 1, DIM, "ITEM");
    if (r->held_item) {
        key = label(LABEL_ITEM, r->held_item);
        draw_label(&key, px + 80, py + 362, 1.5f, "?");
    } else {
        render_text(px + 80, py + 366, 1, WHITE, "none");
    }
}

static void draw_party(void) {
    for (unsigned slot = 0; slot < v.count; ++slot) {
        const float y = 64 + slot * 64;
        const PokemonRecord *r = &v.records[slot];
        render_rect(16, y, 228, 60, slot == v.ui.cursor ? HILITE : PANEL);
        if (!v.have_record[slot]) continue;
        const CacheEntry *sprite = asset(&r->sprite);
        if (sprite) render_texture((GXTexObj *)&sprite->texture, 18, y - 1, 64, 64, WHITE);
        AssetKey key = pkmn_nickname_label(r);
        draw_label(&key, 86, y + 6, 1.5f, "...");
        if (r->flags & PKMN_FLAG_EGG) render_text(86, y + 36, 1, DIM, "EGG");
        else render_text(86, y + 36, 1, slot == v.ui.cursor ? WHITE : DIM, "Lv.%u  %u/%u", r->level, r->hp, r->max_hp);
        if (slot == v.ui.confirmed) render_text(212, y + 6, 1, ACCENT, "SEL");
    }
}

void party_view_draw(void) {
    render_begin();
    render_rect(0, 0, 640, 44, RGBA(22, 28, 48, 255));
    render_text(16, 10, 1.5f, WHITE, "GBA-GC EXTENSION");
    render_text(300, 14, 1, DIM, "%s", state_names[v.state]);
    if (v.state >= V_PARTY && v.state <= V_ACTIVE)
        render_text(400, 14, 1, DIM, "%.4s build %08x", (const char *)v.link.client.tag, v.link.client.build_id);
    const bool have_party = v.count && (v.state == V_ACTIVE || v.state == V_ASSETS || v.have_record[0]);
    if (have_party) {
        draw_party();
        unsigned slot = v.ui.cursor < v.count ? v.ui.cursor : 0;
        if (v.have_record[slot]) draw_detail(&v.records[slot]);
    }
    if (v.state != V_ACTIVE) {
        const float y = have_party ? 440 : 220;
        render_rect(0, y - 8, 640, 36, RGBA(10, 12, 20, 230));
        if (v.state == V_ASSETS)
            render_text(24, y, 1, ACCENT, "%s  %u/%u", v.message, v.plan_index, v.plan_count);
        else if (v.state == V_PARTY)
            render_text(24, y, 1, ACCENT, "%s  %u/%u", v.message, v.record_index, v.count);
        else
            render_text(24, y, 1, ACCENT, "%s%s", v.message, v.state == V_ENDED ? "  (A: reconnect)" : "");
    }
    const CacheStats c = cache_stats();
    render_text(16, 458, 1, DIM, "cache %u assets %u KiB  hits %u", c.entries, (unsigned)(c.arena_used / 1024),
                c.hits);
    if (v.state == V_ACTIVE) render_text(352, 458, 1, DIM, "START: end  Z: 3s silence test");
    render_end();
}
