#ifndef GC_BATTLE_ANIMATION_H
#define GC_BATTLE_ANIMATION_H

#include "layout.h"

typedef enum {
    MSG_NONE, MSG_INTRO, MSG_ENTER, MSG_CHOOSE, MSG_MOVE, MSG_MISS,
    MSG_IMMUNE, MSG_STRONG, MSG_WEAK, MSG_FAINT, MSG_CANCEL, MSG_NO_MOVES, MSG_RESULT, MSG_SHIFT, MSG_ROTATE, MSG_WITHDRAW, MSG_RECOIL, MSG_DRAG_OUT, MSG_RESERVE,
    MSG_STAGE, MSG_STATUS, MSG_CURE, MSG_STATUS_BLOCK, MSG_RESIDUAL,
    MSG_HEAL, MSG_DRAIN, MSG_PROTECT, MSG_PROTECTED, MSG_FAIL, MSG_MULTI_HIT,
    MSG_FLINCH, MSG_CONFUSE, MSG_CONFUSED, MSG_CONFUSION_HIT, MSG_CONFUSION_END,
    MSG_SUBSTITUTE, MSG_SUBSTITUTE_HIT, MSG_SUBSTITUTE_BREAK, MSG_CHARGE, MSG_CHARGE_END, MSG_RECHARGE,
} MessageKind;

typedef struct {
    MessageKind kind;
    Position subject;
    uint16_t move;
    uint8_t outcome;
    uint8_t condition, stat;
    int8_t stage;
} SceneMessage;

typedef struct {
    bool visible, blink;
    uint8_t battler, condition;
    float scale, drop;
    ScreenPoint offset;
    float hp;
    uint16_t max_hp;
    uint8_t volatiles;  // BVF_* as presented (badges, hidden while semi-invulnerable)
} ActorFrame;

typedef enum { ANIM_MESSAGE, ANIM_WAIT, ANIM_FADE, ANIM_ENTER, ANIM_ATTACK, ANIM_HIT, ANIM_HP, ANIM_FAINT, ANIM_SHIFT, ANIM_ROTATE, ANIM_WITHDRAW, ANIM_CONDITION, ANIM_VOLATILE } AnimationKind;

typedef struct {
    AnimationKind kind;
    Position source, target;
    uint16_t frames, hp, max_hp;
    uint8_t rotation, entrant; // one-based roster index for a send-out
    uint8_t condition;
    uint8_t vol_set, vol_clear;  // ANIM_VOLATILE
    SceneMessage message;
} AnimationStep;

// The longest sequence for one core event uses three steps.
#define ANIMATION_STEPS (3 * BC_MAX_EVENTS + 18)

typedef struct {
    ActorFrame actor[2][BC_MAX_ACTIVE];
    AnimationStep steps[ANIMATION_STEPS];
    unsigned count, index, frame;
    SceneMessage message;
    float fade;
    bool effect_visible;
    ScreenPoint effect;
} BattleAnimation;

void animation_init(BattleAnimation *a, const Battle *battle);
void animation_intro(BattleAnimation *a, const BattleLayout *layout);
void animation_play(BattleAnimation *a, const BattleLayout *layout, const EventLog *log);
bool animation_tick(BattleAnimation *a, const BattleLayout *layout);
#endif
