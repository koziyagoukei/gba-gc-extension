#ifndef GC_BATTLE_CORE_H
#define GC_BATTLE_CORE_H

// GameCube battle core. Owns the temporary battle state, the RNG, move
// validation, turn order, damage and faint/victory. It depends only on the
// battle protocol records, never on a game build or an SDK, so it is unit
// tested on the desktop (battle_test.c).
//
// The model is split into Battle / Side / Battler / Position / Action / Target
// so more active battlers per side can be added later. Today only
// BATTLE_FORMAT_SINGLE with one Pokemon per side is accepted.

#include <stdbool.h>
#include <stdint.h>
#include "battle_protocol.h"

#define BC_MAX_ACTIVE 3
#define BC_MAX_EVENTS 16

typedef struct { uint8_t side, slot; } Position;   // side 0 = player, 1 = opponent; slot = active position
typedef struct { Position pos; } Target;

typedef struct {
    const BattleEntry *entry;
    uint8_t party_slot, level, type_count, types[2];
    uint16_t hp, max_hp, attack, defense, sp_attack, sp_defense, speed;
    uint32_t status;
    BattleMove moves[4];
    uint16_t move_ids[4];
    uint8_t pp[4], max_pp[4];
    bool fainted;
} Battler;

typedef struct {
    Battler battlers[BATTLE_MAX_PER_SIDE];
    uint8_t count;
    uint8_t active[BC_MAX_ACTIVE];  // battler index per active position
    uint8_t active_count;
} Side;

typedef enum { ACT_NONE, ACT_MOVE, ACT_FORFEIT } ActionKind;
typedef struct {
    ActionKind kind;
    Position user;
    uint8_t move_slot;
    Target target;
} Action;

typedef enum { EV_MOVE_USED, EV_MISS, EV_DAMAGE, EV_NO_EFFECT, EV_FAINT, EV_FORFEIT, EV_NO_MOVES, EV_END } EventKind;
typedef struct {
    EventKind kind;
    Position user, target;
    uint16_t move_id;
    uint16_t amount, hp_after;
    uint8_t effectiveness;   // quarters: 0 none, 2 = x0.5, 4 = x1, 8 = x2 ...
    uint8_t outcome;         // EV_END
} BattleEvent;

typedef struct { BattleEvent events[BC_MAX_EVENTS]; unsigned count; } EventLog;

enum BattleInitError {
    BI_OK = 0, BI_FORMAT, BI_PARTY_SIZE, BI_NO_PLAYER_MOVES, BI_NO_OPPONENT_MOVES, BI_BAD_TYPES, BI_FAINTED_LEAD,
};

typedef struct {
    const BattleRequest *request;
    Side sides[2];
    uint32_t rng;
    uint8_t turn;     // turns completed
    uint8_t outcome;  // BattleOutcome, OUTCOME_NONE while running
} Battle;

const char *battle_init_error_name(int error);
int battle_init(Battle *battle, const BattleRequest *request, uint32_t seed);
Battler *battle_active(Battle *battle, uint8_t side);
bool battle_move_supported(const BattleRequest *request, const BattleMove *move);
uint8_t battle_usable_moves(const Battle *battle, const Battler *battler);  // bit mask
bool battle_validate_action(const Battle *battle, const Action *action);
void battle_choose_ai(Battle *battle, uint8_t side, Action *out);
// Damage with an explicit random percentage (85-100); effectiveness in quarters.
uint16_t battle_damage(const Battle *battle, const Battler *attacker, const BattleMove *move,
                       const Battler *defender, unsigned random_percent, uint8_t *effectiveness);
void battle_run_turn(Battle *battle, const Action *actions, unsigned count, EventLog *log);
#endif
