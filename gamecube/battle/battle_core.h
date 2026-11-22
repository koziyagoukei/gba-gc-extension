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
// Each hit can also report the substitute taking it and breaking, Sturdy and
// a contact ability (announcement, status or damage, the attacker's faint).
// V11 adds per target an ability immunity (2), per effect an ability block,
// entry abilities for every send-out (announcement, cure, a result per foe),
// end-of-turn abilities and the battle-start notices. V15 adds per entry a
// weather change or its block and a suppression notice, the battle-start
// weather, and at the end of the turn the weather's continuation or end and
// per Pokemon its residual (ability, HP change, faint). V16 adds per entry a
// terrain change, per target a terrain block, the battle-start terrain, and
// at the end of the turn the terrain heals and the terrain's end.
#define BC_EVENTS_PER_TARGET (7 * BATTLE_MAX_HITS + 9 + 3 * BATTLE_MAX_EFFECTS)
#define BC_ENTRY_EVENTS (7 + BC_MAX_ACTIVE)
#define BC_WEATHER_TURN_EVENTS (1 + 3 * BC_MAX_ACTIONS)
#define BC_TERRAIN_TURN_EVENTS (1 + BC_MAX_ACTIONS)
#define BC_MAX_EVENTS (BC_MAX_ACTIONS * (8 + 2 * BC_ENTRY_EVENTS + BC_MAX_ACTIVE * BC_EVENTS_PER_TARGET) + \
                       5 * BC_MAX_ACTIONS + 10 + 2 * BATTLE_MAX_PER_SIDE + BC_WEATHER_TURN_EVENTS + \
                       BC_TERRAIN_TURN_EVENTS)

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
    bool entered;               // sent out since this turn started (V11 end-of-turn abilities)
    uint8_t ability_state;      // V12 ability state (Flash Fire boost); leaves with the battler
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
    // V17 held item (battle_items.c, docs/items.md): the entry whose item the
    // Pokemon holds now, NULL for none. The item it started with is its own
    // entry's (entry->record.held_item, entry->mon.item); the definition goes
    // with the item, not the holder. B0 never changes it during a battle.
    const BattleEntry *item_from;
    // V19 (docs/items.md 12.2): the item it used up last (NULL: none), with
    // the timing that used it (BITIME_*) and the turns completed then. Using
    // an item sets item_from to NULL: the roster Pokemon holds nothing from
    // then on, whatever position it takes (switch, Shift, Rotation).
    const BattleEntry *item_used;
    uint8_t item_used_timing, item_used_turn;
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
              EV_SUBSTITUTE, EV_SUBSTITUTE_HIT, EV_SUBSTITUTE_BREAK, EV_CHARGE, EV_CHARGE_END, EV_RECHARGE,
              // V11 abilities: user = holder, ability = source id, condition = BABIL_* kind.
              EV_ABILITY,             // the ability acts on target (its consequence follows)
              EV_ABILITY_BLOCK,       // the holder's ability stopped something from target
              EV_ABILITY_DAMAGE,      // target loses HP to the holder's ability
              EV_ABILITY_UNSUPPORTED, // battle start: entry `battler` of side user.side is not simulated
              // V15 weather (docs/weather.md): condition = BWEATHER_* kind.
              EV_WEATHER_START,       // user = setter (slot BATTLE_SLOT_NONE: the battle's rules); amount = turns
              EV_WEATHER_CONTINUE,    // end of turn, the weather goes on (amount = turns left, 0 = no end)
              EV_WEATHER_END,         // end of turn, the weather ends
              EV_WEATHER_DAMAGE,      // target loses HP to the weather (amount, hp_after)
              // V16 terrain (docs/terrain.md): condition = BTERRAIN_* kind.
              EV_TERRAIN_START,       // user = setter (slot BATTLE_SLOT_NONE: the battle's rules); amount = turns
              EV_TERRAIN_END,         // end of turn, the terrain ends
              EV_TERRAIN_HEAL,        // end of turn, target regains HP from the terrain (amount, hp_after)
              EV_TERRAIN_BLOCK,       // the terrain kept user's move or its effect off target
              // V17 items (docs/items.md).
              EV_ITEMS_NORMALIZED,    // battle start: RULE_IGNORE_ITEMS, no held item has any effect
                                      // (amount = Pokemon holding an item, battler = those not simulated)
              // V18 item HP records: target = holder, item = its item id, amount and hp_after.
              EV_ITEM_HEAL,           // the holder regains HP from its item (Leftovers)
              EV_ITEM_DAMAGE,         // the holder loses HP to its item (Black Sludge); EV_FAINT may follow
              // V19 consumable items (docs/items.md 12): target = holder, item = its item id.
              EV_ITEM_CURE,           // condition = the BST_* status cured, or BATTLE_CURED_CONFUSION
              EV_ITEM_ENDURE,         // the holder kept 1 HP from a lethal hit (Focus Sash)
              EV_ITEM_CONSUMED        // the item is used up (condition = BITIME_*); it holds nothing now
} EventKind;
#define BATTLE_CURED_CONFUSION 0xffu  // EV_ITEM_CURE: confusion, not a major status
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
    union {
        uint16_t ability;    // EV_ABILITY*: the source ability id
        uint16_t item;       // V18 EV_ITEM_*: the holder's item id (names only)
    };
} BattleEvent;

typedef struct { BattleEvent events[BC_MAX_EVENTS]; unsigned count; } EventLog;

enum BattleInitError {
    BI_OK = 0, BI_FORMAT, BI_PARTY_SIZE, BI_NO_PLAYER_MOVES, BI_NO_OPPONENT_MOVES, BI_BAD_TYPES, BI_FAINTED_LEAD, BI_CONDITIONS,
    BI_UNSUPPORTED_ABILITY,  // RULE_STRICT_ABILITIES and an ability is UNSUPPORTED
    BI_UNSUPPORTED_MOVE,     // V15 RULE_STRICT_MOVES and a known move cannot be run
    BI_ITEMS,                // V16 without RULE_IGNORE_ITEMS: items could change grounding, none is run
    BI_UNSUPPORTED_ITEM,     // V17: an item the core cannot run, and the request does not normalise items
    BI_ITEMS_NORMALIZED,     // V17 RULE_STRICT_ITEMS with RULE_IGNORE_ITEMS while an item has an effect
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

// V15 field state (docs/weather.md). One effect per field condition, changed
// only through the shared operations of battle_field.c, whatever its source;
// a terrain (M4-T) is a second FieldEffect of the same shape.
enum FieldSource { FSRC_NONE = 0, FSRC_RULE = 1, FSRC_MOVE = 2, FSRC_ABILITY = 3 };
typedef struct {
    uint8_t kind;        // BWEATHER_*, 0 = none
    uint8_t turns;       // turns left; 0 with a kind = no end
    uint8_t source_kind; // FSRC_*
    Position source;     // who set it (slot BATTLE_SLOT_NONE for the rules)
} FieldEffect;
typedef struct {
    FieldEffect weather;
    bool locked;         // BFR_START_LOCKED: the start weather cannot be replaced
    FieldEffect terrain; // V16: kind is BTERRAIN_*, independent of the weather
} BattleField;

typedef struct {
    const BattleRequest *request;
    Side sides[2];
    TurnResolution resolution;
    uint32_t rng;
    uint8_t turn;     // turns completed
    uint8_t outcome;  // BattleOutcome, OUTCOME_NONE while running
    bool ability_mirroring;  // a Synchronize status is being given back (no second reflection)
    BattleField field;       // V15
    uint8_t entry_pending[2];  // V15: field slots replaced this round whose entry abilities wait
} Battle;

const char *battle_init_error_name(int error);
int battle_init(Battle *battle, const BattleRequest *request, uint32_t seed);
Battler *battle_active(Battle *battle, uint8_t side);
const Battler *battle_at(const Battle *battle, Position position);
unsigned battle_formation_size(uint8_t format);
uint8_t battle_rotation_slot(uint8_t rotation);
uint8_t battle_rotation_origin(uint8_t rotation, uint8_t destination);
bool battle_adjacent(const Battle *battle, Position a, Position b);
// Whether `other` is in `range` (BRANGE_*) of `holder`: the shared position rule
// for effects that reach several Pokemon. ADJACENT_FOES uses Triple adjacency;
// in Single, Double and Rotation every foe on the field counts.
bool battle_in_range(const Battle *battle, Position holder, Position other, uint8_t range);
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
// Battle start (after battle_init, before the first command): V11 notices for
// abilities that are not simulated, then the leads' entry abilities in speed
// order. Older requests produce no events.
void battle_begin(Battle *battle, EventLog *log);
void battle_run_turn(Battle *battle, const Action *actions, unsigned count, EventLog *log);
bool battle_pending_pivot(const Battle *battle, Position *position);
// Multi-turn actions: a locked position is not prompted; the core supplies its action.
BattleActionState battle_action_state(const Battle *battle, Position position);
bool battle_forced_action(const Battle *battle, Position position, Action *out);
uint8_t battle_volatile_flags(const Battle *battle, Position position);  // BVF_*
bool battle_resume_pivot(Battle *battle, uint8_t battler, EventLog *log);
// V15 weather: the weather in effect (BWEATHER_NONE while a Pokemon on the
// field suppresses it), and the weather on the field whether or not suppressed.
uint8_t battle_weather(const Battle *battle);
const FieldEffect *battle_field_weather(const Battle *battle);
// V16 terrain: the terrain on the field (BTERRAIN_*), and whether a Pokemon is
// grounded (the shared rule of every terrain effect, docs/terrain.md).
uint8_t battle_terrain(const Battle *battle);
const FieldEffect *battle_field_terrain(const Battle *battle);
bool battle_grounded(const Battle *battle, const Battler *mon);
// V17 held items (battle_items.c, docs/items.md).
uint16_t battle_held_item(const Battler *mon);         // the item it holds now (game id, 0 = none)
bool battle_items_simulated(const Battle *battle);      // V17 without RULE_IGNORE_ITEMS: items run
bool battle_items_normalized(const Battle *battle);     // V17 with RULE_IGNORE_ITEMS: every item is off
#endif
