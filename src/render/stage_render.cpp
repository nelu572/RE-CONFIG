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

static int StageAbsI(int value) {
    return value < 0 ? -value : value;
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

struct SpeakerRoomAirRect {
    float left;
    float right;
    float top;
    float bottom;
};

static SpeakerRoomAirRect SpeakerRoomAirRectForRoom(const RoomDef* room) {
    const float side_wall_w = 120.0f;
    const float ceiling_h = 160.0f;
    const float floor_h = 160.0f;

    SpeakerRoomAirRect rect;
    rect.left = room->bounds.x + side_wall_w;
    rect.right = room->bounds.x + room->bounds.w - side_wall_w;
    rect.top = room->bounds.y + ceiling_h;
    rect.bottom = room->bounds.y + room->bounds.h - floor_h;
    return rect;
}

static void SpeakerInsetRoomAirRect(SpeakerRoomAirRect* rect, float inset) {
    rect->left += inset;
    rect->right -= inset;
    rect->top += inset;
    rect->bottom -= inset;
    if (rect->left > rect->right) {
        float center = (rect->left + rect->right) * 0.5f;
        rect->left = center;
        rect->right = center;
    }
    if (rect->top > rect->bottom) {
        float center = (rect->top + rect->bottom) * 0.5f;
        rect->top = center;
        rect->bottom = center;
    }
}

static int SpeakerClipLineTest(float p, float q, float* t0, float* t1) {
    if (p > -0.0001f && p < 0.0001f) {
        return q >= 0.0f;
    }

    float r = q / p;
    if (p < 0.0f) {
        if (r > *t1) {
            return 0;
        }
        if (r > *t0) {
            *t0 = r;
        }
    } else {
        if (r < *t0) {
            return 0;
        }
        if (r < *t1) {
            *t1 = r;
        }
    }
    return 1;
}

static int SpeakerClipLineToRoomAir(const SpeakerRoomAirRect* rect,
                                    float* x0,
                                    float* y0,
                                    float* x1,
                                    float* y1) {
    float ox0 = *x0;
    float oy0 = *y0;
    float dx = *x1 - ox0;
    float dy = *y1 - oy0;
    float t0 = 0.0f;
    float t1 = 1.0f;

    if (!SpeakerClipLineTest(-dx, ox0 - rect->left, &t0, &t1)) {
        return 0;
    }
    if (!SpeakerClipLineTest(dx, rect->right - ox0, &t0, &t1)) {
        return 0;
    }
    if (!SpeakerClipLineTest(-dy, oy0 - rect->top, &t0, &t1)) {
        return 0;
    }
    if (!SpeakerClipLineTest(dy, rect->bottom - oy0, &t0, &t1)) {
        return 0;
    }

    *x0 = ox0 + dx * t0;
    *y0 = oy0 + dy * t0;
    *x1 = ox0 + dx * t1;
    *y1 = oy0 + dy * t1;
    return 1;
}

static void DrawSpeakerAlphaLine(RenderContext* render,
                                 int x0,
                                 int y0,
                                 int x1,
                                 int y1,
                                 int thickness,
                                 uint32_t color,
                                 float alpha) {
    alpha = StageClampF(alpha, 0.0f, 1.0f);
    if (alpha <= 0.0f || thickness <= 0) {
        return;
    }

    float r = (float)((color >> 16) & 255) / 255.0f;
    float g = (float)((color >> 8) & 255) / 255.0f;
    float b = (float)(color & 255) / 255.0f;
    int dx = x1 - x0;
    int dy = y1 - y0;
    int steps = StageMaxI(StageAbsI(dx), StageAbsI(dy));
    int half = thickness / 2;
    if (steps <= 0) {
        BlendPixel(render, x0, y0, r, g, b, alpha);
        return;
    }

    for (int i = 0; i <= steps; ++i) {
        int x = x0 + dx * i / steps;
        int y = y0 + dy * i / steps;
        for (int oy = -half; oy <= half; ++oy) {
            for (int ox = -half; ox <= half; ++ox) {
                BlendPixel(render, x + ox, y + oy, r, g, b, alpha);
            }
        }
    }
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

static const uint32_t SPEAKER_BODY = 0x00a93643;
static const uint32_t SPEAKER_BODY_LIGHT = 0x00c94a58;
static const uint32_t SPEAKER_BRIGHT = 0x00e66b76;
static const uint32_t SPEAKER_HIGHLIGHT = 0x00f08a92;
static const uint32_t SPEAKER_DARK = 0x0070272f;
static const uint32_t SPEAKER_DEEP_DARK = 0x004b2428;
static const uint32_t SPEAKER_BRACKET = 0x007b2b34;
static constexpr float SPEAKER_WAVE_SPACING = 220.0f;
static constexpr float SPEAKER_WAVE_SPEED = 520.0f;
static constexpr float SPEAKER_WAVE_RANGE = 1120.0f;
static constexpr float SPEAKER_WAVE_START_RADIUS = 42.0f;

struct SpeakerAnimation {
    int body_x;
    int body_y;
    int body_w_extra;
    int body_h_extra;
    int big_radius_extra;
    int small_radius_extra;
};

static SpeakerAnimation SpeakerAnimationForTime(double time_seconds) {
    float wave_distance = (float)time_seconds * SPEAKER_WAVE_SPEED - SPEAKER_WAVE_START_RADIUS;
    float phase = StageWrap01(wave_distance / SPEAKER_WAVE_SPACING);
    float primary = StageClampF(1.0f - phase / 0.18f, 0.0f, 1.0f);
    float rebound = 0.0f;
    float kick = 0.0f;
    if (phase > 0.16f) {
        rebound = StageClampF(1.0f - (phase - 0.16f) / 0.16f, 0.0f, 1.0f);
    }
    if (phase > 0.36f && phase < 0.48f) {
        kick = StageClampF(1.0f - (phase - 0.36f) / 0.12f, 0.0f, 1.0f);
    }

    primary *= primary;
    rebound *= rebound;
    kick *= kick;

    SpeakerAnimation anim;
    anim.body_x = -(int)(kick * 1.0f + 0.5f);
    anim.body_y = -(int)(rebound * 1.0f + 0.5f);
    anim.body_w_extra = (int)(primary * 24.0f + rebound * 10.0f + kick * 7.0f + 0.5f);
    anim.body_h_extra = (int)(primary * 18.0f + rebound * 8.0f + kick * 6.0f + 0.5f);
    anim.big_radius_extra = (int)(primary * 18.0f + rebound * 8.0f + kick * 6.0f + 0.5f);
    anim.small_radius_extra = (int)(primary * 10.0f + rebound * 4.0f + kick * 3.0f + 0.5f);
    return anim;
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
    RenderContext* render = state->render;
    SpeakerAnimation anim = SpeakerAnimationForTime(state->render_time_seconds);
    int base_x = WorldX(render, speaker->x);
    int base_y = WorldY(render, speaker->y);
    int base_w = (int)(speaker->width + 0.5f);
    int base_h = (int)(speaker->height + 0.5f);
    int x = base_x + anim.body_x - anim.body_w_extra;
    int y = base_y + anim.body_y - anim.body_h_extra / 2;
    int w = base_w + anim.body_w_extra;
    int h = base_h + anim.body_h_extra;
    int body_right = x + w;
    int side_w = StageMaxI(12, w * 10 / 100);
    int front_w = w - side_w;
    int wall_x = WorldX(render, state->room->bounds.x + state->room->bounds.w - 120.0f);
    int plate_w = StageMaxI(14, base_w * 9 / 100);
    int plate_x = wall_x - plate_w / 2;
    int plate_y = base_y + base_h * 31 / 100;
    int plate_h = base_h * 58 / 100;
    int upper_support_y = y + h * 36 / 100;
    int lower_support_y = y + h * 67 / 100;
    int support_h = StageMaxI(8, base_h * 5 / 100);

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
    DrawSmallSpeakerDriver(render, small_cx, small_cy, h * 11 / 100 + anim.small_radius_extra, SPEAKER_HIGHLIGHT, SPEAKER_DARK, SPEAKER_BRIGHT, SPEAKER_DEEP_DARK);
    DrawSpeakerDriver(render, big_cx, big_cy, h * 22 / 100 + anim.big_radius_extra, SPEAKER_HIGHLIGHT, SPEAKER_DARK, SPEAKER_BRIGHT, SPEAKER_DEEP_DARK);

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

static void DrawSpeakerWaveCircle(const StageRenderState* state,
                                  float world_cx,
                                  float world_cy,
                                  int radius,
                                  int thickness,
                                  uint32_t color,
                                  float fade) {
    struct SpeakerCirclePoint {
        int x;
        int y;
    };
    static const SpeakerCirclePoint circle_points[] = {
        {  4096,     0 }, {  4091,   201 }, {  4076,   401 }, {  4052,   601 },
        {  4017,   799 }, {  3973,   995 }, {  3920,  1189 }, {  3857,  1380 },
        {  3784,  1567 }, {  3703,  1751 }, {  3612,  1931 }, {  3513,  2106 },
        {  3406,  2276 }, {  3290,  2440 }, {  3166,  2598 }, {  3035,  2751 },
        {  2896,  2896 }, {  2751,  3035 }, {  2598,  3166 }, {  2440,  3290 },
        {  2276,  3406 }, {  2106,  3513 }, {  1931,  3612 }, {  1751,  3703 },
        {  1567,  3784 }, {  1380,  3857 }, {  1189,  3920 }, {   995,  3973 },
        {   799,  4017 }, {   601,  4052 }, {   401,  4076 }, {   201,  4091 },
        {     0,  4096 }, {  -201,  4091 }, {  -401,  4076 }, {  -601,  4052 },
        {  -799,  4017 }, {  -995,  3973 }, { -1189,  3920 }, { -1380,  3857 },
        { -1567,  3784 }, { -1751,  3703 }, { -1931,  3612 }, { -2106,  3513 },
        { -2276,  3406 }, { -2440,  3290 }, { -2598,  3166 }, { -2751,  3035 },
        { -2896,  2896 }, { -3035,  2751 }, { -3166,  2598 }, { -3290,  2440 },
        { -3406,  2276 }, { -3513,  2106 }, { -3612,  1931 }, { -3703,  1751 },
        { -3784,  1567 }, { -3857,  1380 }, { -3920,  1189 }, { -3973,   995 },
        { -4017,   799 }, { -4052,   601 }, { -4076,   401 }, { -4091,   201 },
        { -4096,     0 }, { -4091,  -201 }, { -4076,  -401 }, { -4052,  -601 },
        { -4017,  -799 }, { -3973,  -995 }, { -3920, -1189 }, { -3857, -1380 },
        { -3784, -1567 }, { -3703, -1751 }, { -3612, -1931 }, { -3513, -2106 },
        { -3406, -2276 }, { -3290, -2440 }, { -3166, -2598 }, { -3035, -2751 },
        { -2896, -2896 }, { -2751, -3035 }, { -2598, -3166 }, { -2440, -3290 },
        { -2276, -3406 }, { -2106, -3513 }, { -1931, -3612 }, { -1751, -3703 },
        { -1567, -3784 }, { -1380, -3857 }, { -1189, -3920 }, {  -995, -3973 },
        {  -799, -4017 }, {  -601, -4052 }, {  -401, -4076 }, {  -201, -4091 },
        {     0, -4096 }, {   201, -4091 }, {   401, -4076 }, {   601, -4052 },
        {   799, -4017 }, {   995, -3973 }, {  1189, -3920 }, {  1380, -3857 },
        {  1567, -3784 }, {  1751, -3703 }, {  1931, -3612 }, {  2106, -3513 },
        {  2276, -3406 }, {  2440, -3290 }, {  2598, -3166 }, {  2751, -3035 },
        {  2896, -2896 }, {  3035, -2751 }, {  3166, -2598 }, {  3290, -2440 },
        {  3406, -2276 }, {  3513, -2106 }, {  3612, -1931 }, {  3703, -1751 },
        {  3784, -1567 }, {  3857, -1380 }, {  3920, -1189 }, {  3973,  -995 },
        {  4017,  -799 }, {  4052,  -601 }, {  4076,  -401 }, {  4091,  -201 },
        {  4096,     0 },
    };

    if (radius <= 0 || thickness <= 0) {
        return;
    }

    RenderContext* render = state->render;
    SpeakerRoomAirRect air_rect = SpeakerRoomAirRectForRoom(state->room);
    SpeakerInsetRoomAirRect(&air_rect, (float)thickness * 0.5f);
    int point_count = (int)(sizeof(circle_points) / sizeof(circle_points[0]));
    for (int i = 1; i < point_count; ++i) {
        const SpeakerCirclePoint* a = &circle_points[i - 1];
        const SpeakerCirclePoint* b = &circle_points[i];
        float ax = world_cx + (float)(radius * a->x) / 4096.0f;
        float ay = world_cy + (float)(radius * a->y) / 4096.0f;
        float bx = world_cx + (float)(radius * b->x) / 4096.0f;
        float by = world_cy + (float)(radius * b->y) / 4096.0f;
        if (!SpeakerClipLineToRoomAir(&air_rect, &ax, &ay, &bx, &by)) {
            continue;
        }
        DrawSpeakerAlphaLine(render,
                             WorldX(render, ax),
                             WorldY(render, ay),
                             WorldX(render, bx),
                             WorldY(render, by),
                             thickness,
                             color,
                             fade * 0.72f);
    }
}

static void DrawSpeakerWaves(const StageRenderState* state, const SpeakerDevice* speaker) {
    float travel = StageWrap01((float)state->render_time_seconds * SPEAKER_WAVE_SPEED / SPEAKER_WAVE_SPACING) * SPEAKER_WAVE_SPACING;
    float source_x = speaker->x + speaker->width * 0.45f;
    float source_y = speaker->y + speaker->height * 0.66f;
    for (int i = 0; i < 6; ++i) {
        float radius = travel + (float)i * SPEAKER_WAVE_SPACING;
        if (radius > SPEAKER_WAVE_RANGE) {
            continue;
        }
        if (radius < SPEAKER_WAVE_START_RADIUS) {
            continue;
        }
        float fade = StageClampF(1.0f - radius / SPEAKER_WAVE_RANGE, 0.0f, 1.0f);
        int thickness = 3 + (int)(fade * 5.0f + 0.5f);
        uint32_t wave_color = (i & 1) ? SPEAKER_BRIGHT : SPEAKER_HIGHLIGHT;
        DrawSpeakerWaveCircle(state, source_x, source_y, (int)(radius + 0.5f), thickness, wave_color, fade * 0.82f);
    }
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
}

void StageRenderDrawDynamic(const StageRenderState* state) {
    if (state->highlight_type_a || state->type_a_bump_visible || state->type_a_setting_feedback_visible) {
        for (int i = 0; i < state->room->type_a_count; ++i) {
            DrawTypeAWall(state, &state->room->type_a_walls[i], 1);
        }
    }
    for (int i = 0; i < state->room->speaker_count; ++i) {
        DrawSpeakerDevice(state, &state->room->speakers[i]);
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
    for (int i = 0; i < state->room->speaker_count; ++i) {
        DrawSpeakerWaves(state, &state->room->speakers[i]);
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
