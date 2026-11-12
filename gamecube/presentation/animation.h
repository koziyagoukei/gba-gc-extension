#ifndef GC_BATTLE_ANIMATION_H
#define GC_BATTLE_ANIMATION_H

#include "layout.h"

typedef enum {
    MSG_NONE, MSG_INTRO, MSG_ENTER, MSG_CHOOSE, MSG_MOVE, MSG_MISS,
    MSG_IMMUNE, MSG_STRONG, MSG_WEAK, MSG_FAINT, MSG_CANCEL, MSG_NO_MOVES, MSG_RESULT, MSG_SHIFT, MSG_ROTATE, MSG_WITHDRAW, MSG_RECOIL, MSG_DRAG_OUT, MSG_RESERVE,
} MessageKind;

typedef struct {
    MessageKind kind;
    Position subject;
    uint16_t move;
    uint8_t outcome;
} SceneMessage;

typedef struct {
    bool visible, blink;
    uint8_t battler;
    float scale, drop;
    ScreenPoint offset;
    float hp;
    uint16_t max_hp;
} ActorFrame;

typedef enum { ANIM_MESSAGE, ANIM_WAIT, ANIM_FADE, ANIM_ENTER, ANIM_ATTACK, ANIM_HIT, ANIM_HP, ANIM_FAINT, ANIM_SHIFT, ANIM_ROTATE, ANIM_WITHDRAW } AnimationKind;

typedef struct {
    AnimationKind kind;
    Position source, target;
    uint16_t frames, hp, max_hp;
    uint8_t rotation, entrant; // one-based roster index for a send-out
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
