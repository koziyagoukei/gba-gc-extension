#ifndef COMPANION_PARTY_VIEW_H
#define COMPANION_PARTY_VIEW_H
#include <stdbool.h>

// Pokemon host application: connects, downloads the party records and their
// assets into the cache, then mirrors the GBA party selection on the TV.
void party_view_init(void);
// One state-machine step; performs at most one or two link requests.
void party_view_update(bool start_pressed, bool a_pressed, bool z_pressed);
void party_view_draw(void);
#endif
