#include "scene.h"

#include "animation.h"
#include "assets.h"
#include "../companion/render.h"

static struct {
    const Battle *battle;
    const BattleLayout *layout;
    uint32_t build;
    BattleAnimation animation;
    bool show_target, choosing;
    Position target, command_user;
    unsigned turn;
} scene;

void battle_scene_reset(void) { scene.layout = NULL; scene.battle = NULL; }

bool battle_scene_begin(uint32_t build, const Battle *battle) {
    scene.layout = battle_layout_find(battle->request->format);
    scene.battle = battle;
    scene.build = build;
    scene.show_target = scene.choosing = false;
    scene.turn = 0;
    if (!scene.layout) return false;
    animation_init(&scene.animation, battle);
    animation_intro(&scene.animation, scene.layout);
    return true;
}

static const Battler *battler_at(Position p) {
    if (!battle_layout_anchor(scene.layout, p)) return NULL;
    const Side *side = &scene.battle->sides[p.side];
    const uint8_t index = scene.animation.actor[p.side][p.slot].battler;
    return index < side->count ? &side->battlers[index] : NULL;
}

void battle_scene_show_target(Position pos) {
    scene.show_target = battle_layout_anchor(scene.layout, pos) && battler_at(pos) && !battler_at(pos)->fainted;
    scene.target = pos;
}

void battle_scene_choose(unsigned turn, Position user) {
    scene.turn = turn;
    scene.choosing = true;
    scene.command_user = user;
    scene.animation.message = (SceneMessage){.kind = MSG_CHOOSE};
}

void battle_scene_choose_reserve(unsigned turn, Position user) {
    battle_scene_choose(turn, user);
    scene.animation.message.kind = MSG_RESERVE;
}

void battle_scene_play(const EventLog *log) {
    scene.show_target = scene.choosing = false;
    animation_play(&scene.animation, scene.layout, log);
}

bool battle_scene_tick(void) { return animation_tick(&scene.animation, scene.layout); }
unsigned battle_scene_steps(void) { return scene.animation.count; }

static void rect(ScreenRect r, uint32_t color) { render_rect(r.x, r.y, r.w, r.h, color); }
static void art(UiArt a, ScreenRect r, uint32_t color) {
    render_texture(ui_art(a), r.x, r.y, r.w, r.h, color);
}

static void panel(ScreenRect r, uint32_t border) {
    ui_panel(r.x, r.y, r.w, r.h, border, UI_CREAM);
}

static void draw_field(const BattleLayout *layout) {
    const ScreenRect f = layout->field;
    art(ART_MEADOW, f, 0xffffffffu);
    ui_panel(layout->field_center.x - 69, f.y + 11, 138, 26, UI_EDGE, UI_CREAM);
    render_text(layout->field_center.x - 52, f.y + 16, 1, UI_INK, "MEADOW ARENA");
}

static void draw_battler(Position pos, const BattlerAnchor *anchor, const ActorFrame *frame, const Battler *b) {
    art(ART_STAGE, anchor->platform, 0xffffffffu);
    if (!frame->visible || frame->blink || !b || (frame->volatiles & BVF_SEMI_INVULNERABLE)) return;
    CacheEntry *sprite = battle_sprite(scene.build, &b->entry->record, pos);
    if (!sprite || !sprite->frame_bottom) return;
    const float size = anchor->sprite_size * frame->scale;
    const float height = size * sprite->frame_bottom / 64.0f;
    const float feet = anchor->feet.y + frame->offset.y;
    const ScreenRect field = scene.layout->field;
    render_scissor(field.x, field.y, field.w, feet - field.y);
    render_texture_part(&sprite->texture, sprite->width, sprite->height, 0, 0, 64, sprite->frame_bottom,
                        anchor->feet.x + frame->offset.x - size / 2, feet - height + frame->drop, size, height, UI_WHITE);
    render_scissor_reset();
}

static void draw_label(CacheEntry *label, float x, float y, float max_w, float max_h) {
    if (!label || !label->width || !label->height) return;
    const float sx = max_w / label->width, sy = max_h / label->height;
    const float scale = sx < sy ? sx : sy;
    render_texture(&label->texture, x, y, label->width * scale, label->height * scale, UI_INK);
}

static const char *condition_label(uint8_t condition) {
    static const char *const labels[] = {"", "PSN", "TOX", "BRN", "PAR", "SLP", "FRZ", "FRB"};
    return condition < sizeof(labels) / sizeof(*labels) ? labels[condition] : "";
}

static void draw_hud(Position pos, const BattlerAnchor *a, const ActorFrame *f, const Battler *b) {
    if (!f->visible || !b) return;
    const bool targeted = scene.show_target && scene.target.side == pos.side && scene.target.slot == pos.slot;
    const bool selecting = scene.choosing && scene.command_user.side == pos.side && scene.command_user.slot == pos.slot;
    const bool compact = scene.layout->active_per_side > 1;
    const bool front = scene.layout->format == BATTLE_FORMAT_ROTATION && pos.slot == BPOS_CENTER;
    ui_panel(a->hud.x, a->hud.y, a->hud.w, a->hud.h,
             targeted ? UI_ROSE : selecting ? UI_EDGE : front ? UI_MINT : UI_SKY,
             selecting ? RGBA(223, 249, 226, 255) : UI_CREAM);
    CacheEntry *name = battle_label(scene.build, &b->entry->record);
    if (name) draw_label(name, a->name.x, a->name.y, compact ? 112 : 160, compact ? 16 : 22);
    else render_text(a->name.x, a->name.y, 1, UI_INK, "Species %u", b->entry->record.species);
    render_text(a->hud.x + a->hud.w - 67, a->name.y + 3, 1, UI_MUTED, "Lv.%u", b->level);
    art(ART_HP_FRAME, a->hp_bar, UI_EDGE);
    const unsigned hp = (unsigned)(f->hp + 0.5f);
    const float ratio = b->max_hp ? f->hp / b->max_hp : 0;
    const float fill = ratio > 1 ? 1 : ratio;
    if (fill > 0) {
        const float width = (a->hp_bar.w - 4) * fill;
        render_rect(a->hp_bar.x + 2, a->hp_bar.y + 2, width < 1 ? 1 : width, a->hp_bar.h - 4,
                    ratio > 0.5f ? UI_MINT : ratio > 0.2f ? UI_GOLD : RGBA(248, 112, 112, 255));
    }
    render_text(a->hp_text.x, a->hp_text.y, 1, UI_INK, "HP %u/%u", hp, b->max_hp);
    // Volatile badges (text only; no official art).
    const uint8_t v = f->volatiles;
    if (v & (BVF_SUBSTITUTE | BVF_CONFUSED | BVF_CHARGING))
        render_text(a->hud.x + a->hud.w - (compact ? 116 : 150), a->hp_text.y, 0.8f, UI_ROSE, "%s%s%s",
                    v & BVF_SUBSTITUTE ? "SUB " : "", v & BVF_CONFUSED ? "CNF " : "", v & BVF_CHARGING ? "CHG" : "");
    const bool legacy_status = scene.battle->request->version < BATTLE_REQUEST_VERSION_CONDITIONS && b->status;
    if (f->condition || legacy_status) {
        art(ART_STATUS, (ScreenRect){a->status.x, a->status.y, 12, 12}, UI_GOLD);
        render_text(a->status.x + 13, a->status.y - 2, 0.8f, UI_INK, "%s", legacy_status ? "STS" : condition_label(f->condition));
    }
    if (compact) {
        static const char *const positions[] = {"L", "C", "R"};
        const char *label = front ? "FRONT" : scene.layout->active_per_side == 2 && pos.slot == 1 ? "R" : positions[pos.slot];
        render_text(a->hud.x + a->hud.w - (front ? 48 : 20), a->hp_text.y, 0.85f, UI_INK, "%s", label);
    } else {
        art(ART_PARTY, (ScreenRect){a->party.x, a->party.y, 12, 12}, UI_MUTED);
        render_text(a->party.x + 15, a->party.y - 2, 0.8f, UI_MUTED, "P%u", b->party_slot + 1);
    }
}

static void draw_target(void) {
    if (!scene.show_target) return;
    const BattlerAnchor *a = battle_layout_anchor(scene.layout, scene.target);
    if (a) art(ART_CURSOR, (ScreenRect){a->cursor.x - 13, a->cursor.y, 26, 20}, 0xffffffffu);
}

static const char *message_text(const SceneMessage *msg) {
    switch (msg->kind) {
    case MSG_INTRO: return "The trainer wants to battle!";
    case MSG_ENTER: return msg->subject.side ? "The opponent sent out" : "Go!";
    case MSG_CHOOSE: return "Choose a move on the GBA.";
    case MSG_RESERVE: return "Choose a reserve on the GBA.";
    case MSG_MOVE: return "Move used";
    case MSG_MISS: return "The attack missed!";
    case MSG_IMMUNE: return "It had no effect.";
    case MSG_STRONG: return "It's super effective!";
    case MSG_WEAK: return "It's not very effective...";
    case MSG_FAINT: return "Fainted!";
    case MSG_CANCEL: return "The battle was cancelled.";
    case MSG_NO_MOVES: return "No supported move remains.";
    case MSG_DRAG_OUT: return "Forced out of battle!";
    case MSG_RECOIL: return "Recoil damage!";
    case MSG_WITHDRAW: return "Returned to the party.";
    case MSG_SHIFT: return "Shifted to the center!";
    case MSG_ROTATE: return "Rotated to the front!";
    case MSG_RESULT: return msg->outcome == OUTCOME_WIN ? "You won!" : msg->outcome == OUTCOME_LOSE ? "You lost..." : "Draw.";
    case MSG_STAGE: return "Stat stage changed.";
    case MSG_STATUS: return "Status applied.";
    case MSG_CURE: return "Status cleared.";
    case MSG_STATUS_BLOCK: return "Unable to move!";
    case MSG_RESIDUAL: return "Status damage!";
    case MSG_HEAL: return "HP was restored.";
    case MSG_DRAIN: return "Drained the target's energy!";
    case MSG_PROTECT: return "Protected itself!";
    case MSG_PROTECTED: return "The attack was blocked!";
    case MSG_FAIL: return "But it failed!";
    case MSG_MULTI_HIT: return "Hit multiple times!";
    case MSG_FLINCH: return "Flinched and couldn't move!";
    case MSG_CONFUSE: return "Became confused!";
    case MSG_CONFUSED: return "Is confused!";
    case MSG_CONFUSION_HIT: return "Hurt itself in its confusion!";
    case MSG_CONFUSION_END: return "Snapped out of confusion!";
    case MSG_SUBSTITUTE: return "Put in a substitute!";
    case MSG_SUBSTITUTE_HIT: return "The substitute took the hit!";
    case MSG_SUBSTITUTE_BREAK: return "The substitute faded!";
    case MSG_CHARGE: return msg->stage == BSEMI_AIR ? "Flew up high!" :
                            msg->stage == BSEMI_UNDERGROUND ? "Dug underground!" : "Is charging power!";
    case MSG_CHARGE_END: return "The charged move was lost.";
    case MSG_RECHARGE: return "Must recharge!";
    case MSG_NONE: return "";
    }
    return "";
}

static void draw_message(const BattleLayout *layout) {
    ui_panel(layout->command.x, layout->command.y, layout->command.w, layout->command.h, UI_EDGE, UI_MINT);
    if (scene.turn)
        render_text(layout->command.x + 12, layout->command.y + 5, 1, UI_INK, "TURN %u", scene.turn);
    render_text(layout->command.x + layout->command.w - 139, layout->command.y + 5, 1, UI_MUTED, "GBA CONTROLS");
    panel(layout->message, UI_SKY);
    const SceneMessage *msg = &scene.animation.message;
    const float x = layout->message.x + 14, y = layout->message.y + 8;
    render_text(x, y, 1.25f, UI_INK, "%s", message_text(msg));
    if (msg->kind == MSG_ENTER || msg->kind == MSG_MOVE || msg->kind == MSG_FAINT ||
        msg->kind == MSG_SHIFT || msg->kind == MSG_ROTATE || msg->kind == MSG_WITHDRAW || msg->kind == MSG_DRAG_OUT ||
        (msg->kind >= MSG_STAGE && msg->kind <= MSG_RECHARGE)) {
        const Battler *b = battler_at(msg->subject);
        CacheEntry *name = b ? battle_label(scene.build, &b->entry->record) : NULL;
        if (name) draw_label(name, x, y + 27, 190, 23);
        else render_text(x, y + 28, 1, UI_MUTED, "%s", msg->subject.side ? "Opponent" : "Player");
        if (msg->kind == MSG_STAGE && msg->stat < BSTAT_COUNT) {
            static const char *const stats[] = {"ATK", "DEF", "SP. ATK", "SP. DEF", "SPEED", "ACCURACY", "EVASION"};
            render_text(x + 210, y + 28, 1, UI_MUTED, "%s %s%d", stats[msg->stat], msg->stage > 0 ? "+" : "", msg->stage);
        }
        if (msg->kind == MSG_MULTI_HIT)
            render_text(x + 210, y + 28, 1, UI_MUTED, "%d hit%s", msg->stage, msg->stage == 1 ? "" : "s");
        if (msg->condition)
            render_text(x + 210, y + 28, 1, UI_MUTED, "%s", condition_label(msg->condition));
        if (msg->kind == MSG_MOVE) {
            CacheEntry *move = battle_move_label(scene.build, msg->move);
            if (msg->move == BATTLE_MOVE_STRUGGLE)
                render_text(x + 210, y + 28, 1, UI_MUTED, "Struggle");
            else if (move) draw_label(move, x + 210, y + 27, 220, 23);
            else render_text(x + 210, y + 28, 1, UI_MUTED, "Move #%u", msg->move);
        }
    }
}

void battle_scene_draw(void) {
    if (!scene.layout) return;
    const BattleLayout *layout = scene.layout;
    draw_field(layout);
    for (int side = 1; side >= 0; --side)
        for (unsigned slot = 0; slot < layout->active_per_side; ++slot) {
            const Position pos = {side, slot};
            draw_battler(pos, battle_layout_anchor(layout, pos), &scene.animation.actor[side][slot], battler_at(pos));
        }
    if (scene.animation.effect_visible) {
        const ScreenPoint p = scene.animation.effect;
        art(ART_EFFECT, (ScreenRect){p.x - 23, p.y - 23, 46, 46}, UI_GOLD);
    }
    for (unsigned side = 0; side < 2; ++side)
        for (unsigned slot = 0; slot < layout->active_per_side; ++slot) {
            const Position pos = {side, slot};
            draw_hud(pos, battle_layout_anchor(layout, pos), &scene.animation.actor[side][slot], battler_at(pos));
        }
    draw_target();
    draw_message(layout);
    if (scene.animation.fade > 0)
        rect(layout->viewport, RGBA(0, 0, 0, (unsigned)(scene.animation.fade * 255)));
}
