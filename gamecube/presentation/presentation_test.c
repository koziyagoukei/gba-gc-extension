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

    const BattleRequest request = {.version = BATTLE_REQUEST_VERSION_CONDITIONS};
    Battle b = {.request = &request};
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
        {.kind = EV_WITHDRAW, .target = {0, BPOS_LEFT}, .battler = 1},
        {.kind = EV_SEND_OUT, .target = {0, BPOS_LEFT}, .battler = 3, .amount = 90, .hp_after = 65},
    }, 2};
    anim.actor[0][BPOS_LEFT].volatiles = BVF_SUBSTITUTE | BVF_CONFUSED;
    animation_play(&anim, triple, &entrant);
    for (unsigned i = 0; i < 18; ++i) animation_tick(&anim, triple);
    // The withdraw takes its 18 frames (badge clearing adds none) and drops the old badges.
    assert(!anim.actor[0][BPOS_LEFT].visible && anim.actor[0][BPOS_LEFT].battler == 1);
    assert(!anim.actor[0][BPOS_LEFT].volatiles);
    finish(&anim, triple);
    assert(anim.actor[0][BPOS_LEFT].visible && anim.actor[0][BPOS_LEFT].battler == 3);
    assert(!anim.actor[0][BPOS_LEFT].volatiles);
    assert(anim.actor[0][BPOS_LEFT].hp == 65 && anim.actor[0][BPOS_LEFT].max_hp == 90);
    // V9 badges: a substitute appears and breaks; a charge hides the sprite until the move is used.
    const EventLog volatiles = {{
        {.kind = EV_SUBSTITUTE, .user = {0, BPOS_LEFT}, .target = {0, BPOS_LEFT}, .amount = 22, .hp_after = 43},
        {.kind = EV_CHARGE, .user = {0, BPOS_CENTER}, .target = {0, BPOS_CENTER}, .condition = BSEMI_AIR},
    }, 2};
    animation_play(&anim, triple, &volatiles);
    finish(&anim, triple);
    assert(anim.actor[0][BPOS_LEFT].volatiles == BVF_SUBSTITUTE && anim.actor[0][BPOS_LEFT].hp == 43);
    assert(anim.actor[0][BPOS_CENTER].volatiles == (BVF_CHARGING | BVF_SEMI_INVULNERABLE));
    const EventLog release = {{
        {.kind = EV_MOVE_USED, .user = {0, BPOS_CENTER}, .target = {1, BPOS_CENTER}, .move_id = 19},
        {.kind = EV_SUBSTITUTE_HIT, .user = {1, BPOS_CENTER}, .target = {0, BPOS_LEFT}, .amount = 22},
        {.kind = EV_SUBSTITUTE_BREAK, .user = {1, BPOS_CENTER}, .target = {0, BPOS_LEFT}},
    }, 3};
    animation_play(&anim, triple, &release);
    finish(&anim, triple);
    assert(!anim.actor[0][BPOS_CENTER].volatiles && !anim.actor[0][BPOS_LEFT].volatiles);
    assert(anim.actor[0][BPOS_LEFT].visible && !anim.actor[0][BPOS_LEFT].blink && anim.actor[0][BPOS_LEFT].offset.x == 0);
    anim.actor[0][BPOS_LEFT].hp = 65;  // the status block below starts from the entrant's HP
    const EventLog condition = {{
        {.kind = EV_STATUS, .target = {0, BPOS_LEFT}, .condition = BST_BURN},
        {.kind = EV_RESIDUAL, .target = {0, BPOS_LEFT}, .condition = BST_BURN, .hp_after = 60},
    }, 2};
    animation_play(&anim, triple, &condition);
    assert(anim.actor[0][BPOS_LEFT].condition == BST_NONE && anim.actor[0][BPOS_LEFT].hp == 65);
    finish(&anim, triple);
    assert(anim.actor[0][BPOS_LEFT].condition == BST_BURN && anim.actor[0][BPOS_LEFT].hp == 60);
    const EventLog cure = {{ {.kind = EV_CURE, .target = {0, BPOS_LEFT}, .condition = BST_BURN} }, 1};
    animation_play(&anim, triple, &cure);
    finish(&anim, triple);
    assert(anim.actor[0][BPOS_LEFT].condition == BST_NONE);
    // V11 abilities: announcement with the ability id, Rough Skin damage, not-simulated notice.
    const EventLog abilities = {{
        {.kind = EV_ABILITY, .user = {1, BPOS_CENTER}, .target = {0, BPOS_LEFT}, .ability = 24},
        {.kind = EV_ABILITY_DAMAGE, .user = {1, BPOS_CENTER}, .target = {0, BPOS_LEFT}, .ability = 24,
         .amount = 11, .hp_after = 49},
        {.kind = EV_ABILITY_UNSUPPORTED, .user = {1, BATTLE_SLOT_NONE}, .battler = 4, .ability = 104},
    }, 3};
    animation_play(&anim, triple, &abilities);
    bool announced = false;
    for (unsigned i = 0; i < 400 && !animation_tick(&anim, triple); ++i)
        announced |= anim.message.kind == MSG_ABILITY && anim.message.ability == 24 &&
                     anim.message.subject.side == 1 && anim.message.subject.slot == BPOS_CENTER;
    assert(announced && anim.actor[0][BPOS_LEFT].hp == 49);
    assert(anim.message.kind == MSG_ABILITY_UNSUPPORTED && anim.message.ability == 104 && anim.message.battler == 4);
    // V17: the battle-start notice that every held item is off (3 held, 1 not simulated).
    const EventLog items = {{
        {.kind = EV_ITEMS_NORMALIZED, .user = {0, BATTLE_SLOT_NONE}, .target = {0, BATTLE_SLOT_NONE}, .amount = 3,
         .battler = 1},
    }, 1};
    animation_play(&anim, triple, &items);
    finish(&anim, triple);
    assert(anim.message.kind == MSG_ITEMS_NORMALIZED && anim.message.stage == 3 && anim.message.battler == 1);
    // V18: an item heal (Leftovers) and item damage (Black Sludge) with the item id and the HP bar.
    const EventLog item_hp = {{
        {.kind = EV_ITEM_HEAL, .user = {0, BPOS_LEFT}, .target = {0, BPOS_LEFT}, .amount = 3, .hp_after = 52,
         .item = 472},
        {.kind = EV_ITEM_DAMAGE, .user = {1, BPOS_RIGHT}, .target = {1, BPOS_RIGHT}, .amount = 20, .hp_after = 7,
         .item = 487},
    }, 2};
    animation_play(&anim, triple, &item_hp);
    bool healed = false;
    for (unsigned i = 0; i < 400 && !animation_tick(&anim, triple); ++i)
        healed |= anim.message.kind == MSG_ITEM_HEAL && anim.message.item == 472 && anim.message.subject.side == 0;
    assert(healed && anim.actor[0][BPOS_LEFT].hp == 52 && anim.actor[1][BPOS_RIGHT].hp == 7);
    assert(anim.message.kind == MSG_ITEM_DAMAGE && anim.message.item == 487 && anim.message.subject.slot == BPOS_RIGHT);
    // V19: a status berry clears the status icon, a confusion cure the confusion
    // mark; Focus Sash is a message; the use itself shows nothing more.
    const EventLog consumable = {{
        {.kind = EV_STATUS, .user = {1, BPOS_LEFT}, .target = {0, BPOS_LEFT}, .condition = BST_PARALYSIS},
        {.kind = EV_ITEM_CURE, .user = {0, BPOS_LEFT}, .target = {0, BPOS_LEFT}, .condition = BST_PARALYSIS,
         .item = 514},
        {.kind = EV_ITEM_CONSUMED, .user = {0, BPOS_LEFT}, .target = {0, BPOS_LEFT}, .condition = BITIME_STATUS,
         .item = 514},
        {.kind = EV_CONFUSE, .user = {0, BPOS_LEFT}, .target = {1, BPOS_RIGHT}},
        {.kind = EV_ITEM_CURE, .user = {1, BPOS_RIGHT}, .target = {1, BPOS_RIGHT},
         .condition = BATTLE_CURED_CONFUSION, .item = 522},
        {.kind = EV_ITEM_CONSUMED, .user = {1, BPOS_RIGHT}, .target = {1, BPOS_RIGHT}, .item = 522},
        {.kind = EV_ITEM_ENDURE, .user = {1, BPOS_CENTER}, .target = {1, BPOS_CENTER}, .item = 481},
        {.kind = EV_ITEM_CONSUMED, .user = {1, BPOS_CENTER}, .target = {1, BPOS_CENTER}, .item = 481},
    }, 8};
    animation_play(&anim, triple, &consumable);
    bool cured = false, unconfused = false;
    for (unsigned i = 0; i < 600 && !animation_tick(&anim, triple); ++i) {
        cured |= anim.message.kind == MSG_ITEM_CURE && anim.message.item == 514 &&
                 anim.message.condition == BST_PARALYSIS;
        unconfused |= anim.message.kind == MSG_ITEM_CURE && anim.message.condition == BATTLE_CURED_CONFUSION;
    }
    assert(cured && unconfused && anim.actor[0][BPOS_LEFT].condition == BST_NONE &&
           !(anim.actor[1][BPOS_RIGHT].volatiles & BVF_CONFUSED));
    assert(anim.message.kind == MSG_ITEM_ENDURE && anim.message.item == 481 && anim.message.subject.slot == BPOS_CENTER);
    puts("presentation-test OK");
}
