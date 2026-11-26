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
            // A request built in memory (not decoded) gets the decoder's check
            // too, from the first version with item blocks (V17; M4-B1c-1b-fix:
            // V18 before), so no record of a later version reaches the core.
            if (!battle_item_valid(&e->mon.item, e->record.held_item, r->type_count, r->version))
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
    case BPRED_HP_AT_MOST: return mon->hp <= mon->max_hp / p.arg;  // V19 (HasEnoughHpToEatBerry)
    default: return false;  // refused by battle_item_valid
    }
}

// V19: a record with BIREC_CONSUMED that acted uses the item up. The roster
// Pokemon holds nothing from now on (item_from is the single source of what
// it holds: battle_held_item and bc_item follow it); the item stays in its
// history for later stages (Recycle, Harvest).
static void use_up(Battle *b, Position p, Battler *mon, const BattleItemRecord *x, uint8_t timing, EventLog *log) {
    if (!(x->flags & BIREC_CONSUMED)) return;
    const uint16_t item = battle_held_item(mon);
    mon->item_used = mon->item_from;
    mon->item_used_timing = timing;
    mon->item_used_turn = b->turn;
    mon->item_from = NULL;
    bc_emit(log, (BattleEvent){.kind = EV_ITEM_CONSUMED, .user = p, .target = p, .condition = timing, .item = item});
}

// `end` (BITIME_AFTER_MOVE only) is the move the holder just used: V20 damage
// acts if it landed, V21 BIHP_HEAL_DEALT heals the HP it took / divisor (at
// least 1) if it took any (TryShellBell: savedDmg > 0).
static void item_hp(Battle *b, Position p, uint8_t timing, const BcMoveEnd *end, EventLog *log) {
    Battler *mon = bc_mutable_at(b, p);
    if (!mon || mon->fainted) return;
    const BattleItemRecord *x = NULL;
    while ((x = bc_item_next(b, mon, BITEM_HP, x))) {
        if (x->hp.timing != timing || !holder_predicate(mon, x->hp.pred)) continue;
        const bool dealt = x->hp.op == BIHP_HEAL_DEALT;
        if (dealt ? !end || !end->lost : end && !end->landed) return;  // only a move's end knows the HP it took
        unsigned amount = (dealt ? end->lost : mon->max_hp) / x->hp.divisor;
        if (!amount) amount = 1;
        const uint16_t item = battle_held_item(mon);
        if (x->hp.op == BIHP_DAMAGE) {
            bc_apply_damage(b, (BattleEvent){.kind = EV_ITEM_DAMAGE, .user = p, .target = p, .item = item},
                            (uint16_t)amount, log);
            use_up(b, p, mon, x, timing, log);
            return;
        }
        if (mon->hp >= mon->max_hp) return;
        if (amount > (unsigned)(mon->max_hp - mon->hp)) amount = mon->max_hp - mon->hp;
        mon->hp = (uint16_t)(mon->hp + amount);
        bc_emit(log, (BattleEvent){.kind = EV_ITEM_HEAL, .user = p, .target = p, .amount = (uint16_t)amount,
                                   .hp_after = mon->hp, .item = item});
        use_up(b, p, mon, x, timing, log);
        return;
    }
}

void bc_item_hp(Battle *b, Position p, uint8_t timing, EventLog *log) { item_hp(b, p, timing, NULL, log); }

// MOVEEND_LIFE_ORB_SHELL_BELL. V20 TryLifeOrb: the move was used and landed
// (IsAnyTargetTurnDamaged including substitutes). V21 TryShellBell: the move
// took HP (savedDmg: every target's moveDamage, capped at its HP or its
// substitute's, summed over the hits), healed by 1/divisor of the sum. Both:
// the holder is the battler that started the move (originalBattlerPartyId)
// and stands (ItemBattleEffects), its item has the record (bc_item: items
// simulated, still held); a heal does nothing at full HP. Magic Guard and
// Sheer Force are the exporter's to refuse or normalise (docs/items.md 14.5,
// docs/shell-bell.md). An item has one after-move record (battle_item_valid).
void bc_item_after_move(Battle *b, Position user, const BcMoveEnd *end, EventLog *log) {
    if (!end || !end->origin || b->request->version < BATTLE_REQUEST_VERSION_MOVE_ITEMS ||
        battle_at(b, user) != end->origin)
        return;
    item_hp(b, user, BITIME_AFTER_MOVE, end, log);  // not used up
}

// ------------------------------------------------------------- V19 consumable items
// (docs/items.md 12). The check points are the reference's (ItemBattleEffects
// with IsOnHpThresholdActivation / IsOnStatusChangeActivation / on switch-in,
// and the endure step of GetAdjustedDamage); battle_core.c and
// battle_effects.c call them where the reference does.

bool bc_items_consumable(const Battle *b) {
    return b->request->version >= BATTLE_REQUEST_VERSION_CONSUMABLE_ITEMS && battle_items_simulated(b);
}

// ItemHealHp: a Pokemon on the field whose HP is at most max HP / arg heals
// max HP / divisor (at least 1) or a fixed amount, up to its max HP; the
// first record whose threshold holds acts. A fainted Pokemon eats nothing.
bool bc_item_hp_check(Battle *b, Position p, EventLog *log) {
    Battler *mon = bc_mutable_at(b, p);
    if (!bc_items_consumable(b) || !mon || mon->fainted || mon->hp >= mon->max_hp) return false;
    const BattleItemRecord *x = NULL;
    while ((x = bc_item_next(b, mon, BITEM_HP, x))) {
        if (x->hp.timing != BITIME_HP_THRESHOLD || !holder_predicate(mon, x->hp.pred)) continue;
        unsigned amount = x->hp.op == BIHP_HEAL_FIXED ? x->hp.divisor : mon->max_hp / x->hp.divisor;
        if (!amount) amount = 1;
        if (amount > (unsigned)(mon->max_hp - mon->hp)) amount = mon->max_hp - mon->hp;
        mon->hp = (uint16_t)(mon->hp + amount);
        bc_emit(log, (BattleEvent){.kind = EV_ITEM_HEAL, .user = p, .target = p, .amount = (uint16_t)amount,
                                   .hp_after = mon->hp, .item = battle_held_item(mon)});
        use_up(b, p, mon, x, BITIME_HP_THRESHOLD, log);
        return true;
    }
    return false;
}

// TryCure*: every condition of the record's masks the Pokemon has is cured,
// its major status first, then confusion (TryCureAnyStatus).
bool bc_item_status_check(Battle *b, Position p, EventLog *log) {
    Battler *mon = bc_mutable_at(b, p);
    if (!bc_items_consumable(b) || !mon || mon->fainted) return false;
    const BattleItemRecord *x = NULL;
    while ((x = bc_item_next(b, mon, BITEM_CURE, x))) {
        if (x->cure.timing != BITIME_STATUS) continue;
        const uint8_t kind = bc_status_kind(b, mon);
        const bool status = kind && (x->cure.status & (1u << kind));
        const bool confusion = (x->cure.volatiles & BIVOL_CONFUSION) && mon->vol.confusion;
        if (!status && !confusion) continue;
        const uint16_t item = battle_held_item(mon);
        if (status) {
            mon->status = BST_NONE;
            mon->sleep_start = 0;
            bc_emit(log, (BattleEvent){.kind = EV_ITEM_CURE, .user = p, .target = p, .condition = kind, .item = item});
        }
        if (confusion) {
            mon->vol.confusion = 0;
            bc_emit(log, (BattleEvent){.kind = EV_ITEM_CURE, .user = p, .target = p,
                                       .condition = BATTLE_CURED_CONFUSION, .item = item});
        }
        use_up(b, p, mon, x, BITIME_STATUS, log);
        return true;
    }
    return false;
}

// GetAdjustedDamage: after Sturdy, a hit that would take all the HP of a
// holder at full HP leaves 1 HP. Only the amount here; bc_item_endured
// announces it (and uses the item up) where the reference shows it.
uint16_t bc_item_endure(const Battle *b, Position target, uint16_t amount, bool *endured) {
    const Battler *mon = battle_at(b, target);
    *endured = false;
    if (!bc_items_consumable(b) || !mon || mon->hp != mon->max_hp || amount < mon->hp) return amount;
    const BattleItemRecord *x = NULL;
    while ((x = bc_item_next(b, mon, BITEM_ENDURE, x))) {
        if (x->endure.timing != BITIME_LETHAL_HIT || x->endure.condition != BIEND_FULL_HP) continue;
        *endured = true;
        return (uint16_t)(mon->hp - 1);
    }
    return amount;
}

void bc_item_endured(Battle *b, Position target, EventLog *log) {
    Battler *mon = bc_mutable_at(b, target);
    const BattleItemRecord *x = bc_item_next(b, mon, BITEM_ENDURE, NULL);
    if (!x) return;
    bc_emit(log, (BattleEvent){.kind = EV_ITEM_ENDURE, .user = target, .target = target,
                               .item = battle_held_item(mon)});
    use_up(b, target, mon, x, BITIME_LETHAL_HIT, log);
}

// On entry (IsOnSwitchInActivation): a status berry for a status it came in
// with, an HP berry for HP it came in with.
void bc_item_on_entry(Battle *b, Position p, EventLog *log) {
    if (!bc_item_status_check(b, p, log)) bc_item_hp_check(b, p, log);
}

// The reference's battler order (player left, opponent left, player right,
// ...) over the Pokemon on the field.
static unsigned battler_order(const Battle *b, Position *out) {
    Position actors[2][BC_MAX_ACTIVE];
    unsigned n[2], count = 0;
    for (uint8_t side = 0; side < 2; ++side) n[side] = battle_collect_actors(b, side, actors[side]);
    for (uint8_t slot = 0; slot < BC_MAX_ACTIVE; ++slot)
        for (uint8_t side = 0; side < 2; ++side)
            for (unsigned i = 0; i < n[side]; ++i)
                if (actors[side][i].slot == slot) out[count++] = actors[side][i];
    return count;
}

// MOVEEND_ITEM_EFFECTS_ATTACKER: a status berry, else an HP berry.
void bc_items_actor_check(Battle *b, Position actor, EventLog *log) {
    if (!bc_items_consumable(b)) return;
    if (!bc_item_status_check(b, actor, log)) bc_item_hp_check(b, actor, log);
}

// The end of an action: the actor (MOVEEND_ITEM_EFFECTS_ATTACKER_2, unless
// done already), every other Pokemon's HP (MOVEEND_KEE_MARANGA_HP_THRESHOLD_
// ITEM_TARGET), the after-move record of the move's user (`end`: V20 Life Orb,
// V21 Shell Bell; MOVEEND_LIFE_ORB_SHELL_BELL), then every Pokemon's status
// and HP (MOVEEND_ITEMS_EFFECTS_ALL). The after-move record comes after the
// other Pokemon's HP berries (M4-B1c-0c, docs/items.md 15.4).
void bc_items_after_action(Battle *b, Position actor, bool actor_checked, const BcMoveEnd *end, EventLog *log) {
    const bool items = bc_items_consumable(b);
    Position order[BC_MAX_ACTIONS];
    unsigned count = 0;
    if (items) {
        if (!actor_checked) bc_items_actor_check(b, actor, log);
        count = battler_order(b, order);
        for (unsigned i = 0; i < count; ++i)
            if (order[i].side != actor.side || order[i].slot != actor.slot) bc_item_hp_check(b, order[i], log);
    }
    bc_item_after_move(b, actor, end, log);
    for (unsigned i = 0; i < count; ++i)
        if (!bc_item_status_check(b, order[i], log)) bc_item_hp_check(b, order[i], log);
}
