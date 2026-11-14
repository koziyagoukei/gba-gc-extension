#ifndef GC_BATTLE_CORE_H
#define GC_BATTLE_CORE_H

// GameCube battle core. Owns the temporary battle state, the RNG, move
// validation, turn order, damage and faint/victory. It depends only on the
// battle protocol records, never on a game build or an SDK, so it is unit
// tested on the desktop (battle_test.c).
//
// The model is split into Battle / Side / Battler / Position / Action / Target
// with field positions mapped to stable battler indices.

#include <stdbool.h>
#include <stdint.h>
#include "battle_protocol.h"

#define BC_MAX_ACTIVE BATTLE_FIELD_SLOTS
#define BC_MAX_ACTIONS (2 * BC_MAX_ACTIVE)
// Worst case per action: MOVE_USED, then per target up to BATTLE_MAX_HITS hits,
// faint, multi-hit, thaw, drain and every other effect (with switch events),
// then recoil and its faint; plus end-of-turn residuals and the battle end.
// Each hit can also report the substitute taking it and breaking.
#define BC_EVENTS_PER_TARGET (3 * BATTLE_MAX_HITS + 6 + 2 * BATTLE_MAX_EFFECTS)
#define BC_MAX_EVENTS (BC_MAX_ACTIONS * (8 + BC_MAX_ACTIVE * BC_EVENTS_PER_TARGET) + 3 * BC_MAX_ACTIONS + 8)

typedef struct { uint8_t side, slot; } Position;   // side 0 = player, 1 = opponent; slot = active position
typedef struct { Position pos; } Target;

// Volatile state: lives while the battler is on the field, separate from
// HP/PP/major status (the Pokemon) and from stat stages. Lifetimes are in
// bc_volatiles_on (battle_effects.c):
//                       turn end  switch/drag/pivot  Shift  rotation  faint
//   confusion             keep        clear           keep    keep    clear
//   substitute            keep        clear           keep    keep    clear
//   charge, semi-invuln.  keep        clear           keep     (*)    clear
//   recharge            tick down     clear           keep     (*)    clear
//   flinch               clear        clear           keep    keep    clear
//   Protect this turn   (turn start)  clear           keep    keep    clear
// (*) a charging or recharging FRONT has a forced action and cannot rotate
// (Gen 5: no action selection). A Gen 5 rotation does not run the
// switch-out reset, so wings keep their volatiles.
typedef struct {
    uint8_t confusion;          // remaining counter; 0 = not confused
    uint16_t substitute_hp;     // 0 = no substitute
    uint8_t charge_slot;        // move slot being charged, or BATTLE_SLOT_NONE
    Position charge_target;     // target chosen on the charging turn
    uint8_t semi_invulnerable;  // BSEMI_* while charging
    uint8_t recharge;           // > 0: the next action is a forced recharge
    bool flinched;              // this turn only
    bool acted;                 // its action this turn has started (or been skipped)
    bool protected_turn;        // Protect succeeded this turn
    uint8_t protect_uses;       // consecutive successful Protect uses
} BattlerVolatiles;

typedef struct {
    const BattleEntry *entry;
    uint8_t party_slot, level, type_count, types[2];
    uint16_t hp, max_hp, attack, defense, sp_attack, sp_defense, speed;
    uint32_t status;
    uint8_t sleep_start;
    int8_t stat_stages[BSTAT_COUNT]; // field lifetime; kept across Shift and Rotation
    BattleMove moves[4];
    uint16_t move_ids[4];
    uint8_t pp[4], max_pp[4];
    bool fainted;
    BattlerVolatiles vol;
} Battler;

typedef struct {
    Battler battlers[BATTLE_MAX_PER_SIDE];
    uint8_t count;
    uint8_t active[BC_MAX_ACTIVE];  // stable battler index or BATTLE_SLOT_NONE
    uint8_t active_count;
} Side;

typedef enum { ACT_NONE, ACT_MOVE, ACT_FORFEIT, ACT_SHIFT, ACT_SWITCH, ACT_STRUGGLE, ACT_RECHARGE } ActionKind;
typedef struct {
    ActionKind kind;
    Position user;
    uint8_t move_slot;
    Target target;
    uint8_t rotation, reserve;
} Action;

typedef enum { EV_MOVE_USED, EV_MISS, EV_DAMAGE, EV_NO_EFFECT, EV_FAINT, EV_FORFEIT, EV_NO_MOVES, EV_END, EV_SHIFT, EV_ROTATE, EV_SEND_OUT, EV_WITHDRAW, EV_RECOIL, EV_DRAG_OUT, EV_STAGE, EV_STATUS, EV_CURE, EV_STATUS_BLOCK, EV_RESIDUAL,
              EV_HEAL, EV_DRAIN, EV_PROTECT, EV_PROTECTED, EV_FAIL, EV_MULTI_HIT,
              EV_FLINCH, EV_CONFUSE, EV_CONFUSED, EV_CONFUSION_HIT, EV_CONFUSION_END,
              EV_SUBSTITUTE, EV_SUBSTITUTE_HIT, EV_SUBSTITUTE_BREAK, EV_CHARGE, EV_CHARGE_END, EV_RECHARGE } EventKind;
typedef struct {
    EventKind kind;
    Position user, target;
    uint16_t move_id;
    uint16_t amount, hp_after;   // EV_MULTI_HIT: amount = hits landed; EV_HEAL/EV_DRAIN: HP restored
    uint8_t effectiveness;   // quarters: 0 none, 2 = x0.5, 4 = x1, 8 = x2 ...
    uint8_t outcome;         // EV_END
    uint8_t rotation;        // EV_ROTATE
    uint8_t battler;         // EV_SEND_OUT: roster index; amount=max HP, hp_after=current HP
    uint8_t condition;       // status events and send-out
    uint8_t stat;            // EV_STAGE
    int8_t stage;            // EV_STAGE: new stage
} BattleEvent;

typedef struct { BattleEvent events[BC_MAX_EVENTS]; unsigned count; } EventLog;

enum BattleInitError {
    BI_OK = 0, BI_FORMAT, BI_PARTY_SIZE, BI_NO_PLAYER_MOVES, BI_NO_OPPONENT_MOVES, BI_BAD_TYPES, BI_FAINTED_LEAD, BI_CONDITIONS,
};

// Action resolution state within a turn. A battler's own multi-turn state
// (charging, semi-invulnerable, recharging) is in its volatiles; see
// battle_action_state.
typedef enum { TA_PENDING, TA_DONE, TA_SKIPPED, TA_INTERRUPTED } TurnActionStatus;
typedef struct {
    Action action;
    uint8_t battler;
    int priority;
    uint32_t speed, tie;
    uint8_t status;  // TurnActionStatus
} TurnAction;
typedef enum { BAS_EMPTY, BAS_FAINTED, BAS_READY, BAS_CHARGING, BAS_SEMI_INVULNERABLE, BAS_RECHARGING } BattleActionState;

typedef struct {
    TurnAction actions[BC_MAX_ACTIONS];
    unsigned next, count;
    Position pivot;
    bool waiting_for_pivot;
} TurnResolution;

typedef struct {
    const BattleRequest *request;
    Side sides[2];
    TurnResolution resolution;
    uint32_t rng;
    uint8_t turn;     // turns completed
    uint8_t outcome;  // BattleOutcome, OUTCOME_NONE while running
} Battle;

const char *battle_init_error_name(int error);
int battle_init(Battle *battle, const BattleRequest *request, uint32_t seed);
Battler *battle_active(Battle *battle, uint8_t side);
const Battler *battle_at(const Battle *battle, Position position);
unsigned battle_formation_size(uint8_t format);
uint8_t battle_rotation_slot(uint8_t rotation);
uint8_t battle_rotation_origin(uint8_t rotation, uint8_t destination);
bool battle_adjacent(const Battle *battle, Position a, Position b);
bool battle_can_shift(const Battle *battle, Position user);
uint8_t battle_target_mask(const Battle *battle, Position user, const BattleMove *move, uint8_t target_side);
uint8_t battle_command_targets(const Battle *battle, Position user, unsigned move_slot);
unsigned battle_collect_actors(const Battle *battle, uint8_t side, Position *out);
void battle_choose_at(Battle *battle, Position pos, Action *out);
uint8_t battle_reserve_mask(const Battle *battle, uint8_t side);
bool battle_can_struggle(const Battle *battle, Position position);
uint8_t battle_switch_mask(const Battle *battle, Position position);
uint8_t battle_replacement_mask(const Battle *battle, Position position);
bool battle_pending_replacement(const Battle *battle, Position *position);
bool battle_replace(Battle *battle, Position position, uint8_t battler, EventLog *log);
bool battle_move_supported(const BattleRequest *request, const BattleMove *move);
uint8_t battle_usable_moves(const Battle *battle, const Battler *battler);  // bit mask
bool battle_validate_action(const Battle *battle, const Action *action);
void battle_choose_ai(Battle *battle, uint8_t side, Action *out);
// Damage with an explicit random percentage (85-100); effectiveness in quarters.
uint16_t battle_damage(const Battle *battle, const Battler *attacker, const BattleMove *move,
                       const Battler *defender, unsigned random_percent, uint8_t *effectiveness);
void battle_run_turn(Battle *battle, const Action *actions, unsigned count, EventLog *log);
bool battle_pending_pivot(const Battle *battle, Position *position);
// Multi-turn actions: a locked position is not prompted; the core supplies its action.
BattleActionState battle_action_state(const Battle *battle, Position position);
bool battle_forced_action(const Battle *battle, Position position, Action *out);
uint8_t battle_volatile_flags(const Battle *battle, Position position);  // BVF_*
bool battle_resume_pivot(Battle *battle, uint8_t battler, EventLog *log);
#endif
