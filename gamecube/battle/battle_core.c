#include "battle_core.h"

#include <string.h>

static uint32_t next_random(Battle *b) {
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
    case BI_FAINTED_LEAD: return "a lead Pokemon has no HP";
    }
    return "unknown";
}

bool battle_move_supported(const BattleRequest *r, const BattleMove *m) {
    if (m->unsupported || m->type >= r->type_count || m->accuracy > 100) return false;
    const uint8_t target = m->target & BTARGET_CLASS_MASK;
    if (target < BTARGET_SINGLE || target > BTARGET_SELF) return false;
    if (m->effect != BME_HIT) {
        if (r->version < BATTLE_REQUEST_VERSION_DRAG || r->battle_type != BTYPE_TRAINER || target != BTARGET_SINGLE ||
            m->effect > BME_FORCE_OUT_ON_HIT) return false;
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
    for (unsigned i = 0; i < 4; ++i) {
        out->moves[i] = e->mon.moves[i];
        out->move_ids[i] = r->moves[i];
        out->pp[i] = r->pp[i];
        out->max_pp[i] = r->max_pp[i];
    }
    out->fainted = r->hp == 0;
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

static Battler *mutable_at(Battle *b, Position p) {
    if (!field_position(b, p)) return NULL;
    Side *s = &b->sides[p.side];
    const uint8_t index = s->active[p.slot];
    return index < s->count ? &s->battlers[index] : NULL;
}

static bool rotation_battle(const Battle *b) {
    return b->request->format == BATTLE_FORMAT_ROTATION;
}

Battler *battle_active(Battle *b, uint8_t side) {
    return mutable_at(b, (Position){side, rotation_battle(b) ? BPOS_CENTER : 0});
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
        r->version > BATTLE_REQUEST_VERSION_DRAG)
        return BI_FORMAT;
    if (!r->type_count || r->type_count > BATTLE_MAX_TYPES) return BI_BAD_TYPES;
    if (r->version >= BATTLE_REQUEST_VERSION_STRUGGLE &&
        (!r->struggle_power || r->struggle_accuracy > 100 ||
         (r->struggle_recoil != BSTRUGGLE_MAX_HP_FLOOR && r->struggle_recoil != BSTRUGGLE_MAX_HP_NEAREST)))
        return BI_FORMAT;
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
            const Battler *x = &s->battlers[i];
            if (x->type_count < 1 || x->type_count > 2 || x->types[0] >= r->type_count ||
                (x->type_count == 2 && x->types[1] >= r->type_count)) return BI_BAD_TYPES;
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

static uint8_t first_target(uint8_t mask) {
    for (uint8_t slot = 0; slot < BC_MAX_ACTIVE; ++slot)
        if (mask & (1u << slot)) return slot;
    return 0;
}

void battle_choose_at(Battle *b, Position user, Action *out) {
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
                .target = {{1 - user.side, first_target(mask)}}, .rotation = rotation};
        }
    }
    *out = n ? choices[next_random(b) % n] : (Action){.user = user};
    if (n && out->kind == ACT_MOVE && b->request->format != BATTLE_FORMAT_SINGLE) {
        const Position candidate = {user.side, rotation_battle(b) ? battle_rotation_slot(out->rotation) : user.slot};
        const uint8_t mask = battle_command_targets(b, candidate, out->move_slot) & 7u;
        uint8_t targets[BC_MAX_ACTIVE];
        unsigned count = 0;
        for (uint8_t slot = 0; slot < BC_MAX_ACTIVE; ++slot)
            if (mask & (1u << slot)) targets[count++] = slot;
        if (count > 1) out->target.pos.slot = targets[next_random(b) % count];
    }
    if (!n && battle_can_shift(b, user)) out->kind = ACT_SHIFT;
}

void battle_choose_ai(Battle *b, uint8_t side, Action *out) {
    battle_choose_at(b, (Position){side, rotation_battle(b) ? BPOS_CENTER : 0}, out);
}

static uint8_t effectiveness(const Battle *b, uint8_t move_type, const Battler *d) {
    const uint8_t n = b->request->type_count;
    unsigned e = b->request->type_chart[move_type * n + d->types[0]];
    if (d->type_count == 2 && d->types[1] != d->types[0]) e = e * b->request->type_chart[move_type * n + d->types[1]] / 4;
    return (uint8_t)e;
}

static uint16_t calculate_damage(const Battle *b, const Battler *a, const BattleMove *m, const Battler *d,
                                 unsigned random_percent, bool typeless, uint8_t *eff) {
    *eff = typeless ? 4 : effectiveness(b, m->type, d);
    if (*eff == 0) return 0;
    const bool physical = m->category == BCAT_PHYSICAL;
    const uint32_t atk = physical ? a->attack : a->sp_attack;
    const uint32_t def = (physical ? d->defense : d->sp_defense) ? (physical ? d->defense : d->sp_defense) : 1;
    uint32_t dmg = ((2u * a->level / 5u + 2u) * m->power * atk / def) / 50u + 2u;
    dmg = dmg * random_percent / 100u;
    if (!typeless && (m->type == a->types[0] || (a->type_count == 2 && m->type == a->types[1])))
        dmg = dmg * 3u / 2u;
    dmg = dmg * *eff / 4u;
    if (dmg == 0) dmg = 1;
    return dmg > 0xffff ? 0xffff : (uint16_t)dmg;
}

uint16_t battle_damage(const Battle *b, const Battler *a, const BattleMove *m, const Battler *d,
                       unsigned random_percent, uint8_t *eff) {
    return calculate_damage(b, a, m, d, random_percent, false, eff);
}

static void emit(EventLog *log, BattleEvent ev) {
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
    emit(log, (BattleEvent){.kind = EV_SEND_OUT, .target = p, .battler = index,
                            .amount = mon->max_hp, .hp_after = mon->hp});
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
    emit(log, (BattleEvent){.kind = EV_END, .outcome = outcome});
}

typedef struct {
    Action action;
    uint8_t battler;
    int priority;
    uint16_t speed;
    uint32_t tie;
} TurnAction;

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
    for (uint8_t slot = 0; slot < BC_MAX_ACTIVE; ++slot)
        slots[slot] = old[battle_rotation_origin(rotation, slot)];
    emit(log, (BattleEvent){.kind = EV_ROTATE, .user = {side, battle_rotation_slot(rotation)},
                            .target = {side, BPOS_CENTER}, .rotation = rotation});
}

static void shift(Battle *b, Position user, EventLog *log) {
    uint8_t *slots = b->sides[user.side].active;
    const uint8_t old = slots[user.slot];
    slots[user.slot] = slots[BPOS_CENTER];
    slots[BPOS_CENTER] = old;
    emit(log, (BattleEvent){.kind = EV_SHIFT, .user = user, .target = {user.side, BPOS_CENTER}});
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

static unsigned action_rank(ActionKind kind) {
    if (kind == ACT_SWITCH) return 2;
    if (kind == ACT_SHIFT) return 1;
    return 0;
}

static bool acts_first(const TurnAction *a, const TurnAction *d) {
    const unsigned ax = action_rank(a->action.kind), dx = action_rank(d->action.kind);
    if (ax != dx) return ax > dx;
    if (a->priority != d->priority) return a->priority > d->priority;
    if (a->speed != d->speed) return a->speed > d->speed;
    return a->tie > d->tie;
}

static void execute_switch(Battle *b, Position user, uint8_t index, EventKind reason, EventLog *log) {
    if (!(battle_switch_mask(b, user) & (1u << index))) return;
    Battler *outgoing = mutable_at(b, user);
    memset(outgoing->stat_stages, 0, sizeof(outgoing->stat_stages));
    emit(log, (BattleEvent){.kind = reason, .target = user,
                            .battler = b->sides[user.side].active[user.slot]});
    send_out(b, user, index, log);
}

static bool drag_out(Battle *b, Position target, EventLog *log) {
    const uint8_t mask = battle_switch_mask(b, target);
    if (!mask) return false;
    uint8_t candidates[BATTLE_MAX_PER_SIDE];
    unsigned count = 0;
    for (uint8_t i = 0; i < BATTLE_MAX_PER_SIDE; ++i)
        if (mask & (1u << i)) candidates[count++] = i;
    const unsigned choice = count > 1 ? next_random(b) % count : 0;
    execute_switch(b, target, candidates[choice], EV_DRAG_OUT, log);
    return true;
}

static bool missed(Battle *b, const BattleMove *move) {
    return move->accuracy && next_random(b) % 100u >= move->accuracy;
}

static void apply_damage(Battle *b, BattleEvent ev, uint16_t damage, EventLog *log) {
    Battler *mon = mutable_at(b, ev.target);
    mon->hp = damage >= mon->hp ? 0 : (uint16_t)(mon->hp - damage);
    ev.amount = damage;
    ev.hp_after = mon->hp;
    emit(log, ev);
    if (!mon->hp) {
        mon->fainted = true;
        emit(log, (BattleEvent){.kind = EV_FAINT, .target = ev.target});
    }
}

static bool hit(Battle *b, Position user, Position target, const BattleMove *m, uint16_t id,
                bool spread, bool typeless, EventLog *log) {
    BattleEvent ev = {.user = user, .target = target, .move_id = id};
    if (missed(b, m)) {
        ev.kind = EV_MISS;
        emit(log, ev);
        return false;
    }
    const Battler *defender = battle_at(b, target);
    uint16_t damage = calculate_damage(b, battle_at(b, user), m, defender,
                                       85u + next_random(b) % 16u, typeless, &ev.effectiveness);
    if (!ev.effectiveness) {
        ev.kind = EV_NO_EFFECT;
        emit(log, ev);
        return false;
    }
    if (spread) {
        damage = damage * 3u / 4u;
        if (!damage) damage = 1;
    }
    ev.kind = EV_DAMAGE;
    apply_damage(b, ev, damage, log);
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
    const Position target = {foe, count > 1 ? targets[next_random(b) % count] : count ? targets[0] : 0};
    emit(log, (BattleEvent){.kind = EV_MOVE_USED, .user = user, .target = target, .move_id = BATTLE_MOVE_STRUGGLE});
    if (!count) {
        emit(log, (BattleEvent){.kind = EV_NO_EFFECT, .user = user, .target = target, .move_id = BATTLE_MOVE_STRUGGLE});
        return;
    }
    const BattleMove move = {.power = rules->struggle_power, .category = BCAT_PHYSICAL,
                             .accuracy = rules->struggle_accuracy, .target = BTARGET_SINGLE};
    if (!hit(b, user, target, &move, BATTLE_MOVE_STRUGGLE, false, true, log)) return;
    const unsigned max_hp = battle_at(b, user)->max_hp;
    const unsigned rounding = rules->struggle_recoil == BSTRUGGLE_MAX_HP_NEAREST ? 2 : 0;
    const uint16_t recoil = (max_hp + rounding) / 4u;
    apply_damage(b, (BattleEvent){.kind = EV_RECOIL, .user = user, .target = user}, recoil ? recoil : 1, log);
}

static void execute_move(Battle *b, const TurnAction *t, Position user, EventLog *log) {
    Battler *a = mutable_at(b, user);
    const unsigned move = t->action.move_slot;
    const BattleMove *m = &a->moves[move];
    const uint16_t id = a->move_ids[move];
    const uint8_t foe = 1 - user.side;
    uint8_t targets[2] = {battle_target_mask(b, user, m, 0), battle_target_mask(b, user, m, 1)};
    if ((m->target & BTARGET_CLASS_MASK) == BTARGET_SINGLE) {
        const uint8_t selected = 1u << t->action.target.pos.slot;
        if (targets[foe]) targets[foe] &= targets[foe] & selected ? selected : 1u << first_target(targets[foe]);
    }
    unsigned count = 0;
    Position first = {foe, t->action.target.pos.slot};
    for (uint8_t side = 0; side < 2; ++side)
        for (uint8_t slot = 0; slot < BC_MAX_ACTIVE; ++slot)
            if (targets[side] & (1u << slot))
                if (count++ == 0) first = (Position){side, slot};
    --a->pp[move];
    emit(log, (BattleEvent){.kind = EV_MOVE_USED, .user = user, .target = first, .move_id = id});
    if (!count) {
        emit(log, (BattleEvent){.kind = EV_NO_EFFECT, .user = user, .target = first, .move_id = id});
        return;
    }
    if (m->effect == BME_FORCE_OUT) {
        BattleEvent event = {.user = user, .target = first, .move_id = id};
        if (missed(b, m)) {
            event.kind = EV_MISS;
            emit(log, event);
        } else if (!drag_out(b, first, log)) {
            event.kind = EV_NO_EFFECT;
            emit(log, event);
        }
        return;
    }
    for (uint8_t side = 0; side < 2; ++side)
        for (uint8_t slot = 0; slot < BC_MAX_ACTIVE; ++slot) {
            if (!(targets[side] & (1u << slot))) continue;
            const Position target = {side, slot};
            const bool landed = hit(b, user, target, m, id, count > 1, false, log);
            if (landed && m->effect == BME_FORCE_OUT_ON_HIT) drag_out(b, target, log);
        }
}

void battle_run_turn(Battle *b, const Action *in, unsigned count, EventLog *log) {
    TurnAction order[BC_MAX_ACTIONS];
    uint8_t seen[2] = {0};
    uint8_t reserved[2] = {0};
    unsigned n = 0;
    log->count = 0;
    if (b->outcome != OUTCOME_NONE) return;
    if (battle_pending_replacement(b, NULL) &&
        !(count == 1 && in[0].kind == ACT_FORFEIT && battle_validate_action(b, in))) return;
    for (unsigned i = 0; i < count && n < BC_MAX_ACTIONS; ++i) {
        const Action *a = &in[i];
        if (a->kind == ACT_FORFEIT && battle_validate_action(b, a)) {
            emit(log, (BattleEvent){.kind = EV_FORFEIT, .user = a->user});
            finish(b, OUTCOME_CANCELLED, log);
            return;
        }
        if (a->kind == ACT_NONE && field_position(b, a->user)) {
            emit(log, (BattleEvent){.kind = EV_NO_MOVES, .user = a->user});
            finish(b, OUTCOME_UNSUPPORTED, log);
            return;
        }
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
                                 x->speed, next_random(b)};
    }
    for (unsigned i = 1; i < n; ++i) {
        const TurnAction a = order[i];
        unsigned j = i;
        for (; j > 0 && acts_first(&a, &order[j-1]); --j) order[j] = order[j-1];
        order[j] = a;
    }
    unsigned first = 0;
    while (first < n && order[first].action.kind == ACT_SWITCH) {
        const Action *action = &order[first++].action;
        execute_switch(b, action->user, action->reserve, EV_WITHDRAW, log);
    }
    if (rotation_battle(b))
        for (unsigned i = first; i < n; ++i)
            rotate(b, order[i].action.user.side, order[i].action.rotation, log);
    for (unsigned i = first; i < n; ++i) {
        const Position user = locate(b, order[i].action.user.side, order[i].battler);
        const Battler *actor = battle_at(b, user);
        if (!actor || actor->fainted) continue;
        if (order[i].action.kind == ACT_SHIFT) {
            if (battle_can_shift(b, user)) shift(b, user, log);
            continue;
        }
        if (order[i].action.kind == ACT_STRUGGLE)
            execute_struggle(b, user, log);
        else
            execute_move(b, &order[i], user, log);
        const bool player_lost = side_defeated(b, 0), foe_lost = side_defeated(b, 1);
        if (player_lost || foe_lost) {
            ++b->turn;
            clear_fainted_positions(b);
            finish(b, player_lost && foe_lost ? OUTCOME_DRAW : foe_lost ? OUTCOME_WIN : OUTCOME_LOSE, log);
            return;
        }
    }
    ++b->turn;
    clear_fainted_positions(b);
    center_triple_finalists(b, log);
}
