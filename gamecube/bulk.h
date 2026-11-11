#ifndef GC_BULK_H
#define GC_BULK_H
#include "session.h"
#include "../protocol/bulk.h"

bool fetch_sprite(Session *session, unsigned gap, uint8_t bytes[SPRITE_BYTES]);
bool bulk_test(Session *session, unsigned gap);
#endif
