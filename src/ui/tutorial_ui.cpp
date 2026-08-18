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
}

void TutorialUiCompleteTypeA() {
    g_tutorial_step = TUTOR_DONE;
    g_type_a_tutorial_done = 1;
    g_tutorial_hint_alpha = 0.0f;
}

void TutorialUiUpdate(float dt, int current_room, int type_a_active, int type_a_blocked_this_frame) {
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

RectI TutorialUiWorldHintRect(RenderContext* render, const RoomDef* room) {
    if (room->type_a_count <= 0) {
        return { 0, 0, 0, 0 };
    }
    const RectF* wall = &room->type_a_walls[0];
    int x = WorldX(render, wall->x + wall->w * 0.5f) - 24;
    int y = WorldY(render, wall->y - 94.0f) - 10;
    RectI rect = { x, y, 52, 40 };
    return TutorialClampRect(rect);
}

RectI TutorialUiWorldHintDirtyRect(RenderContext* render, const RoomDef* room) {
    RectI rect = TutorialUiWorldHintRect(render, room);
    rect.x -= 12;
    rect.y -= 8;
    rect.w += 24;
    rect.h += 18;
    return TutorialClampRect(rect);
}

void TutorialUiDrawWorldHint(RenderContext* render, const RoomDef* room) {
    if (!TutorialUiWorldHintVisible()) {
        return;
    }
    RectI rect = TutorialUiWorldHintRect(render, room);
    float smooth = TutorialSmooth01(g_tutorial_hint_alpha);
    uint32_t frame = TutorialFadeColor(COL_TUTORIAL_CYAN, 0.74f * smooth);
    DrawRect(render, rect.x + 7, rect.y + 8, 2, 22, frame);
    DrawRect(render, rect.x + 7, rect.y + 8, 9, 2, frame);
    DrawRect(render, rect.x + 7, rect.y + 28, 9, 2, frame);
    DrawRect(render, rect.x + 40, rect.y + 8, 2, 22, frame);
    DrawRect(render, rect.x + 33, rect.y + 8, 9, 2, frame);
    DrawRect(render, rect.x + 33, rect.y + 28, 9, 2, frame);
    DrawTextUi(rect.x + 18,
               rect.y + 2,
               L"X",
               24,
               TutorialFadeColor(COL_TUTORIAL_PRIMARY, 0.92f * smooth),
               1);
}

SettingsUiTutorialState TutorialUiSettingsState() {
    SettingsUiTutorialState tutorial;
    tutorial.mark_system_category = SettingsUiIsOpen() &&
                                    g_tutorial_step == TUTOR_SETTINGS_CATEGORY &&
                                    SettingsUiCategoryFocusActive();
    tutorial.mark_type_a_setting = SettingsUiIsOpen() &&
                                   (g_tutorial_step == TUTOR_SETTINGS_ITEM ||
                                    (g_tutorial_step == TUTOR_SETTINGS_CATEGORY &&
                                     SettingsUiItemFocusActive() &&
                                     SettingsUiSelectedCategory() == SETTINGS_SYSTEM));
    tutorial.active = tutorial.mark_system_category ||
                      tutorial.mark_type_a_setting;
    tutorial.primary_color = COL_TUTORIAL_PRIMARY;
    tutorial.secondary_color = COL_TUTORIAL_CYAN;
    return tutorial;
}
