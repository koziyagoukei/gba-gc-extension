#include "animation.h"

#include <assert.h>
#include <stdio.h>

static void finish(BattleAnimation *animation, const BattleLayout *layout) {
    unsigned frame;
    for (frame = 0; frame < 400 && !animation_tick(animation, layout); ++frame) {}
    assert(frame < 400);
}

int main(void) {
    const BattleLayout *single = battle_layout_find(BATTLE_FORMAT_SINGLE);
    const Position player = {0, 0}, opponent = {1, 0};
    const BattlerAnchor *p = battle_layout_anchor(single, player), *o = battle_layout_anchor(single, opponent);
    assert(p && o && p->feet.x < o->feet.x);
    assert(!battle_layout_anchor(single, (Position){1, 1}));
    assert(!battle_layout_anchor(single, (Position){2, 0}));
    const BattleLayout *double_layout = battle_layout_find(BATTLE_FORMAT_DOUBLE);
    const BattleLayout *triple = battle_layout_find(BATTLE_FORMAT_TRIPLE);
    const BattleLayout *rotation = battle_layout_find(BATTLE_FORMAT_ROTATION);
    assert(double_layout->active_per_side == 2 && triple->active_per_side == 3);
    assert(rotation->anchors[0][BPOS_CENTER].feet.y > rotation->anchors[0][BPOS_LEFT].feet.y);
    assert(!battle_layout_anchor(double_layout, (Position){0, 2}));

    Battle b = {0};
    for (unsigned side = 0; side < 2; ++side) {
        b.sides[side].count = b.sides[side].active_count = 1;
        b.sides[side].battlers[0].hp = b.sides[side].battlers[0].max_hp = 60;
    }
    BattleAnimation anim;
    animation_init(&anim, &b);
    animation_intro(&anim, single);
    finish(&anim, single);
    assert(anim.actor[0][0].visible && anim.actor[1][0].visible && anim.fade == 0);

    BattleLayout moved = *single;
    moved.anchors[1][0].hit_center = (ScreenPoint){510, 85};
    const EventLog log = {{
        {.kind=EV_MOVE_USED, .user=player, .target=opponent, .move_id=7},
        {.kind=EV_DAMAGE, .user=player, .target=opponent, .hp_after=0, .amount=60, .effectiveness=4},
        {.kind=EV_FAINT, .target=opponent},
        {.kind=EV_END, .outcome=OUTCOME_WIN},
    }, 4};
    b.sides[1].battlers[0].hp = 0;
    b.sides[1].battlers[0].fainted = true;
    animation_play(&anim, &moved, &log);
    assert(anim.actor[1][0].hp == 60); // Presentation hasn't consumed the damage yet.
    unsigned i;
    for (i = 0; i < 100 && !anim.effect_visible; ++i) animation_tick(&anim, &moved);
    assert(anim.effect_visible && anim.effect.x == 510 && anim.effect.y == 85);
    assert(anim.actor[1][0].blink);
    finish(&anim, &moved);
    assert(!anim.effect_visible && !anim.actor[1][0].visible && anim.actor[1][0].hp == 0);
    assert(anim.actor[0][0].visible && anim.actor[0][0].hp == 60);
    assert(anim.message.kind == MSG_RESULT && anim.message.outcome == OUTCOME_WIN);
    assert(b.sides[1].battlers[0].hp == 0 && b.sides[1].battlers[0].fainted);
    for (unsigned side = 0; side < 2; ++side) {
        b.sides[side].count = b.sides[side].active_count = 3;
        for (unsigned slot = 0; slot < 3; ++slot) {
            b.sides[side].active[slot] = slot;
            b.sides[side].battlers[slot].hp = b.sides[side].battlers[slot].max_hp = 30 + slot;
        }
    }
    animation_init(&anim, &b);
    animation_intro(&anim, triple);
    finish(&anim, triple);
    const EventLog formation = {{
        {.kind = EV_SHIFT, .user = {0, BPOS_LEFT}, .target = {0, BPOS_CENTER}},
        {.kind = EV_ROTATE, .user = {1, BPOS_RIGHT}, .target = {1, BPOS_CENTER}, .rotation = BROT_RIGHT},
    }, 2};
    animation_play(&anim, triple, &formation);
    finish(&anim, triple);
    assert(anim.actor[0][BPOS_CENTER].battler == 0 && anim.actor[0][BPOS_CENTER].hp == 30);
    assert(anim.actor[1][BPOS_CENTER].battler == 2 && anim.actor[1][BPOS_CENTER].hp == 32);
    const EventLog entrant = {{
        {.kind = EV_SEND_OUT, .target = {0, BPOS_LEFT}, .battler = 3, .amount = 90, .hp_after = 65},
    }, 1};
    animation_play(&anim, triple, &entrant);
    finish(&anim, triple);
    assert(anim.actor[0][BPOS_LEFT].visible && anim.actor[0][BPOS_LEFT].battler == 3);
    assert(anim.actor[0][BPOS_LEFT].hp == 65 && anim.actor[0][BPOS_LEFT].max_hp == 90);
    puts("presentation-test OK");
}
