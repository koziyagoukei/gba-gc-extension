// Native stand-in for pokeemerald's global.h: just what the generic GC
// connector sources need to compile on a PC for tests/connector.
#ifndef GUARD_GLOBAL_H
#define GUARD_GLOBAL_H
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef uint8_t bool8;
typedef uint32_t bool32;
#define TRUE 1
#define FALSE 0
#define EWRAM_DATA
#define IWRAM_DATA
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
typedef volatile uint8_t vu8;
typedef volatile uint16_t vu16;
typedef volatile uint32_t vu32;
typedef volatile uint8_t vbool8;

// GBA IO registers as plain variables (tests/connector/link_test.c).
struct StubIo {
    uint16_t joycnt, joy_recv_l, joy_recv_h, joy_trans_l, joy_trans_h;
    uint16_t ime, ie, if_, rcnt, intr_check;
};
extern struct StubIo gStubIo;
#define REG_ADDR_JOYCNT ((uintptr_t)&gStubIo.joycnt)
#define REG_ADDR_JOY_RECV_L ((uintptr_t)&gStubIo.joy_recv_l)
#define REG_ADDR_JOY_RECV_H ((uintptr_t)&gStubIo.joy_recv_h)
#define REG_ADDR_JOY_TRANS_L ((uintptr_t)&gStubIo.joy_trans_l)
#define REG_ADDR_JOY_TRANS_H ((uintptr_t)&gStubIo.joy_trans_h)
#define REG_IME gStubIo.ime
#define REG_IE gStubIo.ie
#define REG_IF gStubIo.if_
#define REG_RCNT gStubIo.rcnt
#define INTR_CHECK gStubIo.intr_check
#define INTR_FLAG_SERIAL (1 << 7)
#endif
