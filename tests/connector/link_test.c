// Native test of the GBA connector's JoyBus responder (src/gc_companion_link.c
// of a pokeemerald-expansion / Frontier checkout). The IO registers are plain
// variables; the test plays the GameCube by raising the serial IRQ for every
// word written (RECEIVE) and read (SEND), and advances the frame counter.
// Build: tools/connector-test.sh.
#include "global.h"
#include "main.h"
#include "gc_companion_link.h"
#include <assert.h>
#include <stdio.h>

struct StubIo gStubIo;
struct Main gMain;
IntrFunc gIntrTable[16];

#define JOYCNT_RECEIVE (1 << 1)
#define JOYCNT_SEND (1 << 2)

static unsigned handled;
static uint32_t handled_seq[16];
static bool ack_all(const Frame *request, Frame *reply) {
    (void)reply;
    handled_seq[handled++ % 16] = request->seq;
    return true;
}

// GC -> GBA: one word arrives in JOY_RECV and raises the RECEIVE IRQ.
static void gc_write(uint32_t word) {
    gStubIo.joy_recv_l = (uint16_t)word;
    gStubIo.joy_recv_h = (uint16_t)(word >> 16);
    gStubIo.joycnt = JOYCNT_RECEIVE;   // JOYCNT event bits are write-1-to-clear: one event per IRQ
    gIntrTable[1]();
    gStubIo.joycnt = 0;
}

// GBA -> GC: a READ takes JOY_TRANS and raises the SEND IRQ (refill).
static uint32_t gc_read(void) {
    const uint32_t word = gStubIo.joy_trans_l | (uint32_t)gStubIo.joy_trans_h << 16;
    gStubIo.joycnt = JOYCNT_SEND;
    gIntrTable[1]();
    gStubIo.joycnt = 0;
    return word;
}

// Reads what the GBA queued and returns the frames found (up to 4).
static unsigned gc_collect(Frame *frames) {
    Parser parser = {0};
    unsigned n = 0;
    gc_read();  // the first READ returns the idle register; it loads the queue head
    for (unsigned i = 0; i < 3 * WIRE_WORDS && n < 4; ++i)
        if (wire_feed(&parser, gc_read(), &frames[n]) == FRAME_OK) ++n;
    return n;
}

static void send_words(const uint32_t *words, unsigned n, int skip) {
    for (unsigned i = 0; i < n; ++i)
        if ((int)i != skip) gc_write(words[i]);
}

static unsigned encode(uint32_t seq, uint8_t type, uint16_t len, uint32_t *words) {
    Frame f = {.seq = seq, .type = type, .len = len};
    for (unsigned i = 0; i < len; ++i) f.payload[i] = (uint8_t)(seq + i);
    return wire_encode(&f, words);
}

static void frame_tick(unsigned frames) {
    gMain.vblankCounter1 += frames;
}

int main(void) {
    Frame got[4];
    uint32_t words[WIRE_WORDS];
    assert(GcLink_Open());
    gStubIo.joycnt = 0;   // Open acknowledged the pending events
    unsigned n = encode(1, HELLO, 0, words);
    send_words(words, n, -1);
    GcLink_Poll(ack_all);
    assert(handled == 1 && gc_collect(got) == 1 && got[0].type == ACK && got[0].seq == 1);

    // 1. A request loses one word (any position). The GC resends at once, before
    //    the GBA notices: the resend is recognised with no NACK in between.
    uint32_t seq = 2;
    n = encode(seq, ASSET_INFO, 20, words);
    for (unsigned lost = 0; lost < n; ++lost, ++seq) {
        n = encode(seq, ASSET_INFO, 20, words);
        const unsigned before = handled;
        send_words(words, n, (int)lost);
        GcLink_Poll(ack_all);
        send_words(words, n, -1);
        GcLink_Poll(ack_all);
        assert(handled == before + 1 && handled_seq[(handled - 1) % 16] == seq);
        // The resend is answered; a NACK may come first only if the damaged
        // copy was caught on its own (e.g. a lost header word).
        const unsigned frames = gc_collect(got);
        assert(frames >= 1 && frames <= 2 && got[frames - 1].type == ACK && got[frames - 1].seq == seq);
        if (frames == 2) assert(got[0].type == NACK && got[0].seq == 0);
        if (lost >= 2) assert(frames == 1);   // the stale partial absorbed the resend's magic before the fix
    }

    // 2. The GC waits for a reply instead: after STALE_FRAMES without a word the
    //    GBA drops the partial request and NACKs, and the resend then succeeds.
    for (unsigned lost = 2; lost < n; lost += 3, ++seq) {   // a lost magic or header word is caught at once
        n = encode(seq, ASSET_INFO, 20, words);
        const unsigned before = handled;
        const uint32_t drops = GcLink_GetStats()->staleDrops;
        send_words(words, n, (int)lost);
        GcLink_Poll(ack_all);
        frame_tick(1);
        GcLink_Poll(ack_all);
        assert(GcLink_GetStats()->staleDrops == drops);   // one quiet frame is not enough
        frame_tick(1);
        GcLink_Poll(ack_all);
        assert(GcLink_GetStats()->staleDrops == drops + 1);
        assert(gc_collect(got) == 1 && got[0].type == NACK && got[0].seq == 0);
        send_words(words, n, -1);
        GcLink_Poll(ack_all);
        assert(handled == before + 1 && gc_collect(got) == 1 && got[0].type == ACK && got[0].seq == seq);
    }

    // 3. No false drop: a request split over polls with frames in between, and a
    //    game loop that stalls while the rest of the words wait in the queue.
    n = encode(seq, APP, 200, words);
    send_words(words, n / 2, -1);
    GcLink_Poll(ack_all);
    frame_tick(1);
    for (unsigned i = n / 2; i < n; ++i) gc_write(words[i]);
    frame_tick(5);                                          // long game frame before the next poll
    const uint32_t drops = GcLink_GetStats()->staleDrops;
    GcLink_Poll(ack_all);
    assert(GcLink_GetStats()->staleDrops == drops && handled_seq[(handled - 1) % 16] == seq);
    assert(gc_collect(got) == 1 && got[0].type == ACK && got[0].seq == seq);
    ++seq;

    // 4. A corrupted request with nothing behind it is NACKed right away.
    n = encode(seq, APP, 8, words);
    words[n - 1] ^= 1;
    send_words(words, n, -1);
    GcLink_Poll(ack_all);
    assert(gc_collect(got) == 1 && got[0].type == NACK && got[0].seq == 0);
    words[n - 1] ^= 1;
    send_words(words, n, -1);
    GcLink_Poll(ack_all);
    assert(gc_collect(got) == 1 && got[0].type == ACK && got[0].seq == seq);

    const struct GcLinkStats *s = GcLink_GetStats();
    printf("connector link test OK (requests=%u crc=%u resyncs=%u stale=%u nacks=%u maxQueue=%u)\n",
           s->requests, s->crcErrors, s->resyncs, s->staleDrops, s->nacks, s->maxQueue);
    GcLink_Close();
    return 0;
}
