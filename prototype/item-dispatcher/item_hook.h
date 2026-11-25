#ifndef ITEM_HOOK_H
#define ITEM_HOOK_H

#include <stdbool.h>
#include <stdint.h>

/* Host-only normalized inputs. These values are not protocol constants. */
typedef enum {
    ITEM_FINAL_DAMAGE,
    ITEM_AFTER_MOVE,
    ITEM_CONTACT_HIT,
} ItemPoint;

typedef enum {
    EFFECT_FINAL_FACTOR,
    EFFECT_SELF_DAMAGE,
} ItemEffect;

typedef struct {
    ItemPoint point;
    ItemEffect effect;
    uint16_t value;      /* Q12 factor or maximum-HP divisor */
    bool consume;
} ItemRecord;

typedef struct {
    bool after_move_seen;
} MoveItemProgress;

typedef struct {
    uint16_t max_hp;
    bool alive;
    bool same_actor;     /* active roster identity matches the actor that began the move */
    bool landed;        /* a damaging move landed, including on a substitute */
    bool items_enabled;
    bool indirect_immune;
    bool skip_late_move_end;
} AfterMoveFacts;

typedef enum {
    ITEM_INVALID_RECORD,
    ITEM_UNSUPPORTED_POINT,
    ITEM_NO_EFFECT,
    ITEM_ALREADY_VISITED,
    ITEM_APPLY_DAMAGE,
} ItemDecision;

typedef struct {
    ItemDecision decision;
    uint16_t damage;    /* requested loss; the caller clamps HP and emits events */
} ItemOutcome;

bool item_record_valid(const ItemRecord *record);
uint16_t item_final_factor(const ItemRecord *record, bool items_enabled, bool fixed_damage);
/* One call after the move's earlier effects have settled. NULL means no effective item. */
ItemOutcome item_after_move(MoveItemProgress *progress, const ItemRecord *record,
                            AfterMoveFacts facts);

#endif
