#include "battle_internal.h"

#include <string.h>

uint32_t bc_next_random(Battle *b) {
    uint32_t x = b->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return b->rng = x;
}

const char *battle_init_error_name(int error) {
    switch (error) {
    case BI_OK: return "ok";
    case BI_FORMAT: return "invalid format or active count";
    case BI_PARTY_SIZE: return "invalid roster or result policy";
    case BI_NO_PLAYER_MOVES: return "player has no supported move";
    case BI_NO_OPPONENT_MOVES: return "opponent has no supported move";
    case BI_BAD_TYPES: return "type data out of range";
    case BI_CONDITIONS: return "invalid V7 condition rules or status";
    case BI_FAINTED_LEAD: return "a lead Pokemon has no HP";
    }
    return "unknown";
}

bool bc_conditions_enabled(const Battle *b) {
    return b->request->version >= BATTLE_REQUEST_VERSION_CONDITIONS;
}

bool bc_has_type(const Battler *mon, uint8_t type) {
    return mon->types[0] == type || (mon->type_count == 2 && mon->types[1] == type);
}

unsigned bc_stage_scale(unsigned value, int stage, unsigned base) {
    if (stage < -6) stage = -6;
    if (stage > 6) stage = 6;
    return stage < 0 ? value * base / (base - stage) : value * (base + stage) / base;
}

uint8_t bc_status_kind(const Battle *b, const Battler *mon) {
    return bc_conditions_enabled(b) ? BATTLE_CONDITION_KIND(mon->status) : BST_NONE;
}

static unsigned speed(const Battle *b, const Battler *mon) {
    if (!bc_conditions_enabled(b)) return mon->speed;
    unsigned value = bc_stage_scale(mon->speed, mon->stat_stages[BSTAT_SPE], 2);
    if (bc_status_kind(b, mon) == BST_PARALYSIS)
        value /= b->request->conditions.paralysis_speed_divisor;
    return value ? value : 1;
}

bool battle_move_supported(const BattleRequest *r, const BattleMove *m) {
    if (m->unsupported || m->type >= r->type_count || m->accuracy > 100) return false;
    const uint8_t target = m->target & BTARGET_CLASS_MASK;
    if (target < BTARGET_SINGLE || target > BTARGET_SELF) return false;
    if (r->version >= BATTLE_REQUEST_VERSION_EFFECTS) return bc_effects_supported(r, m);
    if (!battle_effect_valid(m->effect, r->version)) return false;
    if (m->effect >= BME_STAGE_BASE) {
        if (m->category != BCAT_STATUS || m->power) return false;
        if (m->effect == BATTLE_STATUS_EFFECT(BST_FROSTBITE) && !(r->conditions.flags & BCR_FROSTBITE))
            return false;
        if (m->effect == BATTLE_STATUS_EFFECT(BST_FREEZE) && (r->conditions.flags & BCR_FROSTBITE))
            return false;
        return true;
    }
    if (m->effect != BME_HIT) {
        if (r->version < BATTLE_REQUEST_VERSION_DRAG || r->battle_type != BTYPE_TRAINER || target != BTARGET_SINGLE ||
            m->effect > (r->version >= BATTLE_REQUEST_VERSION_PIVOT ? BME_PIVOT : BME_FORCE_OUT_ON_HIT)) return false;
        if (m->effect == BME_FORCE_OUT) return m->category == BCAT_STATUS && !m->power;
    }
    return m->power && (m->category == BCAT_PHYSICAL || m->category == BCAT_SPECIAL);
}

static void load_battler(Battler *out, const BattleEntry *e) {
    const PokemonRecord *r = &e->record;
    memset(out, 0, sizeof(*out));
    out->entry = e;
    out->party_slot = r->slot;
    out->level = r->level;
    out->type_count = e->mon.type_count;
    out->types[0] = e->mon.types[0];
    out->types[1] = e->mon.types[1];
    out->hp = r->hp;
    out->max_hp = r->max_hp;
    out->attack = r->attack;
    out->defense = r->defense;
    out->sp_attack = r->sp_attack;
    out->sp_defense = r->sp_defense;
    out->speed = r->speed;
    out->status = r->status;
    out->sleep_start = BATTLE_CONDITION_COUNTER(r->status);
    for (unsigned i = 0; i < 4; ++i) {
        out->moves[i] = e->mon.moves[i];
        out->move_ids[i] = r->moves[i];
        out->pp[i] = r->pp[i];
        out->max_pp[i] = r->max_pp[i];
    }
    out->fainted = r->hp == 0;
    out->vol.charge_slot = BATTLE_SLOT_NONE;
}

unsigned battle_formation_size(uint8_t format) {
    switch (format) {
    case BATTLE_FORMAT_SINGLE: return 1;
    case BATTLE_FORMAT_DOUBLE: return 2;
    case BATTLE_FORMAT_TRIPLE:
    case BATTLE_FORMAT_ROTATION: return 3;
    default: return 0;
    }
}

static bool field_position(const Battle *b, Position p) {
    return p.side < 2 && p.slot < b->sides[p.side].active_count;
}

static bool roster_battle(const Battle *b) {
    return b->request->version >= BATTLE_REQUEST_VERSION_ROSTER;
}

const Battler *battle_at(const Battle *b, Position p) {
    if (!field_position(b, p)) return NULL;
    const Side *s = &b->sides[p.side];
    const uint8_t index = s->active[p.slot];
    return index < s->count ? &s->battlers[index] : NULL;
}

Battler *bc_mutable_at(Battle *b, Position p) {
    if (!field_position(b, p)) return NULL;
    Side *s = &b->sides[p.side];
    const uint8_t index = s->active[p.slot];
    return index < s->count ? &s->battlers[index] : NULL;
}

static bool rotation_battle(const Battle *b) {
    return b->request->format == BATTLE_FORMAT_ROTATION;
}

Battler *battle_active(Battle *b, uint8_t side) {
    return bc_mutable_at(b, (Position){side, rotation_battle(b) ? BPOS_CENTER : 0});
}

uint8_t battle_rotation_slot(uint8_t rotation) {
    switch (rotation) {
    case BROT_KEEP: return BPOS_CENTER;
    case BROT_LEFT: return BPOS_LEFT;
    case BROT_RIGHT: return BPOS_RIGHT;
    default: return BATTLE_SLOT_NONE;
    }
}

uint8_t battle_rotation_origin(uint8_t rotation, uint8_t destination) {
    static const uint8_t origins[3][3] = {
        {BPOS_LEFT, BPOS_CENTER, BPOS_RIGHT},
        {BPOS_RIGHT, BPOS_LEFT, BPOS_CENTER},
        {BPOS_CENTER, BPOS_RIGHT, BPOS_LEFT},
    };
    return origins[rotation][destination];
}

bool battle_adjacent(const Battle *b, Position a, Position d) {
    static const uint8_t triple_neighbors[] = {0x3, 0x7, 0x6};
    if (!field_position(b, a) || !field_position(b, d) || (a.side == d.side && a.slot == d.slot)) return false;
    if (b->request->format == BATTLE_FORMAT_TRIPLE) return triple_neighbors[a.slot] & (1u << d.slot);
    if (rotation_battle(b)) return a.slot == BPOS_CENTER && d.slot == BPOS_CENTER;
    return true;
}

bool battle_can_shift(const Battle *b, Position p) {
    const Battler *x = battle_at(b, p);
    return b->request->format == BATTLE_FORMAT_TRIPLE && p.slot != BPOS_CENTER && x && !x->fainted;
}

uint8_t battle_target_mask(const Battle *b, Position user, const BattleMove *m, uint8_t side) {
    if (side > 1 || !battle_at(b, user)) return 0;
    const uint8_t target = m->target & BTARGET_CLASS_MASK;
    if (target == BTARGET_SELF) return side == user.side ? 1u << user.slot : 0;
    if (side == user.side && target != BTARGET_ALL_ADJACENT) return 0;
    uint8_t mask = 0;
    for (uint8_t slot = 0; slot < b->sides[side].active_count; ++slot) {
        const Position p = {side, slot};
        const Battler *target_mon = battle_at(b, p);
        if ((p.side == user.side && p.slot == user.slot) || !target_mon || target_mon->fainted) continue;
        if (rotation_battle(b) && slot != BPOS_CENTER) continue;
        if ((m->target & BTARGET_IGNORE_DISTANCE) || battle_adjacent(b, user, p))
            mask |= 1u << slot;
    }
    return mask;
}

uint8_t battle_command_targets(const Battle *b, Position user, unsigned slot) {
    const Battler *x = battle_at(b, user);
    if (!x || x->fainted || slot >= 4 || !(battle_usable_moves(b, x) & (1u << slot))) return 0;
    const BattleMove *m = &x->moves[slot];
    const uint8_t target = m->target & BTARGET_CLASS_MASK;
    if (rotation_battle(b))
        return target == BTARGET_SELF ? BATTLE_TARGET_AUTO :
               (1u << BPOS_CENTER) | (target == BTARGET_SINGLE ? 0 : BATTLE_TARGET_AUTO);
    const uint8_t foes = battle_target_mask(b, user, m, 1 - user.side);
    if (target == BTARGET_SINGLE) return foes;
    return (foes || battle_target_mask(b, user, m, user.side)) ? foes | BATTLE_TARGET_AUTO : 0;
}

unsigned battle_collect_actors(const Battle *b, uint8_t side, Position *out) {
    if (rotation_battle(b)) {
        out[0] = (Position){side, BPOS_CENTER};
        return 1;
    }
    unsigned count = 0;
    for (uint8_t slot = 0; slot < b->sides[side].active_count; ++slot) {
        const Position pos = {side, slot};
        const Battler *mon = battle_at(b, pos);
        if (mon && !mon->fainted) out[count++] = pos;
    }
    return count;
}

uint8_t battle_usable_moves(const Battle *b, const Battler *x) {
    uint8_t mask = 0;
    for (unsigned i = 0; i < 4; ++i)
        if (x->move_ids[i] && x->pp[i] > 0 && battle_move_supported(b->request, &x->moves[i])) mask |= 1u << i;
    return mask;
}

int battle_init(Battle *b, const BattleRequest *r, uint32_t seed) {
    memset(b, 0, sizeof(*b));
    b->request = r;
    b->rng = seed ? seed : 0x9e3779b9u;
    const unsigned active = battle_formation_size(r->format);
    if (!active || r->active_per_side != active ||
        r->version > BATTLE_REQUEST_VERSION_VOLATILES)
        return BI_FORMAT;
    if (r->version >= BATTLE_REQUEST_VERSION_VOLATILES && !battle_volatile_rules_valid(&r->volatiles))
        return BI_CONDITIONS;
    if (!r->type_count || r->type_count > BATTLE_MAX_TYPES) return BI_BAD_TYPES;
    if (r->version >= BATTLE_REQUEST_VERSION_STRUGGLE &&
        (!r->struggle_power || r->struggle_accuracy > 100 ||
         (r->struggle_recoil != BSTRUGGLE_MAX_HP_FLOOR && r->struggle_recoil != BSTRUGGLE_MAX_HP_NEAREST)))
        return BI_FORMAT;
    if (bc_conditions_enabled(b) && ((r->rules & RULE_IGNORE_STATUS) ||
        !battle_condition_rules_valid(&r->conditions, r->type_count))) return BI_CONDITIONS;
    const bool roster = roster_battle(b);
    if (!r->player_count || !r->opponent_count || r->player_count > BATTLE_MAX_PER_SIDE ||
        r->opponent_count > BATTLE_MAX_PER_SIDE ||
        (!roster && (r->player_count != active || r->opponent_count != active)) ||
        ((roster || active > 1) && r->result_policy != RESULT_POLICY_DISPLAY_ONLY))
        return BI_PARTY_SIZE;
    for (unsigned side = 0; side < 2; ++side) {
        Side *s = &b->sides[side];
        const BattleEntry *entries = side ? r->opponent : r->player;
        s->count = side ? r->opponent_count : r->player_count;
        s->active_count = active;
        memset(s->active, BATTLE_SLOT_NONE, sizeof(s->active));
        unsigned sent = 0;
        for (unsigned i = 0; i < s->count; ++i) {
            load_battler(&s->battlers[i], &entries[i]);
            if (r->version < BATTLE_REQUEST_VERSION_EFFECTS)
                for (unsigned m = 0; m < 4; ++m) bc_translate_legacy_effects(&s->battlers[i].moves[m]);
            const Battler *x = &s->battlers[i];
            if (x->type_count < 1 || x->type_count > 2 || x->types[0] >= r->type_count ||
                (x->type_count == 2 && x->types[1] >= r->type_count)) return BI_BAD_TYPES;
            if (bc_conditions_enabled(b) && !battle_condition_valid(x->status, &r->conditions))
                return BI_CONDITIONS;
            if (x->party_slot == BATTLE_SLOT_NONE || x->hp > x->max_hp) return BI_PARTY_SIZE;
            for (unsigned j = 0; j < i; ++j)
                if (s->battlers[j].party_slot == x->party_slot) return BI_PARTY_SIZE;
            if (x->fainted) {
                if (!roster) return BI_FAINTED_LEAD;
                continue;
            }
            if (!roster && !battle_usable_moves(b, x)) return side ? BI_NO_OPPONENT_MOVES : BI_NO_PLAYER_MOVES;
            if (sent < active) {
                static const uint8_t rotation_order[] = {BPOS_CENTER, BPOS_LEFT, BPOS_RIGHT};
                const unsigned slot = roster && rotation_battle(b) ? rotation_order[sent] : sent;
                s->active[slot] = i;
                ++sent;
            }
        }
        if (!sent) return BI_FAINTED_LEAD;
    }
    return BI_OK;
}

static const BattleMove struggle_target = {.target = BTARGET_SINGLE};

bool battle_can_struggle(const Battle *b, Position user) {
    if (b->request->version < BATTLE_REQUEST_VERSION_STRUGGLE) return false;
    const Battler *mon = battle_at(b, user);
    if (!mon || mon->fainted) return false;
    // A move with PP which this engine cannot model is still a move.
    for (unsigned slot = 0; slot < 4; ++slot)
        if (mon->move_ids[slot] && mon->pp[slot]) return false;
    return rotation_battle(b) || battle_target_mask(b, user, &struggle_target, 1 - user.side) != 0;
}

bool battle_validate_action(const Battle *b, const Action *a) {
    if (a->kind == ACT_FORFEIT) return a->user.side == 0;
    if (a->kind == ACT_SHIFT) return a->rotation == BROT_KEEP && battle_can_shift(b, a->user);
    if (a->kind == ACT_SWITCH)
        return a->rotation == BROT_KEEP && a->reserve < BATTLE_MAX_PER_SIDE &&
               (battle_switch_mask(b, a->user) & (1u << a->reserve));
    if ((a->kind != ACT_MOVE && a->kind != ACT_STRUGGLE) || !field_position(b, a->user) || a->move_slot >= 4)
        return false;
    Position candidate = a->user;
    if (rotation_battle(b)) {
        if (a->user.slot != BPOS_CENTER) return false;
        candidate.slot = battle_rotation_slot(a->rotation);
    } else if (a->rotation != BROT_KEEP) {
        return false;
    }
    if (a->kind == ACT_STRUGGLE) return battle_can_struggle(b, candidate);
    const uint8_t mask = battle_command_targets(b, candidate, a->move_slot);
    if (!mask) return false;
    if (mask & BATTLE_TARGET_AUTO) return true;
    return a->target.pos.side == 1 - a->user.side && a->target.pos.slot < BC_MAX_ACTIVE &&
           (mask & (1u << a->target.pos.slot));
}

uint8_t bc_first_target(uint8_t mask) {
    for (uint8_t slot = 0; slot < BC_MAX_ACTIVE; ++slot)
        if (mask & (1u << slot)) return slot;
    return 0;
}

void battle_choose_at(Battle *b, Position user, Action *out) {
    if (battle_forced_action(b, user, out)) return;
    Action choices[4 * BC_MAX_ACTIVE];
    unsigned n = 0;
    const unsigned rotations = rotation_battle(b) ? 3 : 1;
    for (uint8_t rotation = 0; rotation < rotations; ++rotation) {
        const Position candidate = {user.side, rotation_battle(b) ? battle_rotation_slot(rotation) : user.slot};
        if (battle_can_struggle(b, candidate)) {
            choices[n++] = (Action){.kind = ACT_STRUGGLE, .user = user, .rotation = rotation};
            continue;
        }
        for (unsigned m = 0; m < 4; ++m) {
            const uint8_t mask = battle_command_targets(b, candidate, m);
            if (!mask) continue;
            choices[n++] = (Action){.kind = ACT_MOVE, .user = user, .move_slot = m,
                .target = {{1 - user.side, bc_first_target(mask)}}, .rotation = rotation};
        }
    }
    *out = n ? choices[bc_next_random(b) % n] : (Action){.user = user};
    if (n && out->kind == ACT_MOVE && b->request->format != BATTLE_FORMAT_SINGLE) {
        const Position candidate = {user.side, rotation_battle(b) ? battle_rotation_slot(out->rotation) : user.slot};
        const uint8_t mask = battle_command_targets(b, candidate, out->move_slot) & 7u;
        uint8_t targets[BC_MAX_ACTIVE];
        unsigned count = 0;
        for (uint8_t slot = 0; slot < BC_MAX_ACTIVE; ++slot)
            if (mask & (1u << slot)) targets[count++] = slot;
        if (count > 1) out->target.pos.slot = targets[bc_next_random(b) % count];
    }
    if (!n && battle_can_shift(b, user)) out->kind = ACT_SHIFT;
}

void battle_choose_ai(Battle *b, uint8_t side, Action *out) {
    battle_choose_at(b, (Position){side, rotation_battle(b) ? BPOS_CENTER : 0}, out);
}

uint8_t bc_effectiveness(const Battle *b, uint8_t move_type, const Battler *d) {
    const uint8_t n = b->request->type_count;
    unsigned e = b->request->type_chart[move_type * n + d->types[0]];
    if (d->type_count == 2 && d->types[1] != d->types[0]) e = e * b->request->type_chart[move_type * n + d->types[1]] / 4;
    return (uint8_t)e;
}

uint16_t bc_calculate_damage(const Battle *b, const Battler *a, const BattleMove *m, const Battler *d,
                                 unsigned random_percent, bool typeless, uint8_t *eff) {
    *eff = typeless ? 4 : bc_effectiveness(b, m->type, d);
    if (*eff == 0) return 0;
    const bool physical = m->category == BCAT_PHYSICAL;
    unsigned atk = physical ? a->attack : a->sp_attack;
    unsigned def = physical ? d->defense : d->sp_defense;
    if (bc_conditions_enabled(b)) {
        atk = bc_stage_scale(atk, a->stat_stages[physical ? BSTAT_ATK : BSTAT_SPA], 2);
        def = bc_stage_scale(def, d->stat_stages[physical ? BSTAT_DEF : BSTAT_SPD], 2);
    }
    uint64_t dmg = ((2u * a->level / 5u + 2u) * (uint64_t)m->power * atk / (def ? def : 1)) / 50u + 2u;
    dmg = dmg * random_percent / 100u;
    if (!typeless && (m->type == a->types[0] || (a->type_count == 2 && m->type == a->types[1])))
        dmg = dmg * 3u / 2u;
    dmg = dmg * *eff / 4u;
    if ((physical && bc_status_kind(b, a) == BST_BURN) || (!physical && bc_status_kind(b, a) == BST_FROSTBITE))
        dmg /= 2;
    if (dmg == 0) dmg = 1;
    return dmg > 0xffff ? 0xffff : (uint16_t)dmg;
}

uint16_t battle_damage(const Battle *b, const Battler *a, const BattleMove *m, const Battler *d,
                       unsigned random_percent, uint8_t *eff) {
    return bc_calculate_damage(b, a, m, d, random_percent, false, eff);
}

void bc_emit(EventLog *log, BattleEvent ev) {
    if (log->count < BC_MAX_EVENTS) log->events[log->count++] = ev;
}

uint8_t battle_reserve_mask(const Battle *b, uint8_t side) {
    if (side >= 2 || !roster_battle(b) || b->outcome != OUTCOME_NONE) return 0;
    const Side *s = &b->sides[side];
    uint8_t mask = 0;
    for (unsigned i = 0; i < s->count; ++i)
        if (!s->battlers[i].fainted) mask |= 1u << i;
    for (unsigned pos = 0; pos < s->active_count; ++pos)
        if (s->active[pos] < s->count) mask &= ~(1u << s->active[pos]);
    return mask;
}

uint8_t battle_switch_mask(const Battle *b, Position p) {
    if (b->request->version < BATTLE_REQUEST_VERSION_SWITCH ||
        (rotation_battle(b) && p.slot != BPOS_CENTER)) return 0;
    const Battler *mon = battle_at(b, p);
    return mon && !mon->fainted ? battle_reserve_mask(b, p.side) : 0;
}

uint8_t battle_replacement_mask(const Battle *b, Position p) {
    if (!field_position(b, p) || battle_at(b, p)) return 0;
    return battle_reserve_mask(b, p.side);
}

bool battle_pending_replacement(const Battle *b, Position *position) {
    if (!roster_battle(b) || b->outcome != OUTCOME_NONE) return false;
    for (uint8_t side = 0; side < 2; ++side)
        for (uint8_t slot = 0; slot < b->sides[side].active_count; ++slot) {
            const Position p = {side, slot};
            if (!battle_replacement_mask(b, p)) continue;
            if (position) *position = p;
            return true;
        }
    return false;
}

static void send_out(Battle *b, Position p, uint8_t index, EventLog *log) {
    Side *side = &b->sides[p.side];
    const Battler *mon = &side->battlers[index];
    side->active[p.slot] = index;
    bc_emit(log, (BattleEvent){.kind = EV_SEND_OUT, .target = p, .battler = index,
                            .amount = mon->max_hp, .hp_after = mon->hp, .condition = bc_status_kind(b, mon)});
}

bool battle_replace(Battle *b, Position p, uint8_t index, EventLog *log) {
    if (index >= BATTLE_MAX_PER_SIDE || !(battle_replacement_mask(b, p) & (1u << index))) return false;
    log->count = 0;
    send_out(b, p, index, log);
    return true;
}

static void clear_fainted_positions(Battle *b) {
    if (!roster_battle(b)) return;
    for (uint8_t side = 0; side < 2; ++side)
        for (uint8_t slot = 0; slot < b->sides[side].active_count; ++slot) {
            const Battler *mon = battle_at(b, (Position){side, slot});
            if (mon && mon->fainted) b->sides[side].active[slot] = BATTLE_SLOT_NONE;
        }
}

static bool side_defeated(Battle *b, uint8_t side) {
    const Side *s = &b->sides[side];
    for (unsigned i = 0; i < s->count; ++i)
        if (!s->battlers[i].fainted) return false;
    return true;
}

static void finish(Battle *b, uint8_t outcome, EventLog *log) {
    b->outcome = outcome;
    bc_emit(log, (BattleEvent){.kind = EV_END, .outcome = outcome});
}

static Position locate(const Battle *b, uint8_t side, uint8_t battler) {
    const Side *s = &b->sides[side];
    for (uint8_t slot = 0; slot < s->active_count; ++slot)
        if (s->active[slot] == battler) return (Position){side, slot};
    return (Position){side, BATTLE_SLOT_NONE};
}

static void rotate(Battle *b, uint8_t side, uint8_t rotation, EventLog *log) {
    if (rotation == BROT_KEEP) return;
    uint8_t *slots = b->sides[side].active;
    uint8_t old[BC_MAX_ACTIVE];
    memcpy(old, slots, sizeof(old));
    for (uint8_t slot = 0; slot < BC_MAX_ACTIVE; ++slot) {
        slots[slot] = old[battle_rotation_origin(rotation, slot)];
        if (slots[slot] < BATTLE_MAX_PER_SIDE) bc_volatiles_on(&b->sides[side].battlers[slots[slot]], VOL_ROTATE);
    }
    bc_emit(log, (BattleEvent){.kind = EV_ROTATE, .user = {side, battle_rotation_slot(rotation)},
                            .target = {side, BPOS_CENTER}, .rotation = rotation});
}

static void shift(Battle *b, Position user, EventLog *log) {
    uint8_t *slots = b->sides[user.side].active;
    const uint8_t old = slots[user.slot];
    slots[user.slot] = slots[BPOS_CENTER];
    slots[BPOS_CENTER] = old;
    bc_emit(log, (BattleEvent){.kind = EV_SHIFT, .user = user, .target = {user.side, BPOS_CENTER}});
}

static void center_triple_finalists(Battle *b, EventLog *log) {
    if (!roster_battle(b) || b->request->format != BATTLE_FORMAT_TRIPLE) return;
    Position last[2];
    for (uint8_t side = 0; side < 2; ++side) {
        if (battle_reserve_mask(b, side)) return;
        Position active[BC_MAX_ACTIVE];
        if (battle_collect_actors(b, side, active) != 1) return;
        last[side] = active[0];
    }
    if (battle_adjacent(b, last[0], last[1])) return;
    shift(b, last[0], log);
    shift(b, last[1], log);
}

static unsigned action_rank(const Battle *b, ActionKind kind) {
    if (kind == ACT_SWITCH) return 2;
    if (kind == ACT_SHIFT && b->request->version < BATTLE_REQUEST_VERSION_PIVOT) return 1;
    return 0;
}

static bool acts_first(const Battle *b, const TurnAction *a, const TurnAction *d) {
    const unsigned ax = action_rank(b, a->action.kind), dx = action_rank(b, d->action.kind);
    if (ax != dx) return ax > dx;
    if (a->priority != d->priority) return a->priority > d->priority;
    if (a->speed != d->speed) return a->speed > d->speed;
    return a->tie > d->tie;
}

void bc_execute_switch(Battle *b, Position user, uint8_t index, EventKind reason, EventLog *log) {
    if (!(battle_switch_mask(b, user) & (1u << index))) return;
    Battler *outgoing = bc_mutable_at(b, user);
    memset(outgoing->stat_stages, 0, sizeof(outgoing->stat_stages));
    bc_effects_reset_volatiles(outgoing);
    const uint8_t condition = bc_status_kind(b, outgoing);
    if (condition == BST_TOXIC) outgoing->status = BATTLE_CONDITION(BST_TOXIC, 0);
    if (condition == BST_SLEEP && (b->request->conditions.flags & BCR_SLEEP_RESET_ON_SWITCH))
        outgoing->status = BATTLE_CONDITION(BST_SLEEP, outgoing->sleep_start);
    bc_emit(log, (BattleEvent){.kind = reason, .target = user,
                            .battler = b->sides[user.side].active[user.slot]});
    send_out(b, user, index, log);
}

static uint8_t random_reserve(Battle *b, uint8_t mask) {
    uint8_t candidates[BATTLE_MAX_PER_SIDE];
    unsigned count = 0;
    for (uint8_t i = 0; i < BATTLE_MAX_PER_SIDE; ++i)
        if (mask & (1u << i)) candidates[count++] = i;
    return candidates[count > 1 ? bc_next_random(b) % count : 0];
}

bool bc_drag_out(Battle *b, Position target, EventLog *log) {
    const uint8_t mask = battle_switch_mask(b, target);
    if (!mask) return false;
    bc_execute_switch(b, target, random_reserve(b, mask), EV_DRAG_OUT, log);
    return true;
}

bool bc_sure_hit(const Battle *b, Position user, const BattleMove *move) {
    if (!bc_conditions_enabled(b)) return false;
    const bool toxic = move->effect == BATTLE_STATUS_EFFECT(BST_TOXIC) ||
        (move->category == BCAT_STATUS && move->effect_count == 1 && move->effects[0].kind == BEFF_STATUS &&
         move->effects[0].arg0 == BST_TOXIC);
    return toxic && (b->request->conditions.flags & BCR_POISON_TOXIC_ALWAYS_HITS) &&
           bc_has_type(battle_at(b, user), b->request->conditions.poison_type);
}

bool bc_missed(Battle *b, Position user, Position target, const BattleMove *move) {
    if (!move->accuracy) return false;
    unsigned accuracy = move->accuracy;
    if (bc_conditions_enabled(b)) {
        const Battler *a = battle_at(b, user), *d = battle_at(b, target);
        if (bc_sure_hit(b, user, move)) return false;
        accuracy = bc_stage_scale(accuracy, a->stat_stages[BSTAT_ACC] - d->stat_stages[BSTAT_EVA], 3);
    }
    return bc_next_random(b) % 100u >= accuracy;
}

void bc_cure_status(Battler *mon, BattleEvent ev, EventLog *log) {
    ev.kind = EV_CURE;
    ev.condition = BATTLE_CONDITION_KIND(mon->status);
    mon->status = BST_NONE;
    mon->sleep_start = 0;
    bc_emit(log, ev);
}

// paralysis: false for the sleep/freeze check, true for the paralysis check.
static bool blocked_by_status(Battle *b, Position user, bool paralysis, EventLog *log) {
    Battler *mon = bc_mutable_at(b, user);
    const uint8_t kind = bc_status_kind(b, mon);
    if (paralysis != (kind == BST_PARALYSIS)) return false;
    const BattleConditionRules *rules = &b->request->conditions;
    const BattleEvent ev = {.kind = EV_STATUS_BLOCK, .user = user, .target = user, .condition = kind};
    if (kind == BST_SLEEP) {
        const unsigned remaining = BATTLE_CONDITION_COUNTER(mon->status) - 1;
        if (remaining) mon->status = BATTLE_CONDITION(kind, remaining);
        else {
            bc_cure_status(mon, ev, log);
            return false;
        }
    } else if (kind == BST_FREEZE) {
        if (bc_next_random(b) % 100u < rules->thaw_percent) {
            bc_cure_status(mon, ev, log);
            return false;
        }
    } else if (kind != BST_PARALYSIS || bc_next_random(b) % rules->paralysis_block_divisor) {
        return false;
    }
    bc_emit(log, ev);
    return true;
}

bool bc_immune_to_status(const BattleConditionRules *rules, const Battler *mon, uint8_t kind) {
    switch (kind) {
    case BST_POISON:
    case BST_TOXIC: return bc_has_type(mon, rules->poison_type) || bc_has_type(mon, rules->steel_type);
    case BST_BURN: return bc_has_type(mon, rules->fire_type);
    case BST_PARALYSIS: return (rules->flags & BCR_ELECTRIC_IMMUNE) && bc_has_type(mon, rules->electric_type);
    case BST_FREEZE:
    case BST_FROSTBITE: return bc_has_type(mon, rules->ice_type);
    default: return false;
    }
}

void bc_apply_damage(Battle *b, BattleEvent ev, uint16_t damage, EventLog *log) {
    Battler *mon = bc_mutable_at(b, ev.target);
    mon->hp = damage >= mon->hp ? 0 : (uint16_t)(mon->hp - damage);
    ev.amount = damage;
    ev.hp_after = mon->hp;
    bc_emit(log, ev);
    if (!mon->hp) {
        mon->fainted = true;
        memset(mon->stat_stages, 0, sizeof(mon->stat_stages));
        bc_volatiles_on(mon, VOL_FAINT);
        bc_emit(log, (BattleEvent){.kind = EV_FAINT, .target = ev.target});
    }
}

static bool hit(Battle *b, Position user, Position target, const BattleMove *m, uint16_t id,
                bool spread, bool typeless, EventLog *log) {
    BattleEvent ev = {.user = user, .target = target, .move_id = id};
    if (bc_missed(b, user, target, m)) {
        ev.kind = EV_MISS;
        bc_emit(log, ev);
        return false;
    }
    const Battler *defender = battle_at(b, target);
    uint16_t damage = bc_calculate_damage(b, battle_at(b, user), m, defender,
                                       85u + bc_next_random(b) % 16u, typeless, &ev.effectiveness);
    if (!ev.effectiveness) {
        ev.kind = EV_NO_EFFECT;
        bc_emit(log, ev);
        return false;
    }
    if (spread) {
        damage = damage * 3u / 4u;
        if (!damage) damage = 1;
    }
    ev.kind = EV_DAMAGE;
    bc_apply_damage(b, ev, damage, log);
    if (!typeless && !defender->fainted && bc_status_kind(b, defender) == BST_FREEZE &&
        (b->request->conditions.flags & BCR_THAW_ON_FIRE_HIT) && m->type == b->request->conditions.fire_type)
        bc_cure_status(bc_mutable_at(b, target), ev, log);
    return true;
}

static void execute_struggle(Battle *b, Position user, EventLog *log) {
    const BattleRequest *rules = b->request;
    const uint8_t foe = 1 - user.side;
    const uint8_t mask = battle_target_mask(b, user, &struggle_target, foe);
    uint8_t targets[BC_MAX_ACTIVE];
    unsigned count = 0;
    for (uint8_t slot = 0; slot < BC_MAX_ACTIVE; ++slot)
        if (mask & (1u << slot)) targets[count++] = slot;
    const Position target = {foe, count > 1 ? targets[bc_next_random(b) % count] : count ? targets[0] : 0};
    bc_emit(log, (BattleEvent){.kind = EV_MOVE_USED, .user = user, .target = target, .move_id = BATTLE_MOVE_STRUGGLE});
    if (!count) {
        bc_emit(log, (BattleEvent){.kind = EV_NO_EFFECT, .user = user, .target = target, .move_id = BATTLE_MOVE_STRUGGLE});
        return;
    }
    const BattleMove move = {.power = rules->struggle_power, .category = BCAT_PHYSICAL,
                             .accuracy = rules->struggle_accuracy, .target = BTARGET_SINGLE};
    if (!hit(b, user, target, &move, BATTLE_MOVE_STRUGGLE, false, true, log)) return;
    const unsigned max_hp = battle_at(b, user)->max_hp;
    const unsigned rounding = rules->struggle_recoil == BSTRUGGLE_MAX_HP_NEAREST ? 2 : 0;
    const uint16_t recoil = (max_hp + rounding) / 4u;
    bc_apply_damage(b, (BattleEvent){.kind = EV_RECOIL, .user = user, .target = user}, recoil ? recoil : 1, log);
}

BattleActionState battle_action_state(const Battle *b, Position p) {
    const Battler *mon = battle_at(b, p);
    if (!mon) return BAS_EMPTY;
    if (mon->fainted) return BAS_FAINTED;
    if (mon->vol.recharge) return BAS_RECHARGING;
    if (mon->vol.charge_slot != BATTLE_SLOT_NONE)
        return mon->vol.semi_invulnerable ? BAS_SEMI_INVULNERABLE : BAS_CHARGING;
    return BAS_READY;
}

bool battle_forced_action(const Battle *b, Position p, Action *out) {
    const BattleActionState state = battle_action_state(b, p);
    const Battler *mon = battle_at(b, p);
    if (state == BAS_RECHARGING) {
        *out = (Action){.kind = ACT_RECHARGE, .user = p};
        return true;
    }
    if (state == BAS_CHARGING || state == BAS_SEMI_INVULNERABLE) {
        *out = (Action){.kind = ACT_MOVE, .user = p, .move_slot = mon->vol.charge_slot,
                        .target = {mon->vol.charge_target}};
        return true;
    }
    return false;
}

uint8_t battle_volatile_flags(const Battle *b, Position p) {
    const Battler *mon = battle_at(b, p);
    if (!mon || mon->fainted) return 0;
    uint8_t flags = 0;
    if (mon->vol.confusion) flags |= BVF_CONFUSED;
    if (mon->vol.substitute_hp) flags |= BVF_SUBSTITUTE;
    if (mon->vol.charge_slot != BATTLE_SLOT_NONE) flags |= BVF_CHARGING;
    if (mon->vol.semi_invulnerable) flags |= BVF_SEMI_INVULNERABLE;
    if (mon->vol.recharge) flags |= BVF_RECHARGING;
    Action forced;
    if (battle_forced_action(b, p, &forced)) flags |= BVF_LOCKED;
    return flags;
}

bool battle_pending_pivot(const Battle *b, Position *position) {
    if (!b->resolution.waiting_for_pivot || b->outcome != OUTCOME_NONE) return false;
    if (position) *position = b->resolution.pivot;
    return true;
}

static void sort_actions(const Battle *b, TurnAction *order, unsigned first, unsigned count) {
    for (unsigned i = first + 1; i < count; ++i) {
        const TurnAction action = order[i];
        unsigned j = i;
        for (; j > first && acts_first(b, &action, &order[j - 1]); --j) order[j] = order[j - 1];
        order[j] = action;
    }
}

static void update_action_order(Battle *b) {
    if (!bc_conditions_enabled(b) || !(b->request->conditions.flags & BCR_DYNAMIC_SPEED)) return;
    TurnResolution *turn = &b->resolution;
    for (unsigned i = turn->next; i < turn->count; ++i) {
        TurnAction *action = &turn->actions[i];
        action->speed = speed(b, &b->sides[action->action.user.side].battlers[action->battler]);
    }
    sort_actions(b, turn->actions, turn->next, turn->count);
}

static unsigned residual_damage(Battle *b, Battler *mon, uint8_t kind) {
    unsigned divisor = b->request->conditions.burn_residual_divisor;
    unsigned multiplier = 1;
    if (kind == BST_POISON) divisor = 8;
    if (kind == BST_TOXIC) {
        divisor = 16;
        multiplier = BATTLE_CONDITION_COUNTER(mon->status);
        if (multiplier < 15) ++multiplier;
        mon->status = BATTLE_CONDITION(kind, multiplier);
    }
    const unsigned base = mon->max_hp / divisor;
    return (base ? base : 1) * multiplier;
}

static void resolve_end_of_turn(Battle *b, EventLog *log) {
    if (!bc_conditions_enabled(b)) return;
    Position order[BC_MAX_ACTIONS];
    unsigned count = 0;
    for (uint8_t side = 0; side < 2; ++side) {
        Position positions[BC_MAX_ACTIVE];
        const unsigned active = battle_collect_actors(b, side, positions);
        for (unsigned i = 0; i < active; ++i) {
            const Battler *mon = battle_at(b, positions[i]);
            if (!mon || mon->fainted) continue;
            unsigned j = count++;
            for (; j > 0 && speed(b, mon) > speed(b, battle_at(b, order[j - 1])); --j)
                order[j] = order[j - 1];
            order[j] = positions[i];
        }
    }

    // Each effect group processes eligible battlers in speed order. Replacement
    // happens after the whole phase, including simultaneous losses on both sides.
    static const uint8_t groups[] = {BST_POISON, BST_BURN, BST_FROSTBITE};
    for (unsigned g = 0; g < sizeof(groups); ++g)
        for (unsigned i = 0; i < count; ++i) {
            Battler *mon = bc_mutable_at(b, order[i]);
            const uint8_t kind = bc_status_kind(b, mon);
            if (mon->fainted || (kind != groups[g] && !(kind == BST_TOXIC && groups[g] == BST_POISON))) continue;
            bc_apply_damage(b, (BattleEvent){.kind = EV_RESIDUAL, .user = order[i], .target = order[i],
                                         .condition = kind}, residual_damage(b, mon, kind), log);
        }
}

static void continue_turn(Battle *b, EventLog *log) {
    TurnResolution *turn = &b->resolution;
    while (turn->next < turn->count) {
        update_action_order(b);
        const TurnAction *action = &turn->actions[turn->next++];
        const Position user = locate(b, action->action.user.side, action->battler);
        TurnAction *current = &turn->actions[turn->next - 1];
        const Battler *actor = battle_at(b, user);
        if (!actor || actor->fainted) {
            current->status = TA_SKIPPED;
            continue;
        }
        Battler *mon = bc_mutable_at(b, user);
        mon->vol.acted = true;
        if (action->action.kind == ACT_SHIFT) {
            if (battle_can_shift(b, user)) shift(b, user, log);
            current->status = TA_DONE;
            continue;
        }
        bool pivot = false;
        current->status = TA_INTERRUPTED;
        if (mon->vol.recharge) {
            bc_emit(log, (BattleEvent){.kind = EV_RECHARGE, .user = user, .target = user});
        } else if (blocked_by_status(b, user, false, log) ||
                   (mon->vol.flinched && (bc_emit(log, (BattleEvent){.kind = EV_FLINCH, .user = user, .target = user}), true)) ||
                   bc_confusion_check(b, user, log) || blocked_by_status(b, user, true, log)) {
            // A lost action breaks a Protect chain and cancels a charging move.
            mon->vol.protect_uses = 0;
            bc_cancel_charge(b, user, log);
        } else if (action->action.kind == ACT_STRUGGLE) {
            mon->vol.protect_uses = 0;
            execute_struggle(b, user, log);
            current->status = TA_DONE;
        } else {
            pivot = bc_execute_move(b, user, action->action.move_slot, action->action.target.pos, log);
            current->status = TA_DONE;
        }
        const bool player_lost = side_defeated(b, 0), foe_lost = side_defeated(b, 1);
        if (player_lost || foe_lost) {
            ++b->turn;
            clear_fainted_positions(b);
            finish(b, player_lost && foe_lost ? OUTCOME_DRAW : foe_lost ? OUTCOME_WIN : OUTCOME_LOSE, log);
            return;
        }
        const uint8_t reserves = pivot ? battle_switch_mask(b, user) : 0;
        if (!reserves) continue;
        if (user.side == 0) {
            turn->pivot = user;
            turn->waiting_for_pivot = true;
            return;
        }
        bc_execute_switch(b, user, random_reserve(b, reserves), EV_WITHDRAW, log);
    }
    resolve_end_of_turn(b, log);
    bc_effects_end_turn(b);
    ++b->turn;
    clear_fainted_positions(b);
    const bool player_lost = side_defeated(b, 0), foe_lost = side_defeated(b, 1);
    if (player_lost || foe_lost)
        finish(b, player_lost && foe_lost ? OUTCOME_DRAW : foe_lost ? OUTCOME_WIN : OUTCOME_LOSE, log);
    else
        center_triple_finalists(b, log);
}

bool battle_resume_pivot(Battle *b, uint8_t index, EventLog *log) {
    Position user;
    if (index >= BATTLE_MAX_PER_SIDE || !battle_pending_pivot(b, &user) ||
        !(battle_switch_mask(b, user) & (1u << index))) return false;
    log->count = 0;
    b->resolution.waiting_for_pivot = false;
    bc_execute_switch(b, user, index, EV_WITHDRAW, log);
    continue_turn(b, log);
    return true;
}

void battle_run_turn(Battle *b, const Action *in, unsigned count, EventLog *log) {
    TurnAction *order = b->resolution.actions;
    uint8_t seen[2] = {0};
    uint8_t reserved[2] = {0};
    unsigned n = 0;
    log->count = 0;
    if (b->outcome != OUTCOME_NONE) return;
    bc_effects_begin_turn(b);
    if ((battle_pending_pivot(b, NULL) || battle_pending_replacement(b, NULL)) &&
        !(count == 1 && in[0].kind == ACT_FORFEIT && battle_validate_action(b, in))) return;
    for (unsigned i = 0; i < count && n < BC_MAX_ACTIONS; ++i) {
        const Action *a = &in[i];
        if (a->kind == ACT_FORFEIT && battle_validate_action(b, a)) {
            b->resolution.waiting_for_pivot = false;
            bc_emit(log, (BattleEvent){.kind = EV_FORFEIT, .user = a->user});
            finish(b, OUTCOME_CANCELLED, log);
            return;
        }
        if (a->kind == ACT_NONE && field_position(b, a->user)) {
            bc_emit(log, (BattleEvent){.kind = EV_NO_MOVES, .user = a->user});
            finish(b, OUTCOME_UNSUPPORTED, log);
            return;
        }
        Action forced;
        if (battle_forced_action(b, a->user, &forced)) continue;  // a locked position acts below
        if (!battle_validate_action(b, a) || (seen[a->user.side] & (1u << a->user.slot))) continue;
        if (a->kind == ACT_SWITCH) {
            const uint8_t bit = 1u << a->reserve;
            if (reserved[a->user.side] & bit) continue;
            reserved[a->user.side] |= bit;
        }
        seen[a->user.side] |= 1u << a->user.slot;
        const uint8_t slot = rotation_battle(b) ? battle_rotation_slot(a->rotation) : a->user.slot;
        const uint8_t index = b->sides[a->user.side].active[slot];
        const Battler *x = &b->sides[a->user.side].battlers[index];
        order[n++] = (TurnAction){*a, index, a->kind == ACT_MOVE ? x->moves[a->move_slot].priority : 0,
                                 speed(b, x), bc_next_random(b), TA_PENDING};
    }
    // Locked positions (charging, recharging) act without a command. In
    // Rotation only the front acts, and a locked front cannot rotate.
    for (uint8_t side = 0; side < 2 && n < BC_MAX_ACTIONS; ++side) {
        Position actors[BC_MAX_ACTIVE];
        const unsigned count = battle_collect_actors(b, side, actors);
        for (unsigned i = 0; i < count && n < BC_MAX_ACTIONS; ++i) {
            Action forced;
            if ((seen[side] & (1u << actors[i].slot)) || !battle_forced_action(b, actors[i], &forced)) continue;
            seen[side] |= 1u << actors[i].slot;
            const uint8_t index = b->sides[side].active[actors[i].slot];
            const Battler *x = &b->sides[side].battlers[index];
            order[n++] = (TurnAction){forced, index, forced.kind == ACT_MOVE ? x->moves[forced.move_slot].priority : 0,
                                     speed(b, x), bc_next_random(b), TA_PENDING};
        }
    }
    sort_actions(b, order, 0, n);
    unsigned first = 0;
    while (first < n && order[first].action.kind == ACT_SWITCH) {
        const Action *action = &order[first++].action;
        bc_execute_switch(b, action->user, action->reserve, EV_WITHDRAW, log);
    }
    if (rotation_battle(b))
        for (unsigned i = first; i < n; ++i)
            rotate(b, order[i].action.user.side, order[i].action.rotation, log);
    b->resolution.next = first;
    b->resolution.count = n;
    continue_turn(b, log);
}
