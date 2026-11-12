#ifndef GC_BATTLE_PRESENTATION_H
#define GC_BATTLE_PRESENTATION_H

#include "layout.h"

// The host owns the Battle. The scene reads it and plays a snapshot of the
// ordered events; it never writes battle state, handles input or performs I/O.
void battle_scene_reset(void);
bool battle_scene_begin(uint32_t build, const Battle *battle);
void battle_scene_show_target(Position pos);
void battle_scene_choose(unsigned turn, Position user);
void battle_scene_choose_reserve(unsigned turn, Position user);
void battle_scene_play(const EventLog *log);
bool battle_scene_tick(void);
void battle_scene_draw(void);
#endif
