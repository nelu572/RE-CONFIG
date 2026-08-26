#include "settings_ui.h"

#include "game_config.h"
#include "input.h"
#include "modal_ui.h"
#include "ui_text.h"

#ifndef RENDER_SCALE
#define RENDER_SCALE 2
#endif
#define RENDER_W (FB_W * RENDER_SCALE)
#define RENDER_H (FB_H * RENDER_SCALE)

enum SettingsFocus {
    SETTINGS_FOCUS_CATEGORY,
    SETTINGS_FOCUS_ITEM
};

static const int SETTINGS_UI_MAX_ITEMS = 16;
static RenderContext* g_settings_render;
static SettingsUiFeatureActiveCallback g_feature_active;
static SettingsUiToggleFeatureCallback g_toggle_feature;
static SettingsUiSetItemValueCallback g_set_item_value;
static SettingsUiDrawContextTextCallback g_draw_context_text;
static int g_settings_open = 0;
static SettingsFocus g_settings_focus = SETTINGS_FOCUS_CATEGORY;
static int g_settings_category_selection = SETTINGS_GAMEPLAY;
static int g_settings_item_selection = 0;
static float g_settings_fade = 0.0f;
static float g_settings_motion = 0.0f;
static float g_settings_category_alpha[SETTINGS_CATEGORY_COUNT];
static float g_settings_item_alpha[SETTINGS_UI_MAX_ITEMS];
static int g_settings_value_index[SETTINGS_UI_MAX_ITEMS];
static float g_settings_content_motion = 1.0f;
static float g_settings_content_dir = 1.0f;
static float g_feature_toggle_motion[FEATURE_COUNT];
static int g_settings_full_dirty = 0;
static int g_settings_dirty = 1;
static int g_settings_cache_valid = 0;
static int g_settings_cache_building = 0;
static int g_settings_tutorial_target_active = 0;
static float g_settings_tutorial_pulse_seconds = 0.0f;
static uint32_t g_settings_cache_supersample_pixels[RENDER_W * RENDER_H];
static unsigned char g_settings_cache_alpha[RENDER_W * RENDER_H];
static const uint32_t COL_TUTORIAL_TARGET = MODAL_UI_CYAN;
static const uint32_t COL_SETTINGS_TEXT = MODAL_UI_TEXT;
static const uint32_t COL_SETTINGS_TEXT_DIM = MODAL_UI_TEXT_DIM;
static const uint32_t COL_SETTINGS_TEXT_DISABLED = 0x00958b86;
static const uint32_t COL_SETTINGS_CRIMSON_BRIGHT = MODAL_UI_CRIMSON;
static const uint32_t COL_SETTINGS_CRIMSON_DARK = MODAL_UI_PANEL_BORDER;
static const uint32_t COL_SETTINGS_CRIMSON_DEEP = MODAL_UI_PANEL_LINE;
static const uint32_t COL_SETTINGS_CRIMSON_ARROW = MODAL_UI_CRIMSON_ARROW;

static float SettingsClampF(float value, float lo, float hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static float SettingsAbsF(float v) {
    return v < 0.0f ? -v : v;
}

static float SettingsSmooth01(float value) {
    value = SettingsClampF(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}

static float SettingsPulse01(float seconds) {
    const float cycle_seconds = 0.86f;
    float phase = seconds / cycle_seconds;
    phase -= (float)((int)phase);
    float triangle = phase < 0.5f ? phase * 2.0f : (1.0f - phase) * 2.0f;
    return SettingsSmooth01(triangle);
}

static float SettingsApproachF(float value, float target, float step) {
    if (value < target) {
        value += step;
        if (value > target) value = target;
    } else if (value > target) {
        value -= step;
        if (value < target) value = target;
    }
    return value;
}

static float SettingsEaseOutFollowF(float value, float target, float dt, float speed) {
    float delta = target - value;
    if (SettingsAbsF(delta) < 0.001f) {
        return target;
    }
    float t = SettingsClampF(dt * speed, 0.0f, 1.0f);
    value += delta * t;
    if (SettingsAbsF(target - value) < 0.001f) {
        value = target;
    }
    return value;
}

static uint32_t SettingsFadeColor(uint32_t color, float fade) {
    fade = SettingsClampF(fade, 0.0f, 1.0f);
    int r = (int)((float)((color >> 16) & 255) * fade);
    int g = (int)((float)((color >> 8) & 255) * fade);
    int b = (int)((float)(color & 255) * fade);
    return (uint32_t)((r << 16) | (g << 8) | b);
}

static uint32_t SettingsLerpColor(uint32_t a, uint32_t b, float t) {
    t = SettingsClampF(t, 0.0f, 1.0f);
    int ar = (int)((a >> 16) & 255);
    int ag = (int)((a >> 8) & 255);
    int ab = (int)(a & 255);
    int br = (int)((b >> 16) & 255);
    int bg = (int)((b >> 8) & 255);
    int bb = (int)(b & 255);
    int r = ar + (int)((float)(br - ar) * t + 0.5f);
    int g = ag + (int)((float)(bg - ag) * t + 0.5f);
    int bl = ab + (int)((float)(bb - ab) * t + 0.5f);
    return (uint32_t)((r << 16) | (g << 8) | bl);
}

static RectI SettingsClampRect(RectI rect) {
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

static void DrawRoundRect(int x, int y, int w, int h, int radius, uint32_t color) {
    if (radius < 1) {
        DrawRect(g_settings_render, x, y, w, h, color);
        return;
    }
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;
    DrawRect(g_settings_render, x + radius, y, w - radius * 2, h, color);
    DrawRect(g_settings_render, x, y + radius, w, h - radius * 2, color);
    FillCircle(g_settings_render, x + radius, y + radius, radius, color);
    FillCircle(g_settings_render, x + w - radius - 1, y + radius, radius, color);
    FillCircle(g_settings_render, x + radius, y + h - radius - 1, radius, color);
    FillCircle(g_settings_render, x + w - radius - 1, y + h - radius - 1, radius, color);
}

static void DrawThinRectOutline(int x, int y, int w, int h, int thickness, uint32_t color) {
    for (int i = 0; i < thickness; ++i) {
        DrawRectOutline(g_settings_render, x + i, y + i, w - i * 2, h - i * 2, color);
    }
}

static void DrawTriangleLeft(int x, int y, int w, int h, uint32_t color) {
    int half = h / 2;
    for (int row = 0; row < h; ++row) {
        int dist = row <= half ? row : h - 1 - row;
        int line_w = 1 + (dist * w) / (half > 0 ? half : 1);
        DrawRect(g_settings_render, x + w - line_w, y + row, line_w, 1, color);
    }
}

static void DrawTriangleRight(int x, int y, int w, int h, uint32_t color) {
    int half = h / 2;
    for (int row = 0; row < h; ++row) {
        int dist = row <= half ? row : h - 1 - row;
        int line_w = 1 + (dist * w) / (half > 0 ? half : 1);
        DrawRect(g_settings_render, x, y + row, line_w, 1, color);
    }
}

static void DrawTriangleUp(int x, int y, int w, int h, uint32_t color) {
    int half = w / 2;
    for (int col = 0; col < w; ++col) {
        int dist = col <= half ? col : w - 1 - col;
        int line_h = 1 + (dist * h) / (half > 0 ? half : 1);
        DrawRect(g_settings_render, x + col, y + h - line_h, 1, line_h, color);
    }
}

static void DrawTriangleDown(int x, int y, int w, int h, uint32_t color) {
    int half = w / 2;
    for (int col = 0; col < w; ++col) {
        int dist = col <= half ? col : w - 1 - col;
        int line_h = 1 + (dist * h) / (half > 0 ? half : 1);
        DrawRect(g_settings_render, x + col, y, 1, line_h, color);
    }
}

static int IsDirectionValue(const wchar_t* text) {
    if (!text || text[1] != 0) {
        return 0;
    }
    return text[0] == L'↑' || text[0] == L'→' || text[0] == L'↓' || text[0] == L'←';
}

static void DrawDirectionValueIcon(int cx, int cy, const wchar_t* text, uint32_t color) {
    if (!text) {
        return;
    }
    if (text[0] == L'↑') {
        DrawTriangleUp(cx - 8, cy - 13, 16, 13, color);
        DrawRect(g_settings_render, cx - 2, cy, 4, 15, color);
    } else if (text[0] == L'→') {
        DrawTriangleRight(cx + 1, cy - 8, 13, 16, color);
        DrawRect(g_settings_render, cx - 14, cy - 2, 15, 4, color);
    } else if (text[0] == L'↓') {
        DrawRect(g_settings_render, cx - 2, cy - 15, 4, 15, color);
        DrawTriangleDown(cx - 8, cy + 2, 16, 13, color);
    } else if (text[0] == L'←') {
        DrawTriangleLeft(cx - 14, cy - 8, 13, 16, color);
        DrawRect(g_settings_render, cx + 1, cy - 2, 15, 4, color);
    }
}

static const SettingsItemDef* SelectedSettingsItem() {
    if (!g_settings_open || g_settings_focus != SETTINGS_FOCUS_ITEM) {
        return 0;
    }
    int index = SettingsItemIndex((SettingsCategory)g_settings_category_selection, g_settings_item_selection);
    return index >= 0 ? SettingsItemAt(index) : 0;
}

static int SelectedSettingsItemIndex() {
    if (!g_settings_open || g_settings_focus != SETTINGS_FOCUS_ITEM) {
        return -1;
    }
    return SettingsItemIndex((SettingsCategory)g_settings_category_selection, g_settings_item_selection);
}

static int SettingsItemFeatureValid(const SettingsItemDef* item) {
    return item &&
           item->status == SETTINGS_ITEM_IMPLEMENTED &&
           item->feature >= 0 &&
           item->feature < FEATURE_COUNT;
}

static int SettingsItemValueDefault(const SettingsItemDef* item) {
    if (!item || item->value_count <= 0) {
        return 0;
    }
    if (item->default_value < 0) {
        return 0;
    }
    if (item->default_value >= item->value_count) {
        return item->value_count - 1;
    }
    return item->default_value;
}

static void SettingsInitDisplayValues() {
    int total_items = SettingsItemTotalCount();
    if (total_items > SETTINGS_UI_MAX_ITEMS) {
        total_items = SETTINGS_UI_MAX_ITEMS;
    }
    for (int i = 0; i < total_items; ++i) {
        g_settings_value_index[i] = SettingsItemValueDefault(SettingsItemAt(i));
    }
}

static void SettingsRegisterInteraction() {
    SettingsUiInvalidateCache();
    g_settings_dirty = 1;
}

void SettingsUiInit(RenderContext* render,
                    SettingsUiFeatureActiveCallback feature_active,
                    SettingsUiToggleFeatureCallback toggle_feature,
                    SettingsUiSetItemValueCallback set_item_value,
                    SettingsUiDrawContextTextCallback draw_context_text) {
    g_settings_render = render;
    g_feature_active = feature_active;
    g_toggle_feature = toggle_feature;
    g_set_item_value = set_item_value;
    g_draw_context_text = draw_context_text;
}

void SettingsUiSetItemValue(int item_index, int value) {
    if (item_index < 0 || item_index >= SETTINGS_UI_MAX_ITEMS || item_index >= SettingsItemTotalCount()) {
        return;
    }
    const SettingsItemDef* item = SettingsItemAt(item_index);
    if (!item || item->value_count <= 0) {
        return;
    }
    if (value < 0) value = 0;
    if (value >= item->value_count) value = item->value_count - 1;
    if (g_settings_value_index[item_index] == value) {
        return;
    }
    g_settings_value_index[item_index] = value;
    SettingsRegisterInteraction();
}

int SettingsUiItemValue(int item_index) {
    if (item_index < 0 || item_index >= SETTINGS_UI_MAX_ITEMS || item_index >= SettingsItemTotalCount()) {
        return 0;
    }
    const SettingsItemDef* item = SettingsItemAt(item_index);
    if (!item || item->value_count <= 0) {
        return 0;
    }
    int value = g_settings_value_index[item_index];
    if (value < 0 || value >= item->value_count) {
        value = SettingsItemValueDefault(item);
    }
    return value;
}

float SettingsUiAudioVolume() {
    int item_index = SettingsAudioVolumeItemIndex();
    const SettingsItemDef* item = item_index >= 0 ? SettingsItemAt(item_index) : 0;
    if (!item || item->value_count <= 1) {
        return 1.0f;
    }
    return (float)SettingsUiItemValue(item_index) / (float)(item->value_count - 1);
}

void SettingsUiReset() {
    g_settings_open = 0;
    g_settings_focus = SETTINGS_FOCUS_CATEGORY;
    g_settings_category_selection = SETTINGS_GAMEPLAY;
    g_settings_item_selection = 0;
    g_settings_fade = 0.0f;
    g_settings_motion = 0.0f;
    g_settings_content_motion = 1.0f;
    g_settings_content_dir = 1.0f;
    g_settings_full_dirty = 0;
    g_settings_cache_valid = 0;
    g_settings_cache_building = 0;
    for (int i = 0; i < SETTINGS_CATEGORY_COUNT; ++i) {
        g_settings_category_alpha[i] = i == g_settings_category_selection ? 1.0f : 0.0f;
    }
    for (int i = 0; i < SETTINGS_UI_MAX_ITEMS; ++i) {
        g_settings_item_alpha[i] = 0.0f;
        g_settings_value_index[i] = 0;
    }
    SettingsInitDisplayValues();
    for (int i = 0; i < FEATURE_COUNT; ++i) {
        g_feature_toggle_motion[i] = g_feature_active && g_feature_active((DeleteFeature)i) ? 1.0f : 0.0f;
    }
    g_settings_dirty = 1;
}

void SettingsUiInvalidateCache() {
    g_settings_cache_valid = 0;
}

void SettingsUiMarkDirty() {
    g_settings_dirty = 1;
}

void SettingsUiMarkFullDirty() {
    g_settings_full_dirty = 1;
    g_settings_dirty = 1;
}

static void SettingsSetTutorialTargetActive(int active) {
    active = active ? 1 : 0;
    if (g_settings_tutorial_target_active != active) {
        g_settings_tutorial_target_active = active;
        g_settings_tutorial_pulse_seconds = 0.0f;
        g_settings_dirty = 1;
    }
}

void SettingsUiClearDirty() {
    g_settings_full_dirty = 0;
    g_settings_dirty = 0;
}

void SettingsUiOpen(int use_static_cache) {
    (void)use_static_cache;
    g_settings_open = 1;
    g_settings_focus = SETTINGS_FOCUS_CATEGORY;
    g_settings_full_dirty = 1;
    if (g_settings_fade < 0.02f) {
        g_settings_fade = 0.02f;
    }
    if (g_settings_motion < 0.02f) {
        g_settings_motion = 0.02f;
    }
    g_settings_content_motion = 1.0f;
    g_settings_dirty = 1;
}

void SettingsUiClose() {
    g_settings_open = 0;
    g_settings_dirty = 1;
}

int SettingsUiIsOpen() {
    return g_settings_open;
}

int SettingsUiOverlayVisible() {
    return g_settings_open || g_settings_fade > 0.001f || g_settings_motion > 0.001f;
}

int SettingsUiAnimationActive() {
    float target = g_settings_open ? 1.0f : 0.0f;
    return SettingsUiOverlayVisible() &&
           (SettingsAbsF(g_settings_fade - target) > 0.001f ||
            SettingsAbsF(g_settings_motion - target) > 0.001f);
}

int SettingsUiIsDirty() {
    return g_settings_dirty;
}

int SettingsUiSelectedCategory() {
    return g_settings_category_selection;
}

int SettingsUiCategoryFocusActive() {
    return g_settings_open && g_settings_focus == SETTINGS_FOCUS_CATEGORY;
}

int SettingsUiItemFocusActive() {
    return g_settings_open && g_settings_focus == SETTINGS_FOCUS_ITEM;
}

DeleteFeature SettingsUiSelectedFeature() {
    const SettingsItemDef* item = SelectedSettingsItem();
    return SettingsItemFeatureValid(item) ? item->feature : FEATURE_COUNT;
}

int SettingsUiHighlightsFeature(DeleteFeature feature) {
    return g_settings_open && SettingsUiSelectedFeature() == feature;
}

void SettingsUiUpdateFade(float dt) {
    float target = g_settings_open ? 1.0f : 0.0f;
    float old_fade = g_settings_fade;
    float old_motion = g_settings_motion;
    float old_content_motion = g_settings_content_motion;

    g_settings_fade = SettingsApproachF(g_settings_fade, target, dt * 6.4f);
    g_settings_motion = SettingsApproachF(g_settings_motion, target, dt * 7.2f);
    g_settings_content_motion = SettingsEaseOutFollowF(g_settings_content_motion, 1.0f, dt, 18.0f);
    if (g_settings_open && g_settings_tutorial_target_active) {
        g_settings_tutorial_pulse_seconds += dt;
        if (g_settings_tutorial_pulse_seconds > 60.0f) {
            g_settings_tutorial_pulse_seconds -= 60.0f;
        }
        g_settings_dirty = 1;
    } else if (!g_settings_tutorial_target_active) {
        g_settings_tutorial_pulse_seconds = 0.0f;
    }

    if (old_fade != g_settings_fade ||
        old_motion != g_settings_motion ||
        old_content_motion != g_settings_content_motion) {
        g_settings_dirty = 1;
    }

    for (int i = 0; i < SETTINGS_CATEGORY_COUNT; ++i) {
        float old_alpha = g_settings_category_alpha[i];
        float alpha_target = i == g_settings_category_selection ? 1.0f : 0.0f;
        g_settings_category_alpha[i] = SettingsEaseOutFollowF(g_settings_category_alpha[i], alpha_target, dt, 34.0f);
        if (old_alpha != g_settings_category_alpha[i]) {
            g_settings_dirty = 1;
        }
    }

    int selected_item_index = SelectedSettingsItemIndex();
    int total_items = SettingsItemTotalCount();
    if (total_items > SETTINGS_UI_MAX_ITEMS) {
        total_items = SETTINGS_UI_MAX_ITEMS;
    }
    for (int i = 0; i < total_items; ++i) {
        float old_alpha = g_settings_item_alpha[i];
        float alpha_target = i == selected_item_index ? 1.0f : 0.0f;
        g_settings_item_alpha[i] = SettingsEaseOutFollowF(g_settings_item_alpha[i], alpha_target, dt, 34.0f);
        if (old_alpha != g_settings_item_alpha[i]) {
            g_settings_dirty = 1;
        }
    }

    for (int i = 0; i < FEATURE_COUNT; ++i) {
        float old_toggle = g_feature_toggle_motion[i];
        float toggle_target = g_feature_active && g_feature_active((DeleteFeature)i) ? 1.0f : 0.0f;
        g_feature_toggle_motion[i] = SettingsEaseOutFollowF(g_feature_toggle_motion[i], toggle_target, dt, 30.0f);
        if (old_toggle != g_feature_toggle_motion[i]) {
            g_settings_dirty = 1;
        }
    }
}

static void SettingsApplyToggleValue(const SettingsItemDef* item, int item_index, int target_index) {
    if (!item || item->value_view != SETTINGS_VALUE_TOGGLE) {
        return;
    }
    if (SettingsItemFeatureValid(item)) {
        if (g_toggle_feature) {
            g_toggle_feature(item->feature);
        }
    } else if (item_index >= 0 && item_index < SETTINGS_UI_MAX_ITEMS) {
        int value = g_settings_value_index[item_index] + 1;
        if (value >= item->value_count) {
            value = 0;
        }
        g_settings_value_index[item_index] = value;
    }
}

static void SettingsStepCurrentValue(const SettingsItemDef* item, int item_index, int dir) {
    if (!item || item_index < 0 || item_index >= SETTINGS_UI_MAX_ITEMS || item->value_count <= 0) {
        return;
    }
    if (item->value_view == SETTINGS_VALUE_TOGGLE) {
        SettingsApplyToggleValue(item, item_index, 0);
        return;
    }

    int value = g_settings_value_index[item_index];
    if (value < 0 || value >= item->value_count) {
        value = SettingsItemValueDefault(item);
    }
    if (item->value_view == SETTINGS_VALUE_CHOICES ||
        item->value_view == SETTINGS_VALUE_STEPS) {
        value += dir;
        if (value < 0) {
            value = item->value_count - 1;
        }
        if (value >= item->value_count) {
            value = 0;
        }
    } else {
        return;
    }
    g_settings_value_index[item_index] = value;
    if (item->status == SETTINGS_ITEM_IMPLEMENTED && g_set_item_value) {
        g_set_item_value(item_index, value);
    }
}

void SettingsUiUpdateInput(int use_static_cache) {
    if (InputWasPressed(KEY_X)) {
        if (g_settings_open) {
            if (g_settings_focus == SETTINGS_FOCUS_ITEM) {
                g_settings_focus = SETTINGS_FOCUS_CATEGORY;
                SettingsRegisterInteraction();
            } else {
                SettingsUiClose();
            }
        } else {
            SettingsUiOpen(use_static_cache);
        }
        return;
    }

    if (!g_settings_open) {
        return;
    }

    if (g_settings_focus == SETTINGS_FOCUS_CATEGORY) {
        if (InputWasPressed(KEY_RIGHT)) {
            if (SettingsItemCount((SettingsCategory)g_settings_category_selection) > 0) {
                g_settings_focus = SETTINGS_FOCUS_ITEM;
                SettingsRegisterInteraction();
            }
            return;
        }
        if (InputWasPressed(KEY_UP)) {
            int old_category = g_settings_category_selection;
            --g_settings_category_selection;
            if (g_settings_category_selection < 0) {
                g_settings_category_selection = SETTINGS_CATEGORY_COUNT - 1;
            }
            g_settings_item_selection = 0;
            g_settings_content_dir = g_settings_category_selection > old_category ? 1.0f : -1.0f;
            g_settings_content_motion = 0.0f;
            SettingsRegisterInteraction();
        }
        if (InputWasPressed(KEY_DOWN)) {
            int old_category = g_settings_category_selection;
            ++g_settings_category_selection;
            if (g_settings_category_selection >= SETTINGS_CATEGORY_COUNT) {
                g_settings_category_selection = 0;
            }
            g_settings_item_selection = 0;
            g_settings_content_dir = g_settings_category_selection < old_category ? -1.0f : 1.0f;
            g_settings_content_motion = 0.0f;
            SettingsRegisterInteraction();
        }
        return;
    }

    int count = SettingsItemCount((SettingsCategory)g_settings_category_selection);
    if (InputWasPressed(KEY_LEFT)) {
        if (count > 0) {
            int item_index = SelectedSettingsItemIndex();
            const SettingsItemDef* item = item_index >= 0 ? SettingsItemAt(item_index) : 0;
            SettingsStepCurrentValue(item, item_index, -1);
            SettingsRegisterInteraction();
        }
        return;
    }
    if (InputWasPressed(KEY_RIGHT)) {
        if (count > 0) {
            int item_index = SelectedSettingsItemIndex();
            const SettingsItemDef* item = item_index >= 0 ? SettingsItemAt(item_index) : 0;
            SettingsStepCurrentValue(item, item_index, 1);
            SettingsRegisterInteraction();
        }
        return;
    }
    if (InputWasPressed(KEY_UP)) {
        if (count > 0) {
            --g_settings_item_selection;
            if (g_settings_item_selection < 0) {
                g_settings_item_selection = count - 1;
            }
            SettingsRegisterInteraction();
        }
    }
    if (InputWasPressed(KEY_DOWN)) {
        if (count > 0) {
            ++g_settings_item_selection;
            if (g_settings_item_selection >= count) {
                g_settings_item_selection = 0;
            }
            SettingsRegisterInteraction();
        }
    }
}

static void DrawSettingsCategoryIndicator(int x, int y, int focused, float selected_alpha, float fade, const SettingsUiColors* colors) {
    float alpha = fade * (0.28f + selected_alpha * 0.72f);
    uint32_t accent = SettingsFadeColor(focused ? COL_TUTORIAL_TARGET : COL_SETTINGS_CRIMSON_DARK, alpha);
    DrawRect(g_settings_render, x - 30, y + 2, 4, 23, accent);
}

static int SettingsEstimateTextWidth(const wchar_t* text, int size);

static void DrawSettingsCategoryRow(int x, int y, SettingsCategory category, float selected_alpha, int tutorial_target, const SettingsUiColors* colors) {
    float fade = SettingsSmooth01(g_settings_fade);
    int focused = g_settings_focus == SETTINGS_FOCUS_CATEGORY && selected_alpha > 0.985f;
    float area_strength = g_settings_focus == SETTINGS_FOCUS_CATEGORY ? 1.0f : 0.92f;
    uint32_t selected_color = focused ? COL_TUTORIAL_TARGET : COL_SETTINGS_TEXT;
    uint32_t base_color = COL_SETTINGS_TEXT_DIM;
    float mix = selected_alpha * area_strength;
    uint32_t color = SettingsLerpColor(base_color, selected_color, mix);
    int bold = selected_alpha > 0.985f && g_settings_focus == SETTINGS_FOCUS_CATEGORY ? 1 : 0;
    int text_size = 23;
    int draw_x = x;
    int draw_y = y - 1;
    if (tutorial_target) {
        int size_delta = (int)(SettingsPulse01(g_settings_tutorial_pulse_seconds) * 4.0f + 0.5f);
        text_size += size_delta;
        int base_w = SettingsEstimateTextWidth(SettingsCategoryAt(category)->name, 23);
        int pulse_w = SettingsEstimateTextWidth(SettingsCategoryAt(category)->name, text_size);
        draw_x -= (pulse_w - base_w) / 2;
        draw_y -= size_delta / 2;
    }
    DrawTextUi(draw_x, draw_y, SettingsCategoryAt(category)->name, text_size, SettingsFadeColor(color, fade), bold);
}

static uint32_t SettingsItemTextColor(float selected_alpha, const SettingsUiColors* colors) {
    return SettingsLerpColor(COL_SETTINGS_TEXT_DIM, COL_SETTINGS_TEXT, selected_alpha);
}

static float SelectedItemAlpha(int item_index) {
    if (item_index >= 0 && item_index < SETTINGS_UI_MAX_ITEMS) {
        return g_settings_item_alpha[item_index];
    }
    return 0.0f;
}

static int SettingsItemValueIndex(const SettingsItemDef* item) {
    int item_index = -1;
    for (int i = 0; i < SettingsItemTotalCount(); ++i) {
        if (SettingsItemAt(i) == item) {
            item_index = i;
            break;
        }
    }
    if (SettingsItemFeatureValid(item)) {
        return g_feature_active && g_feature_active(item->feature) ? 0 : 1;
    }
    if (item_index >= 0 && item_index < SETTINGS_UI_MAX_ITEMS) {
        int value = g_settings_value_index[item_index];
        if (item && value >= 0 && value < item->value_count) {
            return value;
        }
    }
    return item ? SettingsItemValueDefault(item) : 0;
}

static const wchar_t* SettingsCurrentValueText(const SettingsItemDef* item) {
    if (!item || item->value_view == SETTINGS_VALUE_PENDING || !item->values || item->value_count <= 0) {
        return L"기획 중";
    }
    int value_index = SettingsItemValueIndex(item);
    if (value_index < 0 || value_index >= item->value_count) {
        value_index = SettingsItemValueDefault(item);
    }
    return item->values[value_index];
}

static void DrawSmallStepMarks(int x, int y, const SettingsItemDef* item, int value_index, float fade, float area_strength, const SettingsUiColors* colors) {
    if (!item || item->value_count <= 1 || item->value_count > 5 ||
        item->value_view != SETTINGS_VALUE_STEPS) {
        return;
    }
    for (int i = 0; i < item->value_count; ++i) {
        uint32_t color = i == value_index ? COL_SETTINGS_CRIMSON_BRIGHT : COL_SETTINGS_CRIMSON_DEEP;
        DrawRect(g_settings_render, x + i * 12, y, 7, 2, SettingsFadeColor(color, fade * area_strength * (i == value_index ? 0.9f : 0.55f)));
    }
}

static int SettingsItemIsAudioVolume(const SettingsItemDef* item) {
    int volume_index = SettingsAudioVolumeItemIndex();
    return volume_index >= 0 && item == SettingsItemAt(volume_index);
}

static void DrawVolumeStepMeter(int x,
                                int y,
                                const SettingsItemDef* item,
                                int value_index,
                                float fade,
                                float area_strength,
                                int focused,
                                const SettingsUiColors* colors) {
    if (!item || item->value_count <= 1) {
        return;
    }

    const int step_count = 10;
    const int block_w = 10;
    const int block_gap = 5;
    const int base_h = 7;
    int filled = value_index;
    if (filled < 0) filled = 0;
    if (filled > step_count) filled = step_count;

    uint32_t empty_color = COL_SETTINGS_CRIMSON_DEEP;
    uint32_t fill_color = focused ? COL_TUTORIAL_TARGET : COL_SETTINGS_CRIMSON_BRIGHT;
    for (int i = 0; i < step_count; ++i) {
        int h = base_h + i * 2;
        int bx = x + i * (block_w + block_gap);
        int by = y + (base_h + (step_count - 1) * 2) - h;
        int active = i < filled;
        float alpha = fade * area_strength * (active ? 0.88f : 0.24f);
        uint32_t color = active ? fill_color : empty_color;
        DrawRect(g_settings_render, bx, by, block_w, h, SettingsFadeColor(color, alpha));
    }
}

static int SettingsEstimateTextWidth(const wchar_t* text, int size) {
    int width = 0;
    for (const wchar_t* at = text; at && *at; ++at) {
        if (*at == L' ') {
            width += size / 3;
        } else if (*at < 128) {
            width += size * 3 / 5;
        } else {
            width += size;
        }
    }
    return width;
}

static void DrawSettingsValue(int x, int y, int w, const SettingsItemDef* item, float area_strength, int focused, const SettingsUiColors* colors) {
    float fade = SettingsSmooth01(g_settings_fade);
    int value_index = SettingsItemValueIndex(item);
    const wchar_t* value_text = SettingsCurrentValueText(item);
    const int value_center_x = x + w - 96;
    int interactive = item && item->value_view != SETTINGS_VALUE_PENDING && item->value_count > 1;
    uint32_t value_color = item && item->status == SETTINGS_ITEM_IMPLEMENTED ? COL_SETTINGS_TEXT : COL_SETTINGS_TEXT_DISABLED;
    uint32_t control_color = SettingsLerpColor(COL_SETTINGS_CRIMSON_ARROW, COL_SETTINGS_CRIMSON_DARK, area_strength);
    if (item && item->feature == FEATURE_COLLISION_TYPE_A) {
        value_color = value_index == 0 ? COL_SETTINGS_TEXT : COL_SETTINGS_TEXT_DIM;
        control_color = SettingsLerpColor(COL_SETTINGS_CRIMSON_ARROW, COL_SETTINGS_CRIMSON_DARK, area_strength);
    }
    if (focused) {
        value_color = COL_TUTORIAL_TARGET;
        control_color = SettingsLerpColor(COL_SETTINGS_CRIMSON_ARROW, COL_TUTORIAL_TARGET, 0.55f);
    }
    value_color = SettingsLerpColor(COL_SETTINGS_TEXT_DIM, value_color, area_strength);

    if (SettingsItemIsAudioVolume(item)) {
        const int meter_w = 150;
        const int meter_x = x + 28;
        const int meter_y = y + 4;
        uint32_t arrow_color = SettingsFadeColor(control_color, fade * (focused ? 0.78f : (0.34f + area_strength * 0.32f)));
        if (interactive) {
            DrawTriangleLeft(meter_x - 22, y + 11, 8, 12, arrow_color);
            DrawTriangleRight(meter_x + meter_w + 18, y + 11, 8, 12, arrow_color);
        }
        DrawVolumeStepMeter(meter_x, meter_y, item, value_index, fade, area_strength, focused, colors);
        int text_x = x + w - SettingsEstimateTextWidth(value_text, 18);
        DrawTextUi(text_x, y + 4, value_text, 18, SettingsFadeColor(value_color, fade), item && item->status == SETTINGS_ITEM_IMPLEMENTED);
        return;
    }

    if (interactive) {
        uint32_t arrow_color = SettingsFadeColor(control_color, fade * (focused ? 0.78f : (0.34f + area_strength * 0.32f)));
        DrawTriangleLeft(value_center_x - 58, y + 10, 11, 16, arrow_color);
        DrawTriangleRight(value_center_x + 58, y + 10, 11, 16, arrow_color);
    }

    if (IsDirectionValue(value_text)) {
        DrawDirectionValueIcon(value_center_x, y + 19, value_text, SettingsFadeColor(value_color, fade));
    } else {
        int text_x = value_center_x - SettingsEstimateTextWidth(value_text, 21) / 2;
        DrawTextUi(text_x, y + 2, value_text, 21, SettingsFadeColor(value_color, fade), item && item->status == SETTINGS_ITEM_IMPLEMENTED);
    }
    DrawSmallStepMarks(value_center_x - 15, y + 34, item, value_index, fade, area_strength, colors);
}

static void DrawSettingsItemRow(int x, int y, int w, const SettingsItemDef* item, int item_index, int tutorial_target, const SettingsUiColors* colors) {
    float fade = SettingsSmooth01(g_settings_fade);
    float selected_alpha = SelectedItemAlpha(item_index);
    int focused = g_settings_focus == SETTINGS_FOCUS_ITEM && selected_alpha > 0.985f;
    float area_strength = g_settings_focus == SETTINGS_FOCUS_ITEM ? 1.0f : 0.54f;
    uint32_t name_base = g_settings_focus == SETTINGS_FOCUS_ITEM ? COL_SETTINGS_TEXT_DIM : SettingsLerpColor(COL_SETTINGS_TEXT_DIM, COL_SETTINGS_TEXT, 0.18f);
    uint32_t name_focus = focused ? COL_SETTINGS_TEXT : SettingsItemTextColor(selected_alpha, colors);
    float mix = selected_alpha * area_strength;
    uint32_t name_color = SettingsFadeColor(SettingsLerpColor(name_base, name_focus, mix), fade);
    int text_size = 23;
    int draw_x = x;
    int draw_y = y;
    if (tutorial_target) {
        int size_delta = (int)(SettingsPulse01(g_settings_tutorial_pulse_seconds) * 4.0f + 0.5f);
        text_size += size_delta;
        int base_w = SettingsEstimateTextWidth(item->name, 23);
        int pulse_w = SettingsEstimateTextWidth(item->name, text_size);
        draw_x -= (pulse_w - base_w) / 2;
        draw_y -= size_delta / 2;
    }
    DrawTextUi(draw_x, draw_y, item->name, text_size, name_color, 0);
    DrawSettingsValue(x + w - 360, y - 2, 330, item, area_strength, focused, colors);
}

void SettingsUiDrawMenu(const SettingsUiColors* colors, const SettingsUiTutorialState* tutorial) {
    float fade = SettingsSmooth01(g_settings_fade);
    if (fade <= 0.001f || !g_settings_render) {
        return;
    }
    SettingsSetTutorialTargetActive(tutorial && tutorial->active);
    float motion = SettingsSmooth01(g_settings_motion);
    float content_motion = SettingsSmooth01(g_settings_content_motion);
    const int panel_w = 980;
    const int panel_h = 588;
    const int panel_x = (FB_W - panel_w) / 2;
    const int panel_y = (FB_H - panel_h) / 2;
    const int left_w = 284;
    const int sep_x = panel_x + left_w;
    const int right_x = sep_x + 46;
    const int row_w = panel_w - left_w - 80;

    RectI panel_rect = { panel_x, panel_y, panel_w, panel_h };
    DrawUIPanel(g_settings_render, panel_rect, fade);
    DrawRect(g_settings_render, sep_x, panel_y + 124, 1, 386, SettingsFadeColor(COL_SETTINGS_CRIMSON_DARK, fade * 0.62f));
    DrawTextUi(panel_x + 62, panel_y + 58, L"설정", 32, SettingsFadeColor(COL_SETTINGS_TEXT, fade), 0);

    for (int i = 0; i < SETTINGS_CATEGORY_COUNT; ++i) {
        float selected_alpha = g_settings_category_alpha[i];
        int row_y = panel_y + 184 + i * 66;
        int tutorial_target = tutorial->mark_system_category && i == SETTINGS_SYSTEM;
        if (i == g_settings_category_selection && selected_alpha > 0.001f) {
            DrawSettingsCategoryIndicator(panel_x + 86, row_y, g_settings_focus == SETTINGS_FOCUS_CATEGORY, selected_alpha, fade, colors);
        }
        DrawSettingsCategoryRow(panel_x + 86, row_y, (SettingsCategory)i, selected_alpha, tutorial_target, colors);
    }

    SettingsCategory category = (SettingsCategory)g_settings_category_selection;
    int content_x = right_x + (int)((1.0f - content_motion) * g_settings_content_dir * 22.0f);
    float content_fade = fade * (0.35f + content_motion * 0.65f);
    float right_area_strength = g_settings_focus == SETTINGS_FOCUS_ITEM ? 1.0f : 0.78f;
    uint32_t right_header_color = SettingsLerpColor(COL_SETTINGS_TEXT_DIM, COL_SETTINGS_TEXT, right_area_strength);
    DrawTextUi(content_x, panel_y + 116, SettingsCategoryAt(category)->name, 27, SettingsFadeColor(right_header_color, content_fade), 0);
    DrawRect(g_settings_render, content_x, panel_y + 156, 180, 1, SettingsFadeColor(COL_SETTINGS_CRIMSON_DARK, content_fade * (0.26f + right_area_strength * 0.10f)));

    int count = SettingsItemCount(category);
    if (count > 0) {
        if (g_settings_item_selection < 0) g_settings_item_selection = 0;
        if (g_settings_item_selection >= count) g_settings_item_selection = count - 1;
        if (g_settings_focus == SETTINGS_FOCUS_ITEM) {
            int item_y = panel_y + 222 + g_settings_item_selection * 76;
            float selected_item_alpha = 0.0f;
            int selected_index = SelectedSettingsItemIndex();
            selected_item_alpha = SelectedItemAlpha(selected_index);
            DrawRect(g_settings_render, content_x, item_y + 6, 4, 18, SettingsFadeColor(COL_SETTINGS_CRIMSON_BRIGHT, content_fade * selected_item_alpha * 0.44f));
            DrawRect(g_settings_render, content_x + 32, item_y + 31, row_w - 96, 1, SettingsFadeColor(COL_SETTINGS_CRIMSON_DARK, content_fade * selected_item_alpha * 0.38f));
        }
        for (int i = 0; i < count; ++i) {
            int item_index = SettingsItemIndex(category, i);
            if (item_index >= 0) {
                const SettingsItemDef* item = SettingsItemAt(item_index);
                int item_y = panel_y + 222 + i * 76;
                int tutorial_target = tutorial->mark_type_a_setting &&
                                      item->feature == FEATURE_COLLISION_TYPE_A;
                DrawSettingsItemRow(content_x + 32, item_y, row_w - 64, item, item_index, tutorial_target, colors);
                if (i + 1 < count) {
                    DrawRect(g_settings_render, content_x + 32, item_y + 50, row_w - 96, 1, SettingsFadeColor(COL_SETTINGS_CRIMSON_DARK, content_fade * (0.14f + right_area_strength * 0.10f)));
                }
            }
        }
    } else {
        DrawRect(g_settings_render, content_x + 32, panel_y + 228, row_w - 82, 2, SettingsFadeColor(COL_SETTINGS_CRIMSON_DARK, content_fade * 0.48f));
    }

}

RectI SettingsUiMenuDirtyRect() {
    const int panel_w = 980;
    const int panel_h = 588;
    const int panel_x = (FB_W - panel_w) / 2;
    const int panel_y = (FB_H - panel_h) / 2;
    if (g_settings_full_dirty || !SettingsUiOverlayVisible()) {
        RectI full = { 0, 0, FB_W, FB_H };
        return full;
    }
    RectI rect = { panel_x - 12, panel_y - 12, panel_w + 24, panel_h + 24 };
    return SettingsClampRect(rect);
}

void SettingsUiDrawDimRect(RectI rect) {
    rect = SettingsClampRect(rect);
    if (rect.w > 0 && rect.h > 0) {
        DrawModalDimOverlayRect(g_settings_render, rect, SettingsSmooth01(g_settings_fade));
    }
}

void SettingsUiBuildMenuCache(const SettingsUiColors* colors) {
    if (g_settings_cache_valid || g_settings_cache_building || !g_settings_render) {
        return;
    }
    uint32_t* old_pixels = g_settings_render->pixels;
    g_settings_render->pixels = g_settings_cache_supersample_pixels;
    RenderClear(g_settings_render, colors->bg);

    float old_fade = g_settings_fade;
    float old_motion = g_settings_motion;
    int old_full_dirty = g_settings_full_dirty;
    SettingsUiTutorialState tutorial = {};
    tutorial.primary_color = colors->text;
    tutorial.secondary_color = colors->text_dim;
    g_settings_fade = 1.0f;
    g_settings_motion = 1.0f;
    g_settings_full_dirty = 0;
    g_settings_cache_building = 1;
    SettingsUiDrawMenu(colors, &tutorial);
    g_settings_cache_building = 0;
    g_settings_fade = old_fade;
    g_settings_motion = old_motion;
    g_settings_full_dirty = old_full_dirty;

    RectI rect = SettingsUiMenuDirtyRect();
    int sx = rect.x * RENDER_SCALE;
    int sy = rect.y * RENDER_SCALE;
    int sw = rect.w * RENDER_SCALE;
    int sh = rect.h * RENDER_SCALE;
    for (int y = 0; y < sh; ++y) {
        uint32_t* src = g_settings_cache_supersample_pixels + (sy + y) * RENDER_W + sx;
        unsigned char* alpha = g_settings_cache_alpha + (sy + y) * RENDER_W + sx;
        for (int x = 0; x < sw; ++x) {
            alpha[x] = src[x] == colors->bg ? 0 : 255;
        }
    }

    g_settings_render->pixels = old_pixels;
    g_settings_cache_valid = 1;
}
