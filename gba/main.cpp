#include <gba.h>
#include <cstdio>
#include <cstring>
#include "joybus.hpp"
#include "round_trip.h"
#include "wire.h"

namespace {
Parser parser{};
Frame previous{}, cached{};
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

bool handle(const Frame &request, Frame &reply) {
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
        if (!parser.count && word == RAW_DROP) {
            std::puts("\x1b[2JLink offline for test");
            joybus::disconnect();
            waiting();
        } else if (!parser.count && word == RAW_PROBE) {
            const uint32_t reply[] = {RAW_VALUES, 0, UINT32_MAX, RAW_PATTERN};
            joybus::send(reply, 4);
        } else if (!parser.count && word == RAW_ACK) {
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
