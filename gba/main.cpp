#include <gba.h>
#include <cstdio>
#include <cstring>
#include "joybus.hpp"
#include "round_trip.h"
#include "wire.h"
#include "bulk.h"

namespace {
Parser parser{};
Frame previous{}, cached{};
uint32_t bulk_size, bulk_offset, bulk_crc;
uint32_t resets, overflows, crc_errors, duplicates;
bool connected;
unsigned last_activity;
volatile unsigned frames;

void vblank() { ++frames; }

void send_frame(const Frame &f) {
    uint32_t words[WIRE_WORDS];
    const unsigned n = wire_encode(&f, words);
    if (!joybus::send(words, n)) ++overflows;
}

void nack(uint32_t seq) {
    Frame f{};
    f.type = NACK;
    f.seq = seq;
    send_frame(f);
}

bool handle_bulk(const Frame &request, Frame &reply) {
    switch (request.type) {
    case BULK_BEGIN:
        if (request.len != 4 || bulk_size) return false;
        bulk_size = wire_get32(request.payload);
        if (bulk_size != 1024 && bulk_size != 4096 && bulk_size != 16384 && bulk_size != 65536) {
            bulk_size = 0;
            return false;
        }
        bulk_offset = 0;
        bulk_crc = UINT32_MAX;
        return true;
    case BULK_DATA: {
        if (request.len != 8 || !bulk_size || wire_get32(request.payload) != bulk_offset) return false;
        const uint32_t length = wire_get32(request.payload+4);
        if (!length || length > WIRE_PAYLOAD || length > bulk_size - bulk_offset) return false;
        reply.len = (uint16_t)length;
        for (unsigned i=0; i<length; ++i) reply.payload[i] = bulk_pattern(bulk_offset+i);
        bulk_offset += length;
        bulk_crc = wire_crc(bulk_crc, reply.payload, length);
        return true;
    }
    case BULK_END:
        if (request.len != 0 || !bulk_size || bulk_offset != bulk_size) return false;
        reply.len = 4;
        wire_put32(reply.payload, ~bulk_crc);
        bulk_size = 0;
        return true;
    default:
        return false;
    }
}

bool handle(const Frame &request, Frame &reply) {
    if (request.type >= BULK_BEGIN) return handle_bulk(request, reply);
    if (request.type == HELLO && request.len == 0 && request.seq == 1) return true;
    if (request.type != PING || request.len != 4) return false;
    reply.len = 20;
    std::memcpy(reply.payload, request.payload, 4);
    wire_put32(reply.payload+4, resets);
    wire_put32(reply.payload+8, overflows);
    wire_put32(reply.payload+12, crc_errors);
    wire_put32(reply.payload+16, duplicates);
    return true;
}

void request_frame(const Frame &request) {
    if (previous.seq && request.seq == previous.seq) {
        if (request.type != previous.type || request.len != previous.len ||
            std::memcmp(request.payload, previous.payload, request.len) != 0) {
            nack(request.seq);
            return;
        }
        ++duplicates;
        send_frame(cached);
        return;
    }
    if (request.seq != previous.seq + 1 || (!previous.seq && request.type != HELLO)) {
        nack(request.seq);
        return;
    }
    Frame reply{};
    reply.seq = request.seq;
    reply.type = ACK;
    if (!handle(request, reply)) { nack(request.seq); return; }
    previous = request;
    cached = reply;
    send_frame(reply);
}

void waiting() {
    parser = {};
    previous = {};
    bulk_size = 0;
    connected = false;
    std::puts("\x1b[2JWaiting for GameCube");
}
}

int main() {
    irqInit();
    irqSet(IRQ_VBLANK, vblank);
    irqEnable(IRQ_VBLANK);
    consoleDemoInit();
    joybus::init();
    waiting();
    for (;;) {
        if (joybus::reset_seen()) { ++resets; waiting(); }
        if (joybus::overflow_seen()) { ++overflows; parser = {}; }
        if (connected && frames - last_activity >= 120) waiting();
        uint32_t word;
        if (!joybus::read(word)) continue;
        last_activity = frames;
        if (!previous.seq && !parser.count && word == RAW_DROP) {
            std::puts("\x1b[2JLink offline for test");
            joybus::disconnect();
            waiting();
        } else if (!previous.seq && !parser.count && word == RAW_PROBE) {
            const uint32_t reply[] = {RAW_VALUES, 0, UINT32_MAX, RAW_PATTERN};
            joybus::send(reply, 4);
        } else if (!previous.seq && !parser.count && word == RAW_ACK) {
            const uint32_t confirm = RAW_CONFIRM;
            joybus::send(&confirm, 1);
            connected = true;
            std::puts("\x1b[2JGC CONNECTED");
        } else {
            Frame request{};
            switch (wire_feed(&parser, word, &request)) {
            case FRAME_OK: request_frame(request); break;
            case BAD_CRC: ++crc_errors; nack(0); break;
            case BAD_HEADER: nack(0); break;
            default: break;
            }
        }
    }
}
