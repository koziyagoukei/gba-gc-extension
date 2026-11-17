// Native test of the GBA connector's RESULT handling (src/gc_companion_protocol.c
// of a pokeemerald-expansion / Frontier checkout), driven through its frame
// handler exactly as the JoyBus responder calls it. Build: tools/connector-test.sh.
#include "global.h"
#include "gc_companion_link.h"
#include "gc_companion_protocol.h"
#include "gc_ext/pkmn_protocol.h"
#include <assert.h>
#include <stdio.h>

#ifndef MODE_FLAG_RESULT_FINAL  // lets the test build against connectors that predate the flag
#define MODE_FLAG_RESULT_FINAL (1u << 0)
#endif

static struct GcLinkStats stats;
const struct GcLinkStats *GcLink_GetStats(void) { return &stats; }

static unsigned calls;
static uint16_t next_status;
static uint8_t last_payload[8];
static uint16_t on_result(uint16_t mode_id, uint16_t version, const uint8_t *data, uint16_t length) {
    (void)mode_id;
    (void)version;
    ++calls;
    memcpy(last_payload, data, length < 8 ? length : 8);
    return next_status;
}

static struct GcClientConfig config = {
    .peerType = PEER_POKEEMERALD_EXPANSION, .appId = APP_POKEMON, .appVersion = 1,
    .handleResult = on_result,
};

static Frame request, reply;
static uint32_t seq;

static bool send(uint8_t type, const uint8_t *payload, uint16_t length) {
    memset(&request, 0, sizeof(request));
    memset(&reply, 0, sizeof(reply));
    request.type = type;
    request.seq = ++seq;
    request.len = length;
    if (length) memcpy(request.payload, payload, length);
    return GcProtocol_HandleFrame(&request, &reply);
}

static void open_session(void) {
    seq = 0;
    assert(send(HELLO, NULL, 0));
    const Caps host = {.version = MSG_PROTOCOL_VERSION, .peer_type = PEER_GC_HOST, .build_id = 1,
                       .caps = CAP_APP | CAP_ASSETS | CAP_BYE | CAP_MODE | CAP_RESULT, .max_payload = WIRE_PAYLOAD,
                       .app_id = APP_POKEMON, .app_version = 1, .tag = {'T', 'E', 'S', 'T'}};
    uint8_t caps[64];
    assert(send(CAPS, caps, (uint16_t)caps_encode(&host, caps)));
}

static ModeInfo mode(void) {
    ModeInfo m;
    assert(send(MODE, NULL, 0) && mode_info_decode(reply.payload, reply.len, &m));
    return m;
}

static uint16_t result(uint16_t mode_id, uint32_t id, uint8_t marker) {
    uint8_t p[RESULT_HEADER_BYTES + 4] = {0};
    msg_put16(p, mode_id);
    msg_put16(p + 2, 1);
    wire_put32(p + 4, id);
    p[RESULT_HEADER_BYTES] = marker;
    assert(send(RESULT, p, sizeof(p)) && reply.len == 4);
    return msg_get16(reply.payload);
}

static void start_mode(uint32_t nonce) {
    GcProtocol_Init(&config);
    const ModeInfo m = {.mode_id = GC_MODE_BATTLE, .mode_version = 1, .nonce = nonce};
    GcProtocol_SetMode(&m, NULL);
    open_session();
}

int main(void) {
    const uint32_t nonce = 0x51c28542u;
    start_mode(nonce);
    assert(!(mode().flags & MODE_FLAG_RESULT_FINAL));

    // A result that does not answer this mode start is refused, unseen by the mode.
    next_status = RESULT_ACCEPTED;
    assert(result(GC_MODE_BATTLE, 0x12345678u, 1) == RESULT_REJECTED && calls == 0);
    assert(result(GC_MODE_PARTY_VIEW, nonce, 1) == RESULT_UNSUPPORTED && calls == 0);
    // A result the mode rejects (bad payload) does not settle anything.
    next_status = RESULT_REJECTED;
    assert(result(GC_MODE_BATTLE, nonce, 2) == RESULT_REJECTED && calls == 1);
    assert(!(mode().flags & MODE_FLAG_RESULT_FINAL));
    // The first accepted result is final.
    next_status = RESULT_ACCEPTED;
    assert(result(GC_MODE_BATTLE, nonce, 3) == RESULT_ACCEPTED && calls == 2 && last_payload[0] == 3);
    // Resending it is acknowledged, not re-applied.
    assert(result(GC_MODE_BATTLE, nonce, 3) == RESULT_DUPLICATE && calls == 2);
    // Another id for the same mode start cannot replace it (the audit's overwrite case).
    assert(result(GC_MODE_BATTLE, nonce ^ 1u, 4) == RESULT_REJECTED && calls == 2 && last_payload[0] == 3);
    assert(result(GC_MODE_BATTLE, nonce, 5) == RESULT_DUPLICATE && calls == 2 && last_payload[0] == 3);
    // After a reconnect the mode says its result is final, so the host cannot rerun it.
    GcProtocol_ResetSession();
    open_session();
    const ModeInfo again = mode();
    assert(again.nonce == nonce && (again.flags & MODE_FLAG_RESULT_FINAL));
    assert(result(GC_MODE_BATTLE, nonce, 6) == RESULT_DUPLICATE && calls == 2);
    // The next battle is a new mode start with a new nonce: accepted normally.
    start_mode(nonce + 7u);
    assert(!(mode().flags & MODE_FLAG_RESULT_FINAL));
    assert(result(GC_MODE_BATTLE, nonce, 7) == RESULT_REJECTED && calls == 2);   // the old battle's id
    assert(result(GC_MODE_BATTLE, nonce + 7u, 8) == RESULT_ACCEPTED && calls == 3);
    puts("connector result test OK");
    return 0;
}
