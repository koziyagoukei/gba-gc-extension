#ifndef COMPANION_BATTLE_VIEW_H
#define COMPANION_BATTLE_VIEW_H
#include <stdbool.h>
#include "host.h"

// GC_MODE_BATTLE host: downloads the GcBattleRequestV1, the sprites and
// labels of both battlers, runs the battle core with the GBA as command
// input, renders the battle and returns a GcBattleResultV1.
typedef enum { BV_RUNNING, BV_ENDED, BV_LOST } BattleViewStatus;

void battle_view_begin(HostLink *link, const ModeInfo *mode);
BattleViewStatus battle_view_update(bool start_pressed, const char **message);
void battle_view_draw(void);
#endif
