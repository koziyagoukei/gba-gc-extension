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
    case BI_FORMAT: return "only SINGLE battles are implemented";
    case BI_PARTY_SIZE: return "only one Pokemon per side is implemented";
    case BI_NO_PLAYER_MOVES: return "player has no supported move";
    case BI_NO_OPPONENT_MOVES: return "opponent has no supported move";
    case BI_BAD_TYPES: return "type data out of range";
    case BI_FAINTED_LEAD: return "a lead Pokemon has no HP";
    }
    return "unknown";
}

bool battle_move_supported(const BattleRequest *r, const BattleMove *m) {
    return m->unsupported == 0 && (m->category == BCAT_PHYSICAL || m->category == BCAT_SPECIAL) &&
           m->power > 0 && m->priority == 0 && m->target == BTARGET_SINGLE && m->type < r->type_count &&
           m->accuracy <= 100;
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

Battler *battle_active(Battle *b, uint8_t side) {
    Side *s = &b->sides[side];
    return &s->battlers[s->active[0]];
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
    if (r->format != BATTLE_FORMAT_SINGLE || r->active_per_side != 1) return BI_FORMAT;
    if (r->player_count != 1 || r->opponent_count != 1) return BI_PARTY_SIZE;
    for (unsigned side = 0; side < 2; ++side) {
        Side *s = &b->sides[side];
        const BattleEntry *entries = side ? r->opponent : r->player;
        s->count = side ? r->opponent_count : r->player_count;
        for (unsigned i = 0; i < s->count; ++i) {
            load_battler(&s->battlers[i], &entries[i]);
            const Battler *x = &s->battlers[i];
            if (x->types[0] >= r->type_count || (x->type_count == 2 && x->types[1] >= r->type_count)) return BI_BAD_TYPES;
        }
        s->active_count = 1;
        s->active[0] = 0;
        if (s->battlers[0].fainted) return BI_FAINTED_LEAD;
        if (!battle_usable_moves(b, &s->battlers[0])) return side ? BI_NO_OPPONENT_MOVES : BI_NO_PLAYER_MOVES;
    }
    return BI_OK;
}

bool battle_validate_action(const Battle *b, const Action *a) {
    if (a->kind == ACT_FORFEIT) return a->user.side == 0;
    if (a->kind != ACT_MOVE || a->user.side > 1 || a->user.slot != 0 || a->move_slot > 3) return false;
    const Side *s = &b->sides[a->user.side];
    const Battler *x = &s->battlers[s->active[0]];
    return !x->fainted && (battle_usable_moves(b, x) & (1u << a->move_slot)) &&
           a->target.pos.side == (uint8_t)(1 - a->user.side) && a->target.pos.slot == 0;
}

void battle_choose_ai(Battle *b, uint8_t side, Action *out) {
    const Battler *x = battle_active(b, side);
    const uint8_t mask = battle_usable_moves(b, x);
    unsigned choices[4], n = 0;
    for (unsigned i = 0; i < 4; ++i)
        if (mask & (1u << i)) choices[n++] = i;
    memset(out, 0, sizeof(*out));
    out->user = (Position){side, 0};
    out->target.pos = (Position){(uint8_t)(1 - side), 0};
    if (!n) { out->kind = ACT_NONE; return; }
    out->kind = ACT_MOVE;
    out->move_slot = (uint8_t)choices[next_random(b) % n];
}

static uint8_t effectiveness(const Battle *b, uint8_t move_type, const Battler *d) {
    const uint8_t n = b->request->type_count;
    unsigned e = b->request->type_chart[move_type * n + d->types[0]];
    if (d->type_count == 2 && d->types[1] != d->types[0]) e = e * b->request->type_chart[move_type * n + d->types[1]] / 4;
    return (uint8_t)e;
}

uint16_t battle_damage(const Battle *b, const Battler *a, const BattleMove *m, const Battler *d,
                       unsigned random_percent, uint8_t *eff) {
    *eff = effectiveness(b, m->type, d);
    if (*eff == 0) return 0;
    const bool physical = m->category == BCAT_PHYSICAL;
    const uint32_t atk = physical ? a->attack : a->sp_attack;
    const uint32_t def = (physical ? d->defense : d->sp_defense) ? (physical ? d->defense : d->sp_defense) : 1;
    uint32_t dmg = ((2u * a->level / 5u + 2u) * m->power * atk / def) / 50u + 2u;
    dmg = dmg * random_percent / 100u;
    if (m->type == a->types[0] || (a->type_count == 2 && m->type == a->types[1])) dmg = dmg * 3u / 2u;
    dmg = dmg * *eff / 4u;
    if (dmg == 0) dmg = 1;
    return dmg > 0xffff ? 0xffff : (uint16_t)dmg;
}

static void emit(EventLog *log, BattleEvent ev) {
    if (log->count < BC_MAX_EVENTS) log->events[log->count++] = ev;
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

void battle_run_turn(Battle *b, const Action *in, unsigned count, EventLog *log) {
    Action order[2 * BC_MAX_ACTIVE];
    uint32_t tiebreak[2 * BC_MAX_ACTIVE];
    unsigned n = 0;
    log->count = 0;
    if (b->outcome != OUTCOME_NONE) return;
    for (unsigned i = 0; i < count && n < 2 * BC_MAX_ACTIVE; ++i) {
        if (in[i].kind == ACT_FORFEIT && battle_validate_action(b, &in[i])) {
            emit(log, (BattleEvent){.kind = EV_FORFEIT, .user = in[i].user});
            finish(b, OUTCOME_CANCELLED, log);
            return;
        }
        if (in[i].kind == ACT_NONE) {
            // A side without any usable move cannot continue; Struggle is not implemented.
            emit(log, (BattleEvent){.kind = EV_NO_MOVES, .user = in[i].user});
            finish(b, OUTCOME_UNSUPPORTED, log);
            return;
        }
        if (!battle_validate_action(b, &in[i])) continue;
        order[n] = in[i];
        tiebreak[n++] = next_random(b);
    }
    // Priority, then speed, then a random tiebreak (all supported moves have priority 0).
    for (unsigned i = 1; i < n; ++i)
        for (unsigned j = i; j > 0; --j) {
            const Battler *x = battle_active(b, order[j].user.side), *y = battle_active(b, order[j-1].user.side);
            const int px = battle_active(b, order[j].user.side)->moves[order[j].move_slot].priority;
            const int py = battle_active(b, order[j-1].user.side)->moves[order[j-1].move_slot].priority;
            const bool first = px != py ? px > py : x->speed != y->speed ? x->speed > y->speed : tiebreak[j] > tiebreak[j-1];
            if (!first) break;
            Action t = order[j]; order[j] = order[j-1]; order[j-1] = t;
            uint32_t r = tiebreak[j]; tiebreak[j] = tiebreak[j-1]; tiebreak[j-1] = r;
        }
    for (unsigned i = 0; i < n; ++i) {
        Battler *a = battle_active(b, order[i].user.side);
        Battler *d = battle_active(b, order[i].target.pos.side);
        if (a->fainted) continue;
        const BattleMove *m = &a->moves[order[i].move_slot];
        const uint16_t move_id = a->move_ids[order[i].move_slot];
        a->pp[order[i].move_slot]--;
        emit(log, (BattleEvent){.kind = EV_MOVE_USED, .user = order[i].user, .target = order[i].target.pos, .move_id = move_id});
        if (m->accuracy && next_random(b) % 100u >= m->accuracy) {
            emit(log, (BattleEvent){.kind = EV_MISS, .user = order[i].user, .target = order[i].target.pos, .move_id = move_id});
            continue;
        }
        uint8_t eff;
        const uint16_t dmg = battle_damage(b, a, m, d, 85u + next_random(b) % 16u, &eff);
        if (!eff) {
            emit(log, (BattleEvent){.kind = EV_NO_EFFECT, .user = order[i].user, .target = order[i].target.pos, .move_id = move_id});
            continue;
        }
        d->hp = dmg >= d->hp ? 0 : (uint16_t)(d->hp - dmg);
        emit(log, (BattleEvent){.kind = EV_DAMAGE, .user = order[i].user, .target = order[i].target.pos, .move_id = move_id,
                                .amount = dmg, .hp_after = d->hp, .effectiveness = eff});
        if (d->hp == 0) {
            d->fainted = true;
            emit(log, (BattleEvent){.kind = EV_FAINT, .target = order[i].target.pos});
            if (side_defeated(b, order[i].target.pos.side)) {
                b->turn++;
                finish(b, order[i].target.pos.side == 1 ? OUTCOME_WIN : OUTCOME_LOSE, log);
                return;
            }
        }
    }
    b->turn++;
}
