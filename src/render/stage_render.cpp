#include "stage_render.h"

#include "exit_sequence.h"
#include "game_config.h"
#include "settings_ui.h"
#include "tutorial_ui.h"

static float StageClampF(float value, float lo, float hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static uint32_t StageLerpColor(uint32_t a, uint32_t b, float t) {
    t = StageClampF(t, 0.0f, 1.0f);
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

static void DrawPlatform(const StageRenderState* state, const RectF* r) {
    int x = WorldX(state->render, r->x);
    int y = WorldY(state->render, r->y);
    int w = (int)(r->w + 0.5f);
    int h = (int)(r->h + 0.5f);
    DrawRect(state->render, x, y, w, h, state->platform_color);
}

static int StageMaxI(int a, int b) {
    return a > b ? a : b;
}

static int StageMinI(int a, int b) {
    return a < b ? a : b;
}

static void DrawObjectClippedRect(const StageRenderState* state,
                                  int object_x,
                                  int object_y,
                                  int object_w,
                                  int object_h,
                                  int x,
                                  int y,
                                  int w,
                                  int h,
                                  uint32_t color) {
    int x0 = StageMaxI(x, object_x);
    int y0 = StageMaxI(y, object_y);
    int x1 = StageMinI(x + w, object_x + object_w);
    int y1 = StageMinI(y + h, object_y + object_h);
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    DrawRect(state->render, x0, y0, x1 - x0, y1 - y0, color);
}

struct TypeABrickPatternRect {
    int x;
    int y;
    int w;
    int h;
};

static const TypeABrickPatternRect g_type_a_brick_pattern_rects[] = {
    { 0, 6, 17, 14 },
    { 23, 6, 17, 14 },
    { 0, 26, 4, 14 },
    { 10, 26, 20, 14 },
    { 36, 26, 4, 14 },
};

static const TypeABrickPatternRect g_type_a_brick_pattern_alt_rects[] = {
    { 0, 6, 4, 14 },
    { 10, 6, 20, 14 },
    { 36, 6, 4, 14 },
    { 0, 26, 17, 14 },
    { 23, 26, 17, 14 },
};

static void DrawTypeABrickTilePattern(const StageRenderState* state,
                                      int object_x,
                                      int object_y,
                                      int object_w,
                                      int object_h,
                                      int tile_x,
                                      int tile_y,
                                      int tile_col,
                                      uint32_t pattern_color) {
    const TypeABrickPatternRect* rects = g_type_a_brick_pattern_rects;
    int rect_count = (int)(sizeof(g_type_a_brick_pattern_rects) / sizeof(g_type_a_brick_pattern_rects[0]));
    if ((tile_col & 1) != 0) {
        rects = g_type_a_brick_pattern_alt_rects;
        rect_count = (int)(sizeof(g_type_a_brick_pattern_alt_rects) / sizeof(g_type_a_brick_pattern_alt_rects[0]));
    }
    for (int i = 0; i < rect_count; ++i) {
        const TypeABrickPatternRect* rect = &rects[i];
        DrawObjectClippedRect(state,
                              object_x,
                              object_y,
                              object_w,
                              object_h,
                              tile_x + rect->x,
                              tile_y + rect->y,
                              rect->w,
                              rect->h,
                              pattern_color);
    }
}

static void DrawTypeATilePattern(const StageRenderState* state, int x, int y, int w, int h, uint32_t pattern_color) {
    const int tile_size = 40;
    for (int local_y = 0; local_y < h; local_y += tile_size) {
        for (int local_x = 0, tile_col = 0; local_x < w; local_x += tile_size, ++tile_col) {
            int tile_x = x + local_x;
            int tile_y = y + local_y;
            DrawTypeABrickTilePattern(state, x, y, w, h, tile_x, tile_y, tile_col, pattern_color);
        }
    }
}

static void DrawTypeAWall(const StageRenderState* state, const RectF* wall, int highlight);

static void DrawBrickRect(const StageRenderState* state, const RectF* wall, uint32_t base_color, uint32_t pattern_color) {
    int x = WorldX(state->render, wall->x);
    int y = WorldY(state->render, wall->y);
    int w = (int)(wall->w + 0.5f);
    int h = (int)(wall->h + 0.5f);
    DrawRect(state->render, x, y, w, h, base_color);
    DrawTypeATilePattern(state, x, y, w, h, pattern_color);
}

static float StageWrap01(float value) {
    int whole = (int)value;
    value -= (float)whole;
    if (value < 0.0f) {
        value += 1.0f;
    }
    return value;
}

static uint32_t StageScaleColor(uint32_t color, float scale) {
    scale = StageClampF(scale, 0.0f, 1.0f);
    int r = (int)((float)((color >> 16) & 255) * scale + 0.5f);
    int g = (int)((float)((color >> 8) & 255) * scale + 0.5f);
    int b = (int)((float)(color & 255) * scale + 0.5f);
    return (uint32_t)((r << 16) | (g << 8) | b);
}

static void DrawObjectClippedRectOutline(const StageRenderState* state,
                                         int object_x,
                                         int object_y,
                                         int object_w,
                                         int object_h,
                                         int x,
                                         int y,
                                         int w,
                                         int h,
                                         int thickness,
                                         uint32_t color) {
    if (w <= 0 || h <= 0 || thickness <= 0) {
        return;
    }
    DrawObjectClippedRect(state, object_x, object_y, object_w, object_h, x, y, w, thickness, color);
    DrawObjectClippedRect(state, object_x, object_y, object_w, object_h, x, y + h - thickness, w, thickness, color);
    DrawObjectClippedRect(state, object_x, object_y, object_w, object_h, x, y, thickness, h, color);
    DrawObjectClippedRect(state, object_x, object_y, object_w, object_h, x + w - thickness, y, thickness, h, color);
}

static void DrawTypeABrickTileWireTrace(const StageRenderState* state,
                                        int object_x,
                                        int object_y,
                                        int object_w,
                                        int object_h,
                                        int tile_x,
                                        int tile_y,
                                        int tile_col,
                                        int tile_row,
                                        uint32_t color) {
    const TypeABrickPatternRect* rects = g_type_a_brick_pattern_rects;
    int rect_count = (int)(sizeof(g_type_a_brick_pattern_rects) / sizeof(g_type_a_brick_pattern_rects[0]));
    if ((tile_col & 1) != 0) {
        rects = g_type_a_brick_pattern_alt_rects;
        rect_count = (int)(sizeof(g_type_a_brick_pattern_alt_rects) / sizeof(g_type_a_brick_pattern_alt_rects[0]));
    }
    for (int i = 0; i < rect_count; ++i) {
        const TypeABrickPatternRect* rect = &rects[i];
        DrawObjectClippedRectOutline(state,
                                     object_x,
                                     object_y,
                                     object_w,
                                     object_h,
                                     tile_x + rect->x,
                                     tile_y + rect->y,
                                     rect->w,
                                     rect->h,
                                     1,
                                     color);
    }
}

static void DrawTypeAOffAnimatedTrace(const StageRenderState* state, const RectF* wall, uint32_t pattern_color) {
    int x = WorldX(state->render, wall->x);
    int y = WorldY(state->render, wall->y);
    int w = (int)(wall->w + 0.5f);
    int h = (int)(wall->h + 0.5f);
    const int tile_size = 40;
    uint32_t trace_color = StageScaleColor(pattern_color, 0.42f);
    for (int local_y = 0, tile_row = 0; local_y < h; local_y += tile_size, ++tile_row) {
        for (int local_x = 0, tile_col = 0; local_x < w; local_x += tile_size, ++tile_col) {
            DrawTypeABrickTileWireTrace(state,
                                        x,
                                        y,
                                        w,
                                        h,
                                        x + local_x,
                                        y + local_y,
                                        tile_col,
                                        tile_row,
                                        trace_color);
        }
    }
}

static void DrawBrickOffRect(const StageRenderState* state, const RectF* wall, uint32_t base_color, uint32_t pattern_color) {
    (void)base_color;
    DrawTypeAOffAnimatedTrace(state, wall, pattern_color);
}

static void DrawTypeATileCell(const StageRenderState* state, int x, int y) {
    RectF tile = { (float)x, (float)y, 40.0f, 40.0f };
    DrawTypeAWall(state, &tile, 0);
}

static void DrawTypeAGappedTiles(const StageRenderState* state, int x, int y, int cols, int rows) {
    const int tile_size = 40;
    const int gap = 4;
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            DrawTypeATileCell(state, x + col * (tile_size + gap), y + row * (tile_size + gap));
        }
    }
}

static void DrawPreviewRect(const StageRenderState* state, int x, int y, int w, int h, uint32_t color, int scale) {
    DrawRect(state->render, x, y, w * scale, h * scale, color);
}

static void DrawTypeATilePreview(const StageRenderState* state, int x, int y, int scale, uint32_t base_color, uint32_t pattern_color) {
    DrawRect(state->render, x, y, 40 * scale, 40 * scale, base_color);
    DrawPreviewRect(state, x + 2 * scale, y + 3 * scale, 36, 9, pattern_color, scale);
    DrawPreviewRect(state, x, y + 16 * scale, 18, 9, pattern_color, scale);
    DrawPreviewRect(state, x + 22 * scale, y + 16 * scale, 18, 9, pattern_color, scale);
    DrawPreviewRect(state, x + 2 * scale, y + 29 * scale, 36, 8, pattern_color, scale);
}

static void DrawTypeAPreview(const StageRenderState* state) {
    RectF on = { 270.0f, 150.0f, 120.0f, 160.0f };
    RectF off = { 710.0f, 150.0f, 120.0f, 160.0f };
    DrawBrickRect(state, &on, state->type_a_color, state->type_a_pattern_color);
    DrawBrickOffRect(state, &off, state->type_a_off_color, state->type_a_off_pattern_color);
}

static void DrawTypeAOnPreview(const StageRenderState* state) {
    RectF on = { 500.0f, 150.0f, 120.0f, 160.0f };
    DrawBrickRect(state, &on, state->type_a_color, state->type_a_pattern_color);
}

static void DrawTypeAWall(const StageRenderState* state, const RectF* wall, int highlight) {
    (void)highlight;
    if (state->type_a_active) {
        DrawBrickRect(state, wall, state->type_a_color, state->type_a_pattern_color);
    } else {
        DrawBrickOffRect(state, wall, state->type_a_off_color, state->type_a_off_pattern_color);
    }
}

static void DrawSpeakerSlot(RenderContext* render, int x, int y, int w, int h, uint32_t color) {
    int radius = h / 2;
    DrawRect(render, x + radius, y, w - radius * 2, h, color);
    FillCircle(render, x + radius, y + radius, radius, color);
    FillCircle(render, x + w - radius - 1, y + radius, radius, color);
}

static void DrawSpeakerDriver(RenderContext* render,
                              int cx,
                              int cy,
                              int radius,
                              uint32_t bright,
                              uint32_t dark,
                              uint32_t body_light,
                              uint32_t deep_dark) {
    FillCircle(render, cx, cy, radius, bright);
    FillCircle(render, cx, cy, radius * 88 / 100, dark);
    FillCircle(render, cx, cy, radius * 62 / 100, body_light);
    FillCircle(render, cx, cy, radius * 28 / 100, deep_dark);
}

static void DrawSmallSpeakerDriver(RenderContext* render,
                                   int cx,
                                   int cy,
                                   int radius,
                                   uint32_t bright,
                                   uint32_t dark,
                                   uint32_t body_light,
                                   uint32_t deep_dark) {
    FillCircle(render, cx, cy, radius, bright);
    FillCircle(render, cx, cy, radius * 83 / 100, dark);
    FillCircle(render, cx, cy, radius * 56 / 100, body_light);
    FillCircle(render, cx, cy, radius * 27 / 100, deep_dark);
}

static void DrawSpeakerDevice(const StageRenderState* state, const SpeakerDevice* speaker) {
    static const uint32_t SPEAKER_BODY = 0x00a93643;
    static const uint32_t SPEAKER_BODY_LIGHT = 0x00c94a58;
    static const uint32_t SPEAKER_BRIGHT = 0x00e66b76;
    static const uint32_t SPEAKER_HIGHLIGHT = 0x00f08a92;
    static const uint32_t SPEAKER_DARK = 0x0070272f;
    static const uint32_t SPEAKER_DEEP_DARK = 0x004b2428;
    static const uint32_t SPEAKER_BRACKET = 0x007b2b34;

    RenderContext* render = state->render;
    int x = WorldX(render, speaker->x);
    int y = WorldY(render, speaker->y);
    int w = (int)(speaker->width + 0.5f);
    int h = (int)(speaker->height + 0.5f);
    int body_right = x + w;
    int side_w = StageMaxI(12, w * 10 / 100);
    int front_w = w - side_w;
    int wall_x = WorldX(render, state->room->bounds.x + state->room->bounds.w - 120.0f);
    int plate_w = StageMaxI(14, w * 9 / 100);
    int plate_x = wall_x - plate_w / 2;
    int plate_y = y + h * 31 / 100;
    int plate_h = h * 58 / 100;
    int upper_support_y = y + h * 36 / 100;
    int lower_support_y = y + h * 67 / 100;
    int support_h = StageMaxI(8, h * 5 / 100);

    DrawRect(render, body_right - 2, upper_support_y - support_h / 2, plate_x - body_right + plate_w / 2, support_h, SPEAKER_BRACKET);
    DrawRect(render, body_right - 2, lower_support_y - support_h / 2, plate_x - body_right + plate_w / 2, support_h, SPEAKER_BRACKET);
    DrawThickLine(render,
                  body_right + support_h / 2,
                  lower_support_y - support_h / 2,
                  plate_x + plate_w / 2,
                  upper_support_y + support_h / 2,
                  support_h,
                  SPEAKER_BRACKET);

    DrawRect(render, x, y, w, h, SPEAKER_BODY);
    DrawRect(render, x, y, w, StageMaxI(3, h * 1 / 100), SPEAKER_BODY_LIGHT);
    DrawRect(render, x, y, StageMaxI(3, w * 2 / 100), h, SPEAKER_BODY_LIGHT);
    DrawRect(render, body_right - side_w, y, side_w, h, SPEAKER_DARK);
    DrawRectOutline(render, x, y, w, h, SPEAKER_DEEP_DARK);

    int slot_x = x + w * 12 / 100;
    int slot_y = y + h * 14 / 100;
    int slot_w = w * 23 / 100;
    int slot_h = StageMaxI(7, h * 3 / 100);
    int slot_gap = StageMaxI(15, h * 6 / 100);
    for (int i = 0; i < 4; ++i) {
        DrawSpeakerSlot(render, slot_x, slot_y + i * slot_gap, slot_w, slot_h, SPEAKER_DARK);
    }

    int small_cx = x + w * 64 / 100;
    int small_cy = y + h * 23 / 100;
    int big_cx = x + front_w / 2;
    int big_cy = y + h * 66 / 100;
    DrawSmallSpeakerDriver(render, small_cx, small_cy, h * 11 / 100, SPEAKER_HIGHLIGHT, SPEAKER_DARK, SPEAKER_BRIGHT, SPEAKER_DEEP_DARK);
    DrawSpeakerDriver(render, big_cx, big_cy, h * 22 / 100, SPEAKER_HIGHLIGHT, SPEAKER_DARK, SPEAKER_BRIGHT, SPEAKER_DEEP_DARK);

    int screw_r = StageMaxI(5, w * 3 / 100);
    int screw_inset = StageMaxI(20, w * 12 / 100);
    FillCircle(render, x + screw_inset, y + screw_inset, screw_r, SPEAKER_DARK);
    FillCircle(render, x + front_w - screw_inset, y + screw_inset, screw_r, SPEAKER_DARK);
    FillCircle(render, x + screw_inset, y + h - screw_inset, screw_r, SPEAKER_DARK);
    FillCircle(render, x + front_w - screw_inset, y + h - screw_inset, screw_r, SPEAKER_DARK);

    DrawRect(render, plate_x, plate_y + plate_w / 2, plate_w, plate_h - plate_w, SPEAKER_BRACKET);
    FillCircle(render, plate_x + plate_w / 2, plate_y + plate_w / 2, plate_w / 2, SPEAKER_BRACKET);
    FillCircle(render, plate_x + plate_w / 2, plate_y + plate_h - plate_w / 2 - 1, plate_w / 2, SPEAKER_BRACKET);
}

static void DrawSettingsMenu(const StageRenderState* state) {
    SettingsUiColors colors;
    colors.bg = state->bg_color;
    colors.text = state->text_color;
    colors.text_dim = state->text_dim_color;
    colors.type_a = state->type_a_color;
    colors.main_red = state->platform_color;
    colors.bright_red = 0x00f04a5b;
    colors.accent = state->effect_color;
    SettingsUiTutorialState tutorial = TutorialUiSettingsState();
    SettingsUiDrawMenu(&colors, &tutorial);
}

static void DrawContextUi(const StageRenderState* state) {
    TutorialUiDrawWorldHint(state->render, state->room, state->player);
    ExitSequenceDrawSolvedUi(state->render, state->text_color, state->draw_text_small);
    ExitSequenceDrawTransition(state->render);
}

void StageRenderDrawStatic(const StageRenderState* state) {
    for (int i = 0; i < state->room->platform_count; ++i) {
        DrawPlatform(state, &state->room->platforms[i]);
    }
    for (int i = 0; i < state->room->type_a_count; ++i) {
        DrawTypeAWall(state, &state->room->type_a_walls[i], 0);
    }
    for (int i = 0; i < state->room->speaker_count; ++i) {
        DrawSpeakerDevice(state, &state->room->speakers[i]);
    }
}

void StageRenderDrawDynamic(const StageRenderState* state) {
    if (state->highlight_type_a || state->type_a_bump_visible || state->type_a_setting_feedback_visible) {
        for (int i = 0; i < state->room->type_a_count; ++i) {
            DrawTypeAWall(state, &state->room->type_a_walls[i], 1);
        }
    }
    ExitSequenceDrawExit(state->render, &state->room->exit);
    DrawPlayerParticles(state->render, state->player_particles, state->player_particle_count, state->effect_color);
    if (state->player_visible) {
        DrawPlayer(state->render,
                   state->player,
                   state->player_color,
                   StageLerpColor(state->bg_color, state->platform_color, 0.78f),
                   state->gravity_direction);
    }

    DrawContextUi(state);
    if (state->settings_overlay_visible) {
        RectI full = { 0, 0, FB_W, FB_H };
        SettingsUiDrawDimRect(full);
        DrawSettingsMenu(state);
    }
}

void StageRenderDrawFrame(const StageRenderState* state) {
    RenderClear(state->render, state->bg_color);
    StageRenderDrawStatic(state);
    StageRenderDrawDynamic(state);
}

void StageRenderDrawTypeAArtTest(const StageRenderState* state, int mode) {
    RenderClear(state->render, state->bg_color);
    if (mode == 2) {
        DrawTypeAGappedTiles(state, 120, 110, 1, 1);
        DrawTypeAGappedTiles(state, 260, 110, 2, 1);
        DrawTypeAGappedTiles(state, 480, 110, 3, 1);
        DrawTypeAGappedTiles(state, 120, 250, 1, 2);
        DrawTypeAGappedTiles(state, 260, 250, 1, 3);
        DrawTypeAGappedTiles(state, 480, 250, 2, 2);
        DrawTypeAGappedTiles(state, 720, 250, 3, 2);
        return;
    }
    if (mode == 3) {
        DrawTypeAPreview(state);
        return;
    }
    if (mode == 4) {
        DrawTypeAOnPreview(state);
        return;
    }

    RectF type_1x1 = { 120.0f, 110.0f, 40.0f, 40.0f };
    RectF type_2x1 = { 240.0f, 110.0f, 80.0f, 40.0f };
    RectF type_3x1 = { 400.0f, 110.0f, 120.0f, 40.0f };
    RectF type_1x2 = { 120.0f, 240.0f, 40.0f, 80.0f };
    RectF type_1x3 = { 240.0f, 220.0f, 40.0f, 120.0f };
    RectF type_2x2 = { 400.0f, 240.0f, 80.0f, 80.0f };
    RectF type_3x2 = { 600.0f, 240.0f, 120.0f, 80.0f };
    DrawTypeAWall(state, &type_1x1, 0);
    DrawTypeAWall(state, &type_2x1, 0);
    DrawTypeAWall(state, &type_3x1, 0);
    DrawTypeAWall(state, &type_1x2, 0);
    DrawTypeAWall(state, &type_1x3, 0);
    DrawTypeAWall(state, &type_2x2, 0);
    DrawTypeAWall(state, &type_3x2, 0);
}
