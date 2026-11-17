#include "wire.h"
#include <assert.h>
#include <string.h>


// Feeds words and reports how many complete good frames came out and how many
// failed candidates (BAD_CRC / BAD_HEADER) were reported on the way.
static unsigned feed(Parser *p, const uint32_t *words, unsigned n, Frame *out, unsigned *failures) {
    unsigned ok = 0;
    for (unsigned i = 0; i < n; ++i) {
        const enum ParseResult r = wire_feed(p, words[i], out);
        ok += r == FRAME_OK;
        *failures += r == BAD_CRC || r == BAD_HEADER;
    }
    return ok;
}

// Audit J-01: a request that lost one word leaves a partial frame in the
// receiver; the start of the retransmission must not be swallowed by it.
static void test_resync_after_lost_word(void) {
    static const unsigned lengths[] = {0, 3, 4, 20, 36, WIRE_PAYLOAD};
    for (unsigned li = 0; li < sizeof(lengths) / sizeof(*lengths); ++li) {
        Frame in = {.seq = 21, .len = (uint16_t)lengths[li], .type = ASSET_INFO}, out;
        for (unsigned i = 0; i < in.len; ++i) in.payload[i] = (uint8_t)(i * 7 + 1);
        uint32_t words[WIRE_WORDS], broken[WIRE_WORDS + 1];
        const unsigned n = wire_encode(&in, words);
        for (unsigned drop = 0; drop < n; ++drop) {
            Parser p = {0};
            unsigned failures = 0, m = 0;
            for (unsigned i = 0; i < n; ++i)
                if (i != drop) broken[m++] = words[i];
            assert(feed(&p, broken, m, &out, &failures) == 0);
            // The retransmission is recognised at once, whatever word was lost.
            memset(&out, 0, sizeof(out));
            assert(feed(&p, words, n, &out, &failures) == 1);
            assert(out.seq == in.seq && out.len == in.len && !memcmp(out.payload, in.payload, in.len));
            assert(p.count == 0);
        }
        for (unsigned dup = 0; dup < n; ++dup) {       // one word received twice
            Parser p = {0};
            unsigned failures = 0, m = 0;
            for (unsigned i = 0; i < n; ++i) {
                broken[m++] = words[i];
                if (i == dup) broken[m++] = words[i];
            }
            const unsigned got = feed(&p, broken, m, &out, &failures);
            assert(got <= 1);
            if (got) assert(out.seq == in.seq && out.len == in.len && !memcmp(out.payload, in.payload, in.len));
            memset(&out, 0, sizeof(out));
            const unsigned again = feed(&p, words, n, &out, &failures);
            assert(again == 1 && out.seq == in.seq && !memcmp(out.payload, in.payload, in.len));
        }
    }
    // A payload that contains the magic value: a resync never starts a false
    // frame that passes, and the real frame still parses whole.
    Frame in = {.seq = 9, .len = 16, .type = APP}, out;
    wire_put32(in.payload, WIRE_MAGIC);
    wire_put32(in.payload + 4, (uint32_t)16 << 16 | (uint32_t)APP << 8 | WIRE_VERSION);
    wire_put32(in.payload + 8, 9);
    uint32_t words[WIRE_WORDS];
    const unsigned n = wire_encode(&in, words);
    Parser p = {0};
    unsigned failures = 0;
    assert(feed(&p, words, n, &out, &failures) == 1 && !failures && !memcmp(out.payload, in.payload, 16));
    assert(feed(&p, words + 1, n - 1, &out, &failures) == 0);   // magic lost: the payload magic is a candidate
    assert(feed(&p, words, n, &out, &failures) == 1 && out.seq == 9 && !memcmp(out.payload, in.payload, 16));
}

int main(void) {
    assert(~wire_crc(UINT32_MAX, (const uint8_t *)"123456789", 9) == UINT32_C(0xcbf43926));
    Frame input = {.seq=UINT32_C(0x1234a5c7), .len=WIRE_PAYLOAD, .type=PING};
    Frame output = {0};
    Parser parser = {0};
    uint32_t words[WIRE_WORDS];
    for (unsigned i = 0; i < input.len; ++i) input.payload[i] = (uint8_t)i;
    for (unsigned len = 0; len <= WIRE_PAYLOAD; ++len) {
        input.len = (uint16_t)len;
        const unsigned n = wire_encode(&input, words);
        assert(n == 4 + (len+3)/4 && words[0] == WIRE_MAGIC && words[2] == input.seq);
        assert(wire_feed(&parser, 0, &output) == INCOMPLETE);
        for (unsigned i = 0; i < n; ++i)
            assert(wire_feed(&parser, words[i], &output) == (i+1 == n ? FRAME_OK : INCOMPLETE));
        assert(output.seq == input.seq && output.len == input.len && output.type == PING);
        assert(memcmp(input.payload, output.payload, len) == 0);
        words[n-1] ^= 1;
        for (unsigned i = 0; i < n; ++i)
            assert(wire_feed(&parser, words[i], &output) == (i+1 == n ? BAD_CRC : INCOMPLETE));
    }
    assert(wire_feed(&parser, WIRE_MAGIC, &output) == INCOMPLETE);
    assert(wire_feed(&parser, UINT32_MAX, &output) == BAD_HEADER);
    assert(parser.count == 0);
    test_resync_after_lost_word();
    return 0;
}
