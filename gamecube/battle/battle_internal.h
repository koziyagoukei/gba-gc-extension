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
// Type multiplier in quarters; `attacker` (may be NULL) applies its TYPE_PASS ability hooks.
uint8_t bc_effectiveness(const Battle *battle, const Battler *attacker, uint8_t move_type, const Battler *defender);
// True once either side has no Pokemon left (the battle is decided).
bool bc_battle_decided(const Battle *battle);
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
// Sets a major status (sleep counter rolled from the rules) and emits EV_STATUS.
void bc_set_status(Battle *battle, Position source, Position target, uint8_t kind, uint16_t move_id, EventLog *log);
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
// Struggle's hit through the shared target checks (Protect, semi-invulnerable,
// accuracy, substitute); typeless. True if it landed (recoil is then due).
bool bc_struggle_strike(Battle *battle, Position user, Position target, const BattleMove *move, EventLog *log,
                        unsigned *knocked_out);
// Volatile lifetimes (table in battle_core.h).
typedef enum { VOL_TURN_START, VOL_TURN_END, VOL_SWITCH_OUT, VOL_SHIFT, VOL_ROTATE, VOL_FAINT } VolatileEvent;
void bc_volatiles_on(Battler *mon, VolatileEvent event);
void bc_effects_begin_turn(Battle *battle);
void bc_effects_end_turn(Battle *battle);
void bc_effects_reset_volatiles(Battler *mon);
void bc_cancel_charge(Battle *battle, Position user, EventLog *log);
// Confusion check before an action; true if the battler hurt itself instead.
bool bc_confusion_check(Battle *battle, Position user, EventLog *log);

// Shared damage modifiers (battle_modifiers.c, docs/rule-poc.md): the V13
// records of the move and of both abilities, and the V12 multiplier hooks,
// for one stage (BMOD_*) of one hit. base_power is the power after
// BMOD_BASE_POWER (what BPRED_POWER_AT_MOST compares).
typedef struct {
    const Battle *b;
    const Battler *attacker, *defender;
    const BattleMove *move;
    bool typeless;
    unsigned base_power;
} ModContext;
uint64_t bc_modify(const ModContext *context, uint8_t stage, uint64_t value);

// Ability hooks (battle_abilities.c, docs/abilities.md). All are inert unless
// the request is V11+ without RULE_IGNORE_ABILITIES. An ability is a list of
// hooks; a lookup returns a hook whose holder conditions (status, state) hold.
bool bc_abilities_enabled(const Battle *battle);
const BattleAbilityHook *bc_ability(const Battle *battle, const Battler *mon, uint8_t kind);
const BattleAbilityHook *bc_ability_next(const Battle *battle, const Battler *mon, uint8_t kind,
                                         const BattleAbilityHook *previous);
bool bc_ability_unsupported(const Battle *battle, uint8_t side, uint8_t index);
void bc_ability_event(const Battle *battle, EventLog *log, Position holder, Position target,
                      const BattleAbilityHook *hook);
void bc_ability_block(const Battle *battle, EventLog *log, Position holder, Position source,
                      const BattleAbilityHook *hook);
// Damage calculation (per hit). The multipliers of moves and abilities run
// through bc_modify (battle_modifiers.c); these are the remaining special rules.
bool bc_ability_ignores_burn(const Battle *battle, const Battler *attacker);
unsigned bc_ability_stab(const Battle *battle, const Battler *attacker);  // percent
unsigned bc_ability_type_factor(const Battle *battle, const Battler *attacker, uint8_t move_type,
                                uint8_t defend_type, unsigned factor);
uint16_t bc_ability_endure(const Battle *battle, Position target, uint16_t amount, bool *endured);
// Per target, before the substitute and accuracy: true if the move stops here.
bool bc_ability_blocks_move(Battle *battle, Position user, Position target, const BattleMove *move, bool typeless,
                            uint16_t move_id, EventLog *log);
bool bc_ability_sure_hit(const Battle *battle, Position user, Position target);
unsigned bc_ability_accuracy(const Battle *battle, Position user, unsigned accuracy);
bool bc_ability_ignores_evasion(const Battle *battle, Position user);
bool bc_ability_passes_substitute(const Battle *battle, Position user);
// Effect level (inside the effect handlers, at the effect's own unit).
unsigned bc_ability_chance(const Battle *battle, Position user, unsigned chance);
bool bc_ability_shields(const Battle *battle, Position target);
const BattleAbilityHook *bc_ability_status_immunity(const Battle *battle, Position target, uint8_t status);
const BattleAbilityHook *bc_ability_volatile_immunity(const Battle *battle, Position target, uint8_t bit);
const BattleAbilityHook *bc_ability_drop_immunity(const Battle *battle, Position target, uint8_t stat);
bool bc_ability_no_recoil(const Battle *battle, Position user);
// Right after `holder` got a major status from `source` (Synchronize).
void bc_ability_status_set(Battle *battle, Position source, Position holder, uint8_t status, uint16_t move_id,
                           EventLog *log);
// After a move (after recoil): Moxie, with the number of Pokemon it knocked out.
void bc_ability_after_move(Battle *battle, Position user, unsigned knocked_out, EventLog *log);
// Per hit that struck the holder itself.
void bc_ability_contact(Battle *battle, Position user, Position target, const BattleMove *move, uint16_t move_id,
                        EventLog *log);
// Entry (every send-out and the battle start), end of turn, battle start notices.
void bc_ability_on_entry(Battle *battle, Position position, EventLog *log);
void bc_ability_end_turn(Battle *battle, Position position, EventLog *log);
void bc_ability_end_turn_cure(Battle *battle, Position position, EventLog *log);
void bc_ability_begin_battle(Battle *battle, EventLog *log);
#endif
