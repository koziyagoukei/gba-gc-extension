#include "animation.h"

#include <string.h>

void animation_init(BattleAnimation *a, const Battle *battle) {
    memset(a, 0, sizeof(*a));
    a->fade = 1;
    for (unsigned side = 0; side < 2; ++side) {
        const Side *s = &battle->sides[side];
        for (unsigned slot = 0; slot < s->active_count && slot < BC_MAX_ACTIVE; ++slot) {
            ActorFrame *frame = &a->actor[side][slot];
            const Battler *b = battle_at(battle, (Position){side, slot});
            frame->battler = b ? s->active[slot] : BATTLE_SLOT_NONE;
            if (!b) continue;
            frame->hp = b->hp;
            frame->max_hp = b->max_hp;
        }
    }
}

static void add(BattleAnimation *a, AnimationStep step) {
    if (a->count < ANIMATION_STEPS) a->steps[a->count++] = step;
}

static void wait(BattleAnimation *a, unsigned frames) {
    add(a, (AnimationStep){.kind = ANIM_WAIT, .frames = frames});
}

static void message(BattleAnimation *a, SceneMessage msg, unsigned hold) {
    add(a, (AnimationStep){.kind = ANIM_MESSAGE, .message = msg});
    if (hold) wait(a, hold);
}

static void reset_steps(BattleAnimation *a) {
    a->count = a->index = a->frame = 0;
    a->effect_visible = false;
}

void animation_intro(BattleAnimation *a, const BattleLayout *layout) {
    reset_steps(a);
    add(a, (AnimationStep){.kind = ANIM_FADE, .frames = 20});
    const bool single = layout->active_per_side == 1;
    message(a, (SceneMessage){.kind = MSG_INTRO}, single ? 80 : 38);
    for (int side = 1; side >= 0; --side)
        for (unsigned slot = 0; slot < layout->active_per_side; ++slot) {
            const Position pos = {side, slot};
            if (!a->actor[side][slot].max_hp) continue;
            message(a, (SceneMessage){.kind = MSG_ENTER, .subject = pos}, 0);
            add(a, (AnimationStep){.kind = ANIM_ENTER, .target = pos, .frames = 18});
            wait(a, single ? 40 : 6);
        }
}

void animation_play(BattleAnimation *a, const BattleLayout *layout, const EventLog *log) {
    reset_steps(a);
    a->message = (SceneMessage){0};
    const bool single = layout->active_per_side == 1;
    for (unsigned i = 0; i < log->count && i < BC_MAX_EVENTS; ++i) {
        const BattleEvent *e = &log->events[i];
        if (e->kind <= EV_FAINT && !battle_layout_anchor(layout, e->target)) continue;
        switch (e->kind) {
        case EV_MOVE_USED:
            if (!battle_layout_anchor(layout, e->user)) break;
            message(a, (SceneMessage){MSG_MOVE, e->user, e->move_id, 0}, 0);
            add(a, (AnimationStep){.kind = ANIM_ATTACK, .source = e->user, .target = e->target, .frames = single ? 16 : 10});
            wait(a, single ? 16 : 4);
            break;
        case EV_MISS:
            message(a, (SceneMessage){.kind = MSG_MISS, .subject = e->target}, 50);
            break;
        case EV_DAMAGE:
            add(a, (AnimationStep){.kind = ANIM_HIT, .target = e->target, .frames = single ? 28 : 16});
            add(a, (AnimationStep){.kind = ANIM_HP, .target = e->target, .hp = e->hp_after});
            if (e->effectiveness > 4) message(a, (SceneMessage){.kind = MSG_STRONG}, 50);
            else if (e->effectiveness < 4) message(a, (SceneMessage){.kind = MSG_WEAK}, 50);
            else wait(a, single ? 12 : 4);
            break;
        case EV_NO_EFFECT:
            message(a, (SceneMessage){.kind = MSG_IMMUNE, .subject = e->target}, 50);
            break;
        case EV_FAINT:
            add(a, (AnimationStep){.kind = ANIM_FAINT, .target = e->target, .frames = 24});
            message(a, (SceneMessage){.kind = MSG_FAINT, .subject = e->target}, 60);
            break;
        case EV_FORFEIT:
            message(a, (SceneMessage){.kind = MSG_CANCEL}, 40);
            break;
        case EV_NO_MOVES:
            message(a, (SceneMessage){.kind = MSG_NO_MOVES}, 30);
            break;
        case EV_SHIFT:
            add(a, (AnimationStep){.kind = ANIM_SHIFT, .source = e->user, .target = e->target});
            message(a, (SceneMessage){.kind = MSG_SHIFT, .subject = e->target}, 22);
            break;
        case EV_ROTATE:
            add(a, (AnimationStep){.kind = ANIM_ROTATE, .target = e->target, .rotation = e->rotation});
            message(a, (SceneMessage){.kind = MSG_ROTATE, .subject = e->target}, 22);
            break;
        case EV_RECOIL:
            if (!battle_layout_anchor(layout, e->target)) break;
            message(a, (SceneMessage){.kind = MSG_RECOIL, .subject = e->target}, 0);
            add(a, (AnimationStep){.kind = ANIM_HP, .target = e->target, .hp = e->hp_after});
            wait(a, 18);
            break;
        case EV_DRAG_OUT:
        case EV_WITHDRAW:
            if (!battle_layout_anchor(layout, e->target)) break;
            message(a, (SceneMessage){.kind = e->kind == EV_DRAG_OUT ? MSG_DRAG_OUT : MSG_WITHDRAW,
                                      .subject = e->target}, 0);
            add(a, (AnimationStep){.kind = ANIM_WITHDRAW, .target = e->target, .frames = 18});
            break;
        case EV_SEND_OUT:
            if (!battle_layout_anchor(layout, e->target) || e->battler >= BATTLE_MAX_PER_SIDE) break;
            message(a, (SceneMessage){.kind = MSG_ENTER, .subject = e->target}, 0);
            add(a, (AnimationStep){.kind = ANIM_ENTER, .target = e->target, .frames = 18,
                                   .entrant = e->battler + 1, .hp = e->hp_after, .max_hp = e->amount});
            wait(a, 22);
            break;
        case EV_END:
            if (e->outcome == OUTCOME_WIN || e->outcome == OUTCOME_LOSE || e->outcome == OUTCOME_DRAW)
                message(a, (SceneMessage){.kind = MSG_RESULT, .outcome = e->outcome}, 30);
            break;
        }
    }
}

static bool advance(BattleAnimation *a, const BattleLayout *layout, const AnimationStep *s, unsigned f) {
    ActorFrame *actor = &a->actor[s->target.side][s->target.slot];
    const float t = s->frames ? (float)(f + 1) / s->frames : 0;
    switch (s->kind) {
    case ANIM_MESSAGE: a->message = s->message; return true;
    case ANIM_WAIT: break;
    case ANIM_FADE: a->fade = 1 - t; break;
    case ANIM_ENTER:
        if (!f && s->entrant)
            *actor = (ActorFrame){.battler = s->entrant - 1, .hp = s->hp, .max_hp = s->max_hp};
        actor->visible = true;
        actor->scale = t * (2 - t);
        break;
    case ANIM_ATTACK: {
        const BattlerAnchor *source = battle_layout_anchor(layout, s->source);
        const BattlerAnchor *target = battle_layout_anchor(layout, s->target);
        ActorFrame *user = &a->actor[s->source.side][s->source.slot];
        const float lunge = (t < 0.5f ? t : 1 - t) * 0.18f;
        user->offset = (ScreenPoint){
            (target->hit_center.x - source->attack_origin.x) * lunge,
            (target->hit_center.y - source->attack_origin.y) * lunge,
        };
        break;
    }
    case ANIM_HIT:
        actor->blink = (f / 4) % 2 == 0 && f < 24;
        actor->offset.x = f < 24 ? ((f / 2) % 2 ? 4 : -4) : 0;
        a->effect_visible = f < 12;
        a->effect = battle_layout_anchor(layout, s->target)->hit_center;
        break;
    case ANIM_HP: {
        const float step = actor->max_hp / (layout->active_per_side == 1 ? 48.0f : 22.0f);
        actor->hp -= step < 1 ? 1 : step;
        if (actor->hp > s->hp) return false;
        actor->hp = s->hp;
        return true;
    }
    case ANIM_WITHDRAW:
        actor->scale = 1 - t;
        if (t >= 1) actor->visible = false;
        break;
    case ANIM_FAINT:
        actor->drop = battle_layout_anchor(layout, s->target)->sprite_size * t;
        if (t >= 1) actor->visible = false;
        break;
    case ANIM_SHIFT: {
        ActorFrame *source = &a->actor[s->source.side][s->source.slot];
        const ActorFrame old = *actor;
        *actor = *source;
        *source = old;
        return true;
    }
    case ANIM_ROTATE: {
        ActorFrame *side = a->actor[s->target.side];
        ActorFrame old[BC_MAX_ACTIVE];
        memcpy(old, side, sizeof(old));
        for (uint8_t slot = 0; slot < BC_MAX_ACTIVE; ++slot)
            side[slot] = old[battle_rotation_origin(s->rotation, slot)];
        return true;
    }
    }
    return f + 1 >= s->frames;
}

bool animation_tick(BattleAnimation *a, const BattleLayout *layout) {
    while (a->index < a->count) {
        const AnimationStep *s = &a->steps[a->index];
        if (!advance(a, layout, s, a->frame++)) return false;
        a->frame = 0;
        ++a->index;
        if (s->kind != ANIM_MESSAGE) return false;
    }
    return true;
}
