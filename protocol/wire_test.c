#include "wire.h"
#include <assert.h>
#include <string.h>

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
    return 0;
}
