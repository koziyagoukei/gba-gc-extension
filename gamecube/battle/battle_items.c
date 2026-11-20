// Held items of the GameCube battle core (requests V17 and V18, docs/items.md).
//
// Three layers, as for the weather and the terrain:
//   1. the state: the item a Pokemon holds now (Battler.item_from, the entry
//      whose item it is), separate from the item it started with (its own
//      entry's) so that later milestones can move or remove items;
//   2. the shared operations here: one check of whether an item has an effect
//      (bc_item), the record iterator, the battle-start checks and notice, and
//      the duration of a weather or terrain its holder sets, and (V18) the HP
//      records of one timing; the power records run through the shared
//      modifiers (battle_modifiers.c);
//   3. the game's rules, resolved by the GBA into the item blocks: percent,
//      category, move type, rounding and durations. The core knows no item id.
// An item with an effect the core cannot run is UNSUPPORTED: such a battle
// does not start unless RULE_IGNORE_ITEMS turns every item off, which the
// core then announces (EV_ITEMS_NORMALIZED). No item is ever run in part.

#include "battle_internal.h"

#include <stddef.h>

bool battle_items_simulated(const Battle *b) {
    return b->request->version >= BATTLE_REQUEST_VERSION_ITEMS && !(b->request->rules & RULE_IGNORE_ITEMS);
}

bool battle_items_normalized(const Battle *b) {
    return b->request->version >= BATTLE_REQUEST_VERSION_ITEMS && (b->request->rules & RULE_IGNORE_ITEMS);
}

uint16_t battle_held_item(const Battler *mon) { return mon->item_from ? mon->item_from->record.held_item : 0; }

const BattleItem *bc_item(const Battle *b, const Battler *mon) {
    if (!mon || !mon->item_from || !battle_items_simulated(b)) return NULL;
    const BattleItem *it = &mon->item_from->mon.item;
    return it->cls == BICLASS_RECORDS ? it : NULL;
}

const BattleItemRecord *bc_item_next(const Battle *b, const Battler *mon, uint8_t kind,
                                     const BattleItemRecord *previous) {
    const BattleItem *it = bc_item(b, mon);
    if (!it) return NULL;
    const BattleItemRecord *x = previous ? previous + 1 : it->records;
    for (; x < it->records + it->count; ++x)
        if (x->kind == kind) return x;
    return NULL;
}

int bc_items_check(const Battle *b) {
    const BattleRequest *r = b->request;
    if (r->version < BATTLE_REQUEST_VERSION_ITEMS) return BI_OK;
    const bool ignore = (r->rules & RULE_IGNORE_ITEMS) != 0, strict = (r->rules & RULE_STRICT_ITEMS) != 0;
    for (uint8_t side = 0; side < 2; ++side)
        for (uint8_t i = 0; i < b->sides[side].count; ++i) {
            const BattleEntry *e = b->sides[side].battlers[i].entry;
            const uint8_t cls = e->mon.item.cls;
            // V18: a request built in memory (not decoded) gets the decoder's
            // check too, so no HP record with a zero divisor reaches the core.
            if (r->version >= BATTLE_REQUEST_VERSION_END_TURN_ITEMS &&
                !battle_item_valid(&e->mon.item, e->record.held_item, r->type_count, r->version))
                return BI_CONDITIONS;
            if (cls == BICLASS_UNSUPPORTED && !ignore) return BI_UNSUPPORTED_ITEM;
            // Strict: normalising would drop an item effect, so the result would not be exact.
            if (strict && ignore && cls != BICLASS_NONE) return BI_ITEMS_NORMALIZED;
        }
    return BI_OK;
}

void bc_items_begin_battle(Battle *b, EventLog *log) {
    if (!battle_items_normalized(b)) return;
    unsigned held = 0, unsupported = 0;
    for (uint8_t side = 0; side < 2; ++side)
        for (uint8_t i = 0; i < b->sides[side].count; ++i) {
            const Battler *x = &b->sides[side].battlers[i];
            if (battle_held_item(x)) ++held;
            if (x->entry->mon.item.cls == BICLASS_UNSUPPORTED) ++unsupported;
        }
    bc_emit(log, (BattleEvent){.kind = EV_ITEMS_NORMALIZED, .user = {0, BATTLE_SLOT_NONE},
                               .target = {0, BATTLE_SLOT_NONE}, .amount = (uint16_t)held,
                               .battler = (uint8_t)unsupported});
}

// TryChangeWeather / TryChangeBattleTerrain: the setter's item fixes the
// duration of a weather or terrain set for a limited time (Damp Rock: 8
// turns), never one without an end or one that keeps the count (an ability
// weather before Gen 6), nor the battle's own start weather or terrain: the
// rules have no holder (their source slot is BATTLE_SLOT_NONE, so battle_at
// gives no Pokemon and bc_item_next no record).
uint8_t bc_item_field_turns(const Battle *b, Position source, uint8_t record_kind, uint8_t bit, uint8_t turns) {
    if (!turns || turns > BWEATHER_MAX_TURNS) return turns;  // no end, or BWEATHER_TURNS_KEEP
    const BattleItemRecord *x = NULL;
    while ((x = bc_item_next(b, battle_at(b, source), record_kind, x)))
        if (x->arg0 & bit) return x->arg2;
    return turns;
}

// V18: the holder's BITEM_HP records at one timing of the turn sequence
// (docs/items.md, "Order"). The first record whose predicate holds is the
// item's effect there (ItemBattleEffects: Black Sludge heals a Poison-type
// holder, else hurts it); a heal at full HP is that effect doing nothing.
// The amount is max HP / divisor, at least 1 (SetHealAmount,
// SetPassiveDamageAmount); a heal stops at max HP, damage goes through the
// core's own HP loss and faint (bc_apply_damage).
static bool holder_predicate(const Battler *mon, BattlePredicate p) {
    switch (p.kind) {
    case BPRED_NONE: return true;
    case BPRED_HAS_TYPE: return bc_has_type(mon, p.arg);
    default: return false;  // refused by battle_item_valid
    }
}

void bc_item_hp(Battle *b, Position p, uint8_t timing, EventLog *log) {
    Battler *mon = bc_mutable_at(b, p);
    if (!mon || mon->fainted) return;
    const BattleItemRecord *x = NULL;
    while ((x = bc_item_next(b, mon, BITEM_HP, x))) {
        if (x->hp.timing != timing || !holder_predicate(mon, x->hp.pred)) continue;
        unsigned amount = mon->max_hp / x->hp.divisor;
        if (!amount) amount = 1;
        const uint16_t item = battle_held_item(mon);
        if (x->hp.op == BIHP_DAMAGE) {
            bc_apply_damage(b, (BattleEvent){.kind = EV_ITEM_DAMAGE, .user = p, .target = p, .item = item},
                            (uint16_t)amount, log);
            return;
        }
        if (mon->hp >= mon->max_hp) return;
        if (amount > (unsigned)(mon->max_hp - mon->hp)) amount = mon->max_hp - mon->hp;
        mon->hp = (uint16_t)(mon->hp + amount);
        bc_emit(log, (BattleEvent){.kind = EV_ITEM_HEAL, .user = p, .target = p, .amount = (uint16_t)amount,
                                   .hp_after = mon->hp, .item = item});
        return;
    }
}
