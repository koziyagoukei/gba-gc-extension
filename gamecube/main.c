#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include "joybus.h"
#include "round_trip.h"
#include "session.h"

static bool read_marker(uint32_t marker) {
    const uint64_t deadline = gettime() + millisecs_to_ticks(350);
    do {
        uint32_t word;
        if (!joy_read(&word)) return false;
        if (word == marker) return true;
    } while (gettime() < deadline);
    return false;
}

static bool round_trip(void) {
    uint32_t words[3];
    if (!joy_reset() || !joy_write(RAW_PROBE) || !read_marker(RAW_VALUES)) return false;
    for (unsigned i = 0; i < 3; ++i)
        if (!joy_read(&words[i])) return false;
    if (words[0] != 0 || words[1] != UINT32_MAX || words[2] != RAW_PATTERN) return false;
    printf("GBA: %08x %08x %08x\n", words[0], words[1], words[2]);
    if (!joy_write(RAW_ACK) || !read_marker(RAW_CONFIRM)) return false;
    SYS_Report("POC1 PASS %08x %08x %08x\n", words[0], words[1], words[2]);
    return true;
}

static bool ping(Session *session, uint32_t pattern, Frame *reply, enum Fault fault) {
    Frame req = {.len=4, .type=PING};
    wire_put32(req.payload, pattern);
    return session_request(session, &req, reply, fault) && reply->len == 20 &&
           wire_get32(reply->payload) == pattern;
}

static bool open_session(Session *session) {
    Frame hello = {.type=HELLO}, reply;
    *session = (Session){0};
    if (!session_request(session, &hello, &reply, NO_FAULT) || reply.len != 0) return false;
    if (!ping(session, RAW_PATTERN, &reply, NO_FAULT)) return false;
    SYS_Report("POC2 HELLO/PING PASS\n");
    return true;
}

int main(void) {
    VIDEO_Init();
    GXRModeObj *mode = VIDEO_GetPreferredMode(NULL);
    void *fb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    VIDEO_Configure(mode);
    VIDEO_SetNextFramebuffer(fb);
    console_init(fb, 20, 20, mode->fbWidth-40, mode->xfbHeight-40, mode->fbWidth * 2);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    joy_init();
    joy_set_interval(POC_INTERVAL);
    puts("JoyBus PoC - GBA on port 2");
    SYS_Report("JoyBus PoC boot\n");
    bool connected = false;
    bool tested = false;
    Session session = {0};
    uint64_t check_at = 0;
    for (;;) {
        if (!connected) {
            connected = round_trip();
            if (connected) {
                puts("GC CONNECTED");
                if (!tested && joy_write(RAW_DROP)) {
                    VIDEO_WaitVSync();
                    const bool disconnected = !joy_status();
                    for (unsigned i = 0; i < 35; ++i) VIDEO_WaitVSync();
                    connected = round_trip();
                    tested = disconnected && connected;
                    SYS_Report("POC1 RESET/DISCONNECT/RECONNECT %s\n", tested ? "PASS" : "FAIL");
                }
                if (connected) connected = open_session(&session);
                check_at = gettime() + millisecs_to_ticks(250);
            }
            else for (unsigned i = 0; i < 20; ++i) VIDEO_WaitVSync();
        }
        if (connected && gettime() >= check_at) {
            Frame reply;
            connected = ping(&session, 0, &reply, NO_FAULT);
            if (!connected) puts("Link lost - retrying");
            check_at = gettime() + millisecs_to_ticks(250);
        }
        VIDEO_WaitVSync();
    }
}
