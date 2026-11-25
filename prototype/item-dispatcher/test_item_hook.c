#include "item_hook.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

static const ItemRecord orb_factor = {ITEM_FINAL_DAMAGE, EFFECT_FINAL_FACTOR, 5324, false};
static const ItemRecord orb_loss = {ITEM_AFTER_MOVE, EFFECT_SELF_DAMAGE, 10, false};

static AfterMoveFacts eligible(void)
{
    return (AfterMoveFacts){.max_hp = 300, .alive = true, .same_actor = true,
                            .landed = true, .items_enabled = true};
}

/* Test-only arithmetic adapter. The production integration uses the existing final chain. */
static unsigned half_down(unsigned left, unsigned right)
{
    return (unsigned)(((uint64_t)left * right + 2047u) >> 12);
}

static void final_damage(void)
{
    const uint16_t factor = item_final_factor(&orb_factor, true, false);
    assert(factor == 5324);
    assert(half_down(factor, 55) == 71 && half_down(factor, 51) == 66);
    assert(half_down(factor, 46) == 60 && half_down(factor, 675) == 877);
    assert(half_down(5325, 675) == 878);
    assert(half_down(factor, 61) == 79 && half_down(factor, 57) == 74);
    assert(half_down(factor, 24) == 31);
    assert(half_down(factor, 69) == 90 && half_down(factor, 42) == 55);
    assert(half_down(half_down(2048, factor), 108) == 70);
    assert(half_down(item_final_factor(&orb_factor, true, true), 50) == 50);
    assert(item_final_factor(&orb_factor, false, false) == 4096);
    assert(item_final_factor(NULL, true, false) == 4096);
}

static void damage_floor_and_gates(void)
{
    const uint16_t hp_values[] = {300, 200, 109, 105, 9, 1};
    const uint16_t losses[] = {30, 20, 10, 10, 1, 1};
    for (size_t i = 0; i < sizeof(hp_values) / sizeof(hp_values[0]); ++i) {
        AfterMoveFacts facts = eligible();
        MoveItemProgress progress = {0};
        facts.max_hp = hp_values[i];
        const ItemOutcome out = item_after_move(&progress, &orb_loss, facts);
        assert(out.decision == ITEM_APPLY_DAMAGE && out.damage == losses[i]);
        assert(item_after_move(&progress, &orb_loss, facts).decision == ITEM_ALREADY_VISITED);
    }

    AfterMoveFacts blocked[] = {eligible(), eligible(), eligible(), eligible(), eligible(), eligible()};
    blocked[0].alive = false;             /* lost HP to contact or recoil */
    blocked[1].same_actor = false;        /* already replaced mid-move */
    blocked[2].landed = false;            /* miss, immunity or no damaging hit */
    blocked[3].items_enabled = false;     /* explicit ignore or an ineffective item */
    blocked[4].indirect_immune = true;    /* Magic Guard fact */
    blocked[5].skip_late_move_end = true; /* Sheer Force fact */
    for (size_t i = 0; i < sizeof(blocked) / sizeof(blocked[0]); ++i) {
        MoveItemProgress progress = {0};
        const ItemOutcome out = item_after_move(&progress, &orb_loss, blocked[i]);
        assert(out.decision == ITEM_NO_EFFECT && out.damage == 0);
        assert(item_after_move(&progress, &orb_loss, eligible()).decision == ITEM_ALREADY_VISITED);
    }
}

typedef enum {
    HIT, SUBSTITUTE_HIT, CONTACT, TARGET_FAINT, HIT_COUNT, DRAIN, RECOIL, ACTOR_CHECK,
    MOXIE, ITEM_DAMAGE, HOLDER_FAINT, ALL_ITEMS, PIVOT
} Event;

typedef struct {
    Event events[16];
    size_t count;
    uint16_t hp;
} Scenario;

static void emit(Scenario *scenario, Event event)
{
    assert(scenario->count < sizeof(scenario->events) / sizeof(scenario->events[0]));
    scenario->events[scenario->count++] = event;
}

static void expect(const Scenario *scenario, const Event *events, size_t count)
{
    assert(scenario->count == count);
    for (size_t i = 0; i < count; ++i)
        assert(scenario->events[i] == events[i]);
}

/* Core-side adapter for testing order and immediate re-entry. */
static void resolve_item(Scenario *s, MoveItemProgress *progress, AfterMoveFacts facts)
{
    facts.alive = s->hp != 0;
    const ItemOutcome out = item_after_move(progress, &orb_loss, facts);
    assert(out.decision == ITEM_APPLY_DAMAGE);
    assert(item_after_move(progress, &orb_loss, facts).decision == ITEM_ALREADY_VISITED);
    s->hp = out.damage < s->hp ? s->hp - out.damage : 0;
    emit(s, ITEM_DAMAGE);
    if (!s->hp)
        emit(s, HOLDER_FAINT);
}

static void move_scope_and_order(void)
{
    const Event multi[] = {HIT, HIT, HIT_COUNT, RECOIL, ACTOR_CHECK, MOXIE, ITEM_DAMAGE, ALL_ITEMS};
    Scenario s = {.hp = 300};
    MoveItemProgress progress = {0};
    emit(&s, HIT);
    emit(&s, HIT);
    emit(&s, HIT_COUNT);
    emit(&s, RECOIL);
    emit(&s, ACTOR_CHECK);
    emit(&s, MOXIE);
    resolve_item(&s, &progress, eligible());
    emit(&s, ALL_ITEMS);
    assert(s.hp == 270);
    expect(&s, multi, sizeof(multi) / sizeof(multi[0]));

    const Event spread[] = {HIT, SUBSTITUTE_HIT, DRAIN, ACTOR_CHECK, MOXIE, ITEM_DAMAGE, ALL_ITEMS, PIVOT};
    s = (Scenario){.hp = 300};
    progress = (MoveItemProgress){0};
    emit(&s, HIT);
    emit(&s, SUBSTITUTE_HIT);
    emit(&s, DRAIN);
    emit(&s, ACTOR_CHECK);
    emit(&s, MOXIE);
    resolve_item(&s, &progress, eligible());
    emit(&s, ALL_ITEMS);  /* existing GC order; ITM-TIME-003 remains open */
    emit(&s, PIVOT);
    assert(s.hp == 270);
    expect(&s, spread, sizeof(spread) / sizeof(spread[0]));

    const Event lethal[] = {HIT, TARGET_FAINT, CONTACT, ITEM_DAMAGE, HOLDER_FAINT};
    s = (Scenario){.hp = 1};
    progress = (MoveItemProgress){0};
    emit(&s, HIT);
    emit(&s, TARGET_FAINT); /* ABL-G04 is outside the after-move hook. */
    emit(&s, CONTACT);
    resolve_item(&s, &progress, eligible());
    assert(!s.hp);
    expect(&s, lethal, sizeof(lethal) / sizeof(lethal[0]));

    const Event substitute_only[] = {SUBSTITUTE_HIT, ITEM_DAMAGE};
    s = (Scenario){.hp = 300};
    progress = (MoveItemProgress){0};
    emit(&s, SUBSTITUTE_HIT);
    resolve_item(&s, &progress, eligible());
    assert(s.hp == 270);
    expect(&s, substitute_only, sizeof(substitute_only) / sizeof(substitute_only[0]));
}

static void absent_after_consumption(void)
{
    /* The adapter represents the existing V19 use_up transfer, not a new consumable record. */
    const ItemRecord *held = &orb_loss;
    const ItemRecord *history = held;
    held = NULL;
    MoveItemProgress progress = {0};
    assert(item_after_move(&progress, held, eligible()).decision == ITEM_NO_EFFECT);
    assert(item_after_move(&progress, held, eligible()).decision == ITEM_ALREADY_VISITED);
    progress = (MoveItemProgress){0};     /* the next move still sees the current empty slot */
    assert(item_after_move(&progress, held, eligible()).decision == ITEM_NO_EFFECT);
    assert(history == &orb_loss);        /* a history entry is never a live item */
}

static void invalid_input(void)
{
    const ItemRecord invalid[] = {
        {ITEM_AFTER_MOVE, EFFECT_SELF_DAMAGE, 0, false},
        {ITEM_AFTER_MOVE, EFFECT_SELF_DAMAGE, 1, false},
        {ITEM_AFTER_MOVE, EFFECT_SELF_DAMAGE, 256, false},
        {ITEM_AFTER_MOVE, EFFECT_FINAL_FACTOR, 10, false},
        {ITEM_AFTER_MOVE, EFFECT_SELF_DAMAGE, 10, true},
    };
    MoveItemProgress progress = {0};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(!item_record_valid(&invalid[i]));
        assert(item_after_move(&progress, &invalid[i], eligible()).decision == ITEM_INVALID_RECORD);
        assert(!progress.after_move_seen);
    }
    const ItemRecord contact = {ITEM_CONTACT_HIT, EFFECT_SELF_DAMAGE, 6, false};
    assert(!item_record_valid(&contact));
    assert(item_after_move(&progress, &contact, eligible()).decision == ITEM_UNSUPPORTED_POINT);
    assert(item_after_move(&progress, &orb_factor, eligible()).decision == ITEM_UNSUPPORTED_POINT);
    assert(!progress.after_move_seen);
    assert(item_after_move(&progress, &orb_loss, eligible()).decision == ITEM_APPLY_DAMAGE);
}

int main(void)
{
    final_damage();
    damage_floor_and_gates();
    move_scope_and_order();
    absent_after_consumption();
    invalid_input();
    puts("item hook prototype: 5 test groups passed");
    return 0;
}
