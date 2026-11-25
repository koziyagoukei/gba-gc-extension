#include "item_hook.h"

bool item_record_valid(const ItemRecord *record)
{
    if (!record || record->consume)
        return false;
    switch (record->point) {
    case ITEM_FINAL_DAMAGE:
        return record->effect == EFFECT_FINAL_FACTOR && record->value > 0;
    case ITEM_AFTER_MOVE:
        return record->effect == EFFECT_SELF_DAMAGE && record->value >= 2 && record->value <= 255;
    default:
        return false;
    }
}

uint16_t item_final_factor(const ItemRecord *record, bool items_enabled, bool fixed_damage)
{
    if (!items_enabled || fixed_damage || !item_record_valid(record) || record->point != ITEM_FINAL_DAMAGE)
        return 4096;
    return record->value;
}

ItemOutcome item_after_move(MoveItemProgress *progress, const ItemRecord *record, AfterMoveFacts facts)
{
    if (record && record->point != ITEM_AFTER_MOVE)
        return (ItemOutcome){.decision = ITEM_UNSUPPORTED_POINT};
    if (record && !item_record_valid(record))
        return (ItemOutcome){.decision = ITEM_INVALID_RECORD};
    if (progress->after_move_seen)
        return (ItemOutcome){.decision = ITEM_ALREADY_VISITED};

    /* The stage is settled even if there is no effective item or the move missed. */
    progress->after_move_seen = true;
    if (!record || !facts.items_enabled || !facts.alive || !facts.same_actor ||
        !facts.landed || facts.indirect_immune || facts.skip_late_move_end)
        return (ItemOutcome){.decision = ITEM_NO_EFFECT};

    const uint16_t amount = facts.max_hp / record->value;
    return (ItemOutcome){.decision = ITEM_APPLY_DAMAGE, .damage = amount ? amount : 1};
}
