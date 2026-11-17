// Native stand-in for pokeemerald's main.h (tests/connector).
#ifndef GUARD_MAIN_H
#define GUARD_MAIN_H
#include "global.h"
typedef void (*IntrFunc)(void);
struct Main {
    u32 vblankCounter1;
    u16 intrCheck;
};
extern struct Main gMain;
extern IntrFunc gIntrTable[];
#endif
