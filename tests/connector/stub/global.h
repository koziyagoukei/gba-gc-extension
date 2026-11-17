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
#endif
