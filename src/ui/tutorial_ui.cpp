#include "tutorial_ui.h"

#include <stdint.h>

#include "game_config.h"
#include "ui_text.h"

enum TutorialStep {
    TUTOR_NONE,
    TUTOR_ROOM01_X,
    TUTOR_SETTINGS_CATEGORY,
    TUTOR_SETTINGS_ITEM,
    TUTOR_DONE
};

static const uint32_t COL_TUTORIAL_PRIMARY = 0x00f7f0e5;
static const uint32_t COL_TUTORIAL_CYAN = 0x0035cfc3;

static int g_type_a_tutorial_done = 0;
static TutorialStep g_tutorial_step = TUTOR_NONE;
static float g_tutorial_hint_alpha = 0.0f;
static float g_tutorial_hint_pulse_seconds = 0.0f;

static float TutorialClampF(float value, float lo, float hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static float TutorialAbsF(float value) {
    return value < 0.0f ? -value : value;
}

static float TutorialApproachF(float value, float target, float step) {
    if (value < target) {
        value += step;
        if (value > target) value = target;
    } else if (value > target) {
        value -= step;
        if (value < target) value = target;
    }
    return value;
}

static float TutorialSmooth01(float value) {
    value = TutorialClampF(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}

static float TutorialPulse01(float seconds) {
    const float cycle_seconds = 0.86f;
    float phase = seconds / cycle_seconds;
    phase -= (float)((int)phase);
    float triangle = phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;
    return TutorialSmooth01(triangle);
}

static uint32_t TutorialFadeColor(uint32_t color, float fade) {
    fade = TutorialClampF(fade, 0.0f, 1.0f);
    int r = (int)((float)((color >> 16) & 255) * fade);
    int g = (int)((float)((color >> 8) & 255) * fade);
    int b = (int)((float)(color & 255) * fade);
    return (uint32_t)((r << 16) | (g << 8) | b);
}

static RectI TutorialClampRect(RectI rect) {
    if (rect.x < 0) {
        rect.w += rect.x;
        rect.x = 0;
    }
    if (rect.y < 0) {
        rect.h += rect.y;
        rect.y = 0;
    }
    if (rect.x + rect.w > FB_W) {
        rect.w = FB_W - rect.x;
    }
    if (rect.y + rect.h > FB_H) {
        rect.h = FB_H - rect.y;
    }
    if (rect.w < 0) rect.w = 0;
    if (rect.h < 0) rect.h = 0;
    return rect;
}

static int TutorialWorldHintTargetVisible(int current_room) {
    return g_tutorial_step == TUTOR_ROOM01_X && !SettingsUiIsOpen() && current_room == 1;
}

void TutorialUiResetStageState() {
    g_tutorial_step = TUTOR_NONE;
    g_tutorial_hint_alpha = 0.0f;
    g_tutorial_hint_pulse_seconds = 0.0f;
}

void TutorialUiCompleteTypeA() {
    g_tutorial_step = TUTOR_DONE;
    g_type_a_tutorial_done = 1;
    g_tutorial_hint_alpha = 0.0f;
    g_tutorial_hint_pulse_seconds = 0.0f;
}

void TutorialUiUpdate(float dt, int current_room, int type_a_active, int type_a_blocked_this_frame) {
    if (!type_a_active && current_room == 1) {
        TutorialUiCompleteTypeA();
    }

    if (g_type_a_tutorial_done) {
        g_tutorial_step = TUTOR_DONE;
    } else if (current_room == 1 && type_a_active && !SettingsUiIsOpen()) {
        if (type_a_blocked_this_frame && g_tutorial_step == TUTOR_NONE) {
            g_tutorial_step = TUTOR_ROOM01_X;
        }
    } else if (SettingsUiIsOpen()) {
        if (g_tutorial_step == TUTOR_ROOM01_X) {
            g_tutorial_step = TUTOR_SETTINGS_CATEGORY;
            SettingsUiMarkDirty();
        }
        if (g_tutorial_step == TUTOR_SETTINGS_CATEGORY &&
            SettingsUiSelectedCategory() == SETTINGS_SYSTEM &&
            SettingsUiItemFocusActive()) {
            g_tutorial_step = TUTOR_SETTINGS_ITEM;
            SettingsUiMarkDirty();
        }
    }

    int hint_visible = TutorialWorldHintTargetVisible(current_room);
    if (hint_visible) {
        g_tutorial_hint_pulse_seconds += dt;
        if (g_tutorial_hint_pulse_seconds > 60.0f) {
            g_tutorial_hint_pulse_seconds -= 60.0f;
        }
    } else if (g_tutorial_hint_alpha <= 0.001f) {
        g_tutorial_hint_pulse_seconds = 0.0f;
    }
    float old_alpha = g_tutorial_hint_alpha;
    g_tutorial_hint_alpha = TutorialApproachF(g_tutorial_hint_alpha, hint_visible ? 1.0f : 0.0f, dt * 4.0f);
    if (old_alpha != g_tutorial_hint_alpha) {
        SettingsUiMarkDirty();
    }
}

int TutorialUiFadeActive(int current_room) {
    float target = TutorialWorldHintTargetVisible(current_room) ? 1.0f : 0.0f;
    return TutorialAbsF(g_tutorial_hint_alpha - target) > 0.001f;
}

int TutorialUiWorldHintVisible() {
    return g_tutorial_hint_alpha > 0.001f;
}

RectI TutorialUiWorldHintRect(RenderContext* render, const RoomDef* room, const Player* player) {
    const int hint_w = 68;
    const int hint_h = 54;
    int center_x = FB_W / 2;
    int top_y = FB_H / 2 - 110;

    if (player) {
        RectF player_rect = PlayerCollisionRect(player);
        center_x = WorldX(render, player_rect.x + player_rect.w * 0.5f);
        top_y = WorldY(render, player_rect.y) - 60;
    } else if (room->type_a_count > 0) {
        const RectF* wall = &room->type_a_walls[0];
        center_x = WorldX(render, wall->x - 48.0f);
        top_y = WorldY(render, wall->y + wall->h * 0.5f) - 70;
    }

    RectI rect = { center_x - hint_w / 2, top_y, hint_w, hint_h };
    return TutorialClampRect(rect);
}

RectI TutorialUiWorldHintDirtyRect(RenderContext* render, const RoomDef* room, const Player* player) {
    RectI rect = TutorialUiWorldHintRect(render, room, player);
    rect.x -= 20;
    rect.y -= 18;
    rect.w += 40;
    rect.h += 36;
    return TutorialClampRect(rect);
}

void TutorialUiDrawWorldHint(RenderContext* render, const RoomDef* room, const Player* player) {
    if (!TutorialUiWorldHintVisible()) {
        return;
    }
    RectI rect = TutorialUiWorldHintRect(render, room, player);
    float smooth = TutorialSmooth01(g_tutorial_hint_alpha);
    float pulse = TutorialPulse01(g_tutorial_hint_pulse_seconds);
    int pulse_px = (int)(pulse * 4.0f + 0.5f);
    int x = rect.x - pulse_px / 2;
    int y = rect.y - pulse_px / 2;
    int w = rect.w + pulse_px;
    float pulse_alpha = (0.58f + 0.25f * pulse) * smooth;
    uint32_t frame = TutorialFadeColor(COL_TUTORIAL_CYAN, pulse_alpha);
    DrawRect(render, x + 6, y + 6, 3, 32, frame);
    DrawRect(render, x + 6, y + 6, 15, 3, frame);
    DrawRect(render, x + 6, y + 35, 15, 3, frame);
    DrawRect(render, x + w - 9, y + 6, 3, 32, frame);
    DrawRect(render, x + w - 21, y + 6, 15, 3, frame);
    DrawRect(render, x + w - 21, y + 35, 15, 3, frame);
    uint32_t x_color = TutorialFadeColor(COL_TUTORIAL_CYAN, pulse_alpha);
    int cx = x + w / 2;
    int cy = y + 22;
    int x_half_w = 7 + pulse_px;
    int x_half_h = 9 + pulse_px;
    DrawThickLine(render, cx - x_half_w, cy - x_half_h, cx + x_half_w, cy + x_half_h, 3, x_color);
    DrawThickLine(render, cx + x_half_w, cy - x_half_h, cx - x_half_w, cy + x_half_h, 3, x_color);
}

SettingsUiTutorialState TutorialUiSettingsState() {
    SettingsUiTutorialState tutorial;
    tutorial.mark_system_category = SettingsUiIsOpen() &&
                                    (g_tutorial_step == TUTOR_SETTINGS_CATEGORY ||
                                     g_tutorial_step == TUTOR_SETTINGS_ITEM);
    tutorial.mark_type_a_setting = SettingsUiIsOpen() &&
                                   SettingsUiSelectedCategory() == SETTINGS_SYSTEM &&
                                   (g_tutorial_step == TUTOR_SETTINGS_ITEM ||
                                    g_tutorial_step == TUTOR_SETTINGS_CATEGORY);
    tutorial.active = tutorial.mark_system_category ||
                      tutorial.mark_type_a_setting;
    tutorial.primary_color = COL_TUTORIAL_PRIMARY;
    tutorial.secondary_color = COL_TUTORIAL_CYAN;
    return tutorial;
}
