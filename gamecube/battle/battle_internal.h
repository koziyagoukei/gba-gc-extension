#ifndef GC_BATTLE_INTERNAL_H
#define GC_BATTLE_INTERNAL_H

// Shared helpers of the battle core (battle_core.c) used by the move effect
// engine (battle_effects.c). Not part of the public battle API.

#include "battle_core.h"

uint32_t bc_next_random(Battle *battle);
bool bc_conditions_enabled(const Battle *battle);
bool bc_has_type(const Battler *mon, uint8_t type);
unsigned bc_stage_scale(unsigned value, int stage, unsigned base);
uint8_t bc_status_kind(const Battle *battle, const Battler *mon);
Battler *bc_mutable_at(Battle *battle, Position position);
uint8_t bc_effectiveness(const Battle *battle, uint8_t move_type, const Battler *defender);
uint16_t bc_calculate_damage(const Battle *battle, const Battler *attacker, const BattleMove *move,
                             const Battler *defender, unsigned random_percent, bool typeless, uint8_t *effectiveness);
void bc_emit(EventLog *log, BattleEvent event);
void bc_execute_switch(Battle *battle, Position user, uint8_t index, EventKind reason, EventLog *log);
bool bc_drag_out(Battle *battle, Position target, EventLog *log);
bool bc_missed(Battle *battle, Position user, Position target, const BattleMove *move);
// A move that cannot miss for this user (Toxic from a Poison type under the rule); it also
// reaches semi-invulnerable targets.
bool bc_sure_hit(const Battle *battle, Position user, const BattleMove *move);
void bc_cure_status(Battler *mon, BattleEvent event, EventLog *log);
bool bc_immune_to_status(const BattleConditionRules *rules, const Battler *mon, uint8_t kind);
void bc_apply_damage(Battle *battle, BattleEvent event, uint16_t damage, EventLog *log);
uint8_t bc_first_target(uint8_t mask);

// Move effect engine (battle_effects.c).
// Fills move->effects from a V5-V7 effect byte so every request version runs
// through the same effect handlers.
void bc_translate_legacy_effects(BattleMove *move);
bool bc_move_has_effect(const BattleMove *move, uint8_t kind);
// Whether the core can run this move's effect list under the request's rules.
bool bc_effects_supported(const BattleRequest *request, const BattleMove *move);
// Executes one move (PP, targets, effects); returns true if a pivot switch is due.
bool bc_execute_move(Battle *battle, Position user, uint8_t move_slot, Position selected, EventLog *log);
// Volatile lifetimes (table in battle_core.h).
typedef enum { VOL_TURN_START, VOL_TURN_END, VOL_SWITCH_OUT, VOL_SHIFT, VOL_ROTATE, VOL_FAINT } VolatileEvent;
void bc_volatiles_on(Battler *mon, VolatileEvent event);
void bc_effects_begin_turn(Battle *battle);
void bc_effects_end_turn(Battle *battle);
void bc_effects_reset_volatiles(Battler *mon);
void bc_cancel_charge(Battle *battle, Position user, EventLog *log);
// Confusion check before an action; true if the battler hurt itself instead.
bool bc_confusion_check(Battle *battle, Position user, EventLog *log);
#endif
