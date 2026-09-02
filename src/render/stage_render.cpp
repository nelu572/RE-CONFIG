#include "stage_render.h"

#include "box_sprite_data.h"
#include "walker_enemy_render_geometry.h"

#include "camera.h"
#include "exit_sequence.h"
#include "game_config.h"
#include "piston.h"
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
    int w = WorldW(state->render, r->w);
    int h = WorldH(state->render, r->h);
    DrawRect(state->render, x, y, w, h, state->platform_color);
}

static void DrawPlatformBlend(const StageRenderState* state, const RectF* r, float alpha) {
    int x = WorldX(state->render, r->x);
    int y = WorldY(state->render, r->y);
    int w = WorldW(state->render, r->w);
    int h = WorldH(state->render, r->h);
    DrawRectBlend(state->render, x, y, w, h, state->platform_color, alpha);
}
static int StageMaxI(int a, int b) {
    return a > b ? a : b;
}

static int StageMinI(int a, int b) {
    return a < b ? a : b;
}

static int StageWorldPixels(RenderContext* render, int pixels) {
    if (pixels == 0) {
        return 0;
    }
    int scaled = WorldW(render, (float)(pixels < 0 ? -pixels : pixels));
    return pixels < 0 ? -scaled : scaled;
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
    int w = WorldW(state->render, wall->w);
    int h = WorldH(state->render, wall->h);
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
    SpeakerRoomAirRect rect;
    rect.left = room->bounds.x;
    rect.right = room->bounds.x + room->bounds.w;
    rect.top = room->bounds.y;
    rect.bottom = room->bounds.y + room->bounds.h;
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

static void DrawSpeakerAlphaLine(const StageRenderState* state,
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

    RenderContext* render = state->render;
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
    int w = WorldW(state->render, wall->w);
    int h = WorldH(state->render, wall->h);
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
static constexpr float SPEAKER_WAVE_SPACING = 300.0f;
static constexpr float SPEAKER_WAVE_SPEED = 520.0f;
static constexpr float SPEAKER_WAVE_RANGE = 1080.0f;
static constexpr float SPEAKER_WAVE_START_RADIUS = 42.0f;

struct SpeakerAnimation {
    int body_x;
    int body_y;
    int body_w_extra;
    int body_h_extra;
    int big_radius_extra;
    int small_radius_extra;
};

static SpeakerAnimation SpeakerAnimationForTime(double time_seconds, float volume) {
    volume = StageClampF(volume, 0.0f, 1.0f);
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
    anim.body_x = -(int)((kick * 1.0f) * volume + 0.5f);
    anim.body_y = -(int)((rebound * 1.0f) * volume + 0.5f);
    anim.body_w_extra = (int)((primary * 24.0f + rebound * 10.0f + kick * 7.0f) * volume + 0.5f);
    anim.body_h_extra = (int)((primary * 18.0f + rebound * 8.0f + kick * 6.0f) * volume + 0.5f);
    anim.big_radius_extra = (int)((primary * 18.0f + rebound * 8.0f + kick * 6.0f) * volume + 0.5f);
    anim.small_radius_extra = (int)((primary * 10.0f + rebound * 4.0f + kick * 3.0f) * volume + 0.5f);
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
    SpeakerAnimation anim = SpeakerAnimationForTime(state->speaker_time_seconds, state->speaker_volume);
    int base_x = WorldX(render, speaker->x);
    int base_y = WorldY(render, speaker->y);
    int base_w = WorldW(render, speaker->width);
    int base_h = WorldH(render, speaker->height);
    int body_x = StageWorldPixels(render, anim.body_x);
    int body_y = StageWorldPixels(render, anim.body_y);
    int body_w_extra = StageWorldPixels(render, anim.body_w_extra);
    int body_h_extra = StageWorldPixels(render, anim.body_h_extra);
    int room_center_x = WorldX(render, state->room->bounds.x + state->room->bounds.w * 0.5f);
    int speaker_center_x = WorldX(render, speaker->x + speaker->width * 0.5f);
    int mounted_left = speaker->mount == SPEAKER_MOUNT_LEFT ||
                       (speaker->mount == SPEAKER_MOUNT_AUTO && speaker_center_x < room_center_x);
    int x = mounted_left ? base_x - body_x : base_x + body_x - body_w_extra;
    int y = base_y + body_y - body_h_extra / 2;
    int w = base_w + body_w_extra;
    int h = base_h + body_h_extra;
    int side_w = StageMaxI(12, w * 10 / 100);
    int front_w = w - side_w;
    int body_left = x;
    int body_right = x + w;
    int wall_x;
    if (speaker->mount == SPEAKER_MOUNT_AUTO) {
        wall_x = mounted_left ?
            WorldX(render, state->room->bounds.x + 120.0f) :
            WorldX(render, state->room->bounds.x + state->room->bounds.w - 120.0f);
    } else {
        wall_x = mounted_left ?
            WorldX(render, speaker->x - 120.0f) :
            WorldX(render, speaker->x + speaker->width + 120.0f);
    }
    int plate_w = StageMaxI(14, base_w * 9 / 100);
    int plate_x = wall_x - plate_w / 2;
    int plate_y = base_y + base_h * 31 / 100;
    int plate_h = base_h * 58 / 100;
    int upper_support_y = y + h * 36 / 100;
    int lower_support_y = y + h * 67 / 100;
    int support_h = StageMaxI(8, base_h * 5 / 100);

    if (mounted_left) {
        DrawRect(render, plate_x + plate_w / 2, upper_support_y - support_h / 2, body_left - (plate_x + plate_w / 2) + 2, support_h, SPEAKER_BRACKET);
        DrawRect(render, plate_x + plate_w / 2, lower_support_y - support_h / 2, body_left - (plate_x + plate_w / 2) + 2, support_h, SPEAKER_BRACKET);
        DrawThickLine(render,
                      plate_x + plate_w / 2,
                      upper_support_y + support_h / 2,
                      body_left - support_h / 2,
                      lower_support_y - support_h / 2,
                      support_h,
                      SPEAKER_BRACKET);
    } else {
        DrawRect(render, body_right - 2, upper_support_y - support_h / 2, plate_x - body_right + plate_w / 2, support_h, SPEAKER_BRACKET);
        DrawRect(render, body_right - 2, lower_support_y - support_h / 2, plate_x - body_right + plate_w / 2, support_h, SPEAKER_BRACKET);
        DrawThickLine(render,
                      body_right + support_h / 2,
                      lower_support_y - support_h / 2,
                      plate_x + plate_w / 2,
                      upper_support_y + support_h / 2,
                      support_h,
                      SPEAKER_BRACKET);
    }

    DrawRect(render, x, y, w, h, SPEAKER_BODY);
    DrawRect(render, x, y, w, StageMaxI(3, h * 1 / 100), SPEAKER_BODY_LIGHT);
    if (mounted_left) {
        DrawRect(render, body_right - StageMaxI(3, w * 2 / 100), y, StageMaxI(3, w * 2 / 100), h, SPEAKER_BODY_LIGHT);
        DrawRect(render, x, y, side_w, h, SPEAKER_DARK);
    } else {
        DrawRect(render, x, y, StageMaxI(3, w * 2 / 100), h, SPEAKER_BODY_LIGHT);
        DrawRect(render, body_right - side_w, y, side_w, h, SPEAKER_DARK);
    }
    DrawRectOutline(render, x, y, w, h, SPEAKER_DEEP_DARK);

    int slot_x = mounted_left ? x + w * 65 / 100 : x + w * 12 / 100;
    int slot_y = y + h * 14 / 100;
    int slot_w = w * 23 / 100;
    int slot_h = StageMaxI(7, h * 3 / 100);
    int slot_gap = StageMaxI(15, h * 6 / 100);
    for (int i = 0; i < 4; ++i) {
        DrawSpeakerSlot(render, slot_x, slot_y + i * slot_gap, slot_w, slot_h, SPEAKER_DARK);
    }

    int small_cx = mounted_left ? x + w * 36 / 100 : x + w * 64 / 100;
    int small_cy = y + h * 23 / 100;
    int big_cx = mounted_left ? x + side_w + front_w / 2 : x + front_w / 2;
    int big_cy = y + h * 66 / 100;
    DrawSmallSpeakerDriver(render, small_cx, small_cy, h * 11 / 100 + anim.small_radius_extra, SPEAKER_HIGHLIGHT, SPEAKER_DARK, SPEAKER_BRIGHT, SPEAKER_DEEP_DARK);
    DrawSpeakerDriver(render, big_cx, big_cy, h * 22 / 100 + anim.big_radius_extra, SPEAKER_HIGHLIGHT, SPEAKER_DARK, SPEAKER_BRIGHT, SPEAKER_DEEP_DARK);

    int screw_r = StageMaxI(5, w * 3 / 100);
    int screw_inset = StageMaxI(20, w * 12 / 100);
    if (mounted_left) {
        FillCircle(render, body_right - screw_inset, y + screw_inset, screw_r, SPEAKER_DARK);
        FillCircle(render, x + side_w + screw_inset, y + screw_inset, screw_r, SPEAKER_DARK);
        FillCircle(render, body_right - screw_inset, y + h - screw_inset, screw_r, SPEAKER_DARK);
        FillCircle(render, x + side_w + screw_inset, y + h - screw_inset, screw_r, SPEAKER_DARK);
    } else {
        FillCircle(render, x + screw_inset, y + screw_inset, screw_r, SPEAKER_DARK);
        FillCircle(render, x + front_w - screw_inset, y + screw_inset, screw_r, SPEAKER_DARK);
        FillCircle(render, x + screw_inset, y + h - screw_inset, screw_r, SPEAKER_DARK);
        FillCircle(render, x + front_w - screw_inset, y + h - screw_inset, screw_r, SPEAKER_DARK);
    }

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
        DrawSpeakerAlphaLine(state,
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
    RenderContext* render = state->render;
    float volume = StageClampF(state->speaker_volume, 0.0f, 1.0f);
    if (volume <= 0.001f) {
        return;
    }

    float travel = StageWrap01((float)state->speaker_time_seconds * SPEAKER_WAVE_SPEED / SPEAKER_WAVE_SPACING) * SPEAKER_WAVE_SPACING;
    float source_x = speaker->x + speaker->width * 0.45f;
    float source_y = speaker->y + speaker->height * 0.66f;
    for (int i = 0; i < 4; ++i) {
        float radius = travel + (float)i * SPEAKER_WAVE_SPACING;
        if (radius > SPEAKER_WAVE_RANGE) {
            continue;
        }
        if (radius < SPEAKER_WAVE_START_RADIUS) {
            continue;
        }
        float fade = StageClampF(1.0f - radius / SPEAKER_WAVE_RANGE, 0.0f, 1.0f);
        int thickness = WorldW(render, (float)(1 + (int)((1.0f + fade * 2.0f) * volume + 0.5f)));
        uint32_t wave_color = (i & 1) ? SPEAKER_BRIGHT : SPEAKER_HIGHLIGHT;
        DrawSpeakerWaveCircle(state, source_x, source_y, (int)(radius + 0.5f), thickness, wave_color, fade * 0.82f * volume);
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

static int PistonDetailSize(int pixels, float geometry_scale) {
    return StageMaxI(1, (int)((float)pixels * geometry_scale + 0.5f));
}

static void DrawPistonDevice(const StageRenderState* state, const PistonDevice* piston, int piston_index) {
    RenderContext* render = state->render;
    float extension = state->piston_effective_extension ? state->piston_effective_extension[piston_index] : PistonPoseAt(piston, state->piston_time_seconds).extension;
    RectF body = PistonBodyRect(piston);
    RectF shaft = PistonShaftRectForExtension(piston, extension);
    RectF plate = PistonPlateRectForExtension(piston, extension);

    int bx = WorldX(render, body.x);
    int by = WorldY(render, body.y);
    int bw = WorldW(render, body.w);
    int bh = WorldH(render, body.h);
    int sx = WorldX(render, shaft.x);
    int sy = WorldY(render, shaft.y);
    int sw = WorldW(render, shaft.w);
    int sh = WorldH(render, shaft.h);
    int px = WorldX(render, plate.x);
    int py = WorldY(render, plate.y);
    int pw = WorldW(render, plate.w);
    int ph = WorldH(render, plate.h);

    uint32_t body_color = StageLerpColor(state->platform_color, state->effect_color, 0.24f);
    uint32_t dark_color = StageLerpColor(state->bg_color, state->platform_color, 0.58f);
    uint32_t light_color = StageLerpColor(state->platform_color, state->text_dim_color, 0.22f);
    uint32_t plate_color = StageLerpColor(state->platform_color, state->text_color, 0.10f);

    int horizontal_piston = piston->direction == PISTON_LEFT || piston->direction == PISTON_RIGHT;
    float height_scale = horizontal_piston ? PistonConnectionScale(piston) : 1.0f;
    int transverse_inset_y = PistonDetailSize(10, height_scale);
    int shaft_highlight_h = PistonDetailSize(4, height_scale);

    DrawRect(render, bx, by, bw, bh, body_color);
    if (horizontal_piston) {
        DrawRect(render, bx, by, 7, bh, dark_color);
        DrawRect(render, bx + 8, by + transverse_inset_y, 5, bh - transverse_inset_y * 2, light_color);
    } else {
        DrawRect(render, bx, by + bh - 7, bw, 7, dark_color);
        DrawRect(render, bx + 8, by + 8, bw - 16, 5, light_color);
    }
    DrawRectOutline(render, bx, by, bw, bh, dark_color);

    if (sh > 0) {
        DrawRect(render, sx, sy, sw, sh, dark_color);
        if (horizontal_piston) {
            DrawRect(render, sx, sy + (sh - shaft_highlight_h) / 2, sw, shaft_highlight_h, body_color);
        } else {
            DrawRect(render, sx + sw / 2 - 2, sy, 4, sh, body_color);
        }
    }

    DrawRect(render, px, py, pw, ph, plate_color);
    if (piston->direction == PISTON_LEFT) {
        DrawRect(render, px, py, 9, ph, dark_color);
        DrawRect(render, px + 8, py + transverse_inset_y, 4, ph - transverse_inset_y * 2, light_color);
    } else if (piston->direction == PISTON_RIGHT) {
        DrawRect(render, px + pw - 9, py, 9, ph, dark_color);
        DrawRect(render, px + pw - 12, py + transverse_inset_y, 4, ph - transverse_inset_y * 2, light_color);
    } else if (piston->direction == PISTON_UP) {
        DrawRect(render, px, py, pw, 9, dark_color);
        DrawRect(render, px + 10, py + 8, pw - 20, 4, light_color);
    } else {
        DrawRect(render, px, py + ph - 9, pw, 9, dark_color);
        DrawRect(render, px + 10, py + 8, pw - 20, 4, light_color);
    }
    DrawRectOutline(render, px, py, pw, ph, dark_color);
}

static void DrawRoomPistons(const StageRenderState* state) {
    for (int i = 0; i < state->room->piston_count; ++i) {
        DrawPistonDevice(state, &state->room->pistons[i], i);
    }
}

static void DrawPressureSwitchDevice(const StageRenderState* state, const PressureSwitchDevice* sw, int pressed, float anim) {
    RenderContext* render = state->render;
    int x = WorldX(render, sw->rect.x);
    int y = WorldY(render, sw->rect.y);
    int w = WorldW(render, sw->rect.w);
    int h = WorldH(render, sw->rect.h);
    uint32_t mount_color = StageLerpColor(state->text_color, state->platform_color, 0.16f);
    uint32_t stem_color = StageLerpColor(state->text_color, state->platform_color, 0.28f);
    uint32_t press = pressed ? 0xFF3030u : 0xE1192Du;
    uint32_t press_shadow = 0x8A101Au;
    PressureSwitchMount mount = PressureSwitchMountFor(state->room, sw);

    if (mount == PRESSURE_SWITCH_MOUNT_DOWN || mount == PRESSURE_SWITCH_MOUNT_UP) {
        int travel = (int)(anim * 7.0f + 0.5f);
        int mount_h = StageMaxI(8, h / 4);
        int cap_h = StageMaxI(14, h * 7 / 16);
        int cap_w = w;
        int cap_x = x;
        int cap_y = mount == PRESSURE_SWITCH_MOUNT_DOWN ? y + travel : y + h - cap_h - travel;
        int stem_w = StageMaxI(8, w / 8);
        int stem_x = x + (w - stem_w) / 2;
        int mount_y = mount == PRESSURE_SWITCH_MOUNT_DOWN ? y + h - mount_h : y;
        int stem_y = mount == PRESSURE_SWITCH_MOUNT_DOWN ? cap_y + cap_h - 3 : mount_y + mount_h - 3;
        int stem_h = mount == PRESSURE_SWITCH_MOUNT_DOWN ?
            StageMaxI(5, mount_y - stem_y + 3) :
            StageMaxI(5, cap_y - stem_y + 3);

        DrawRect(render, x, mount_y, w, mount_h, mount_color);
        DrawRect(render, stem_x, stem_y, stem_w, stem_h, stem_color);
        DrawRect(render, cap_x, cap_y, cap_w, cap_h, press_shadow);
        DrawRect(render, cap_x + 2, cap_y + 2, StageMaxI(2, cap_w - 4), StageMaxI(2, cap_h - 4), press);
    } else {
        int max_travel = StageMaxI(4, StageMinI(10, w / 5));
        int cap_w = StageMaxI(10, w * 3 / 10);
        int side_travel = (int)(anim * (float)max_travel + 0.5f);
        int cap_x = mount == PRESSURE_SWITCH_MOUNT_RIGHT ? x + 2 + side_travel : x + w - 2 - side_travel - cap_w;
        int cap_y = y + 8;
        int cap_h = StageMaxI(10, h - 16);
        int mount_w = StageMaxI(7, w / 5);
        int mount_x = mount == PRESSURE_SWITCH_MOUNT_RIGHT ?
            x + w - mount_w :
            x;
        int connector_x = mount == PRESSURE_SWITCH_MOUNT_RIGHT ? cap_x + cap_w : mount_x + mount_w;
        int connector_w = mount == PRESSURE_SWITCH_MOUNT_RIGHT ? mount_x - connector_x : cap_x - connector_x;
        int connector_h = StageMaxI(5, h / 12);
        int connector_y = y + h / 2 - connector_h / 2;
        int mount_y = y + 5;
        int mount_h = StageMaxI(12, h - 10);

        DrawRect(render, mount_x, mount_y, mount_w, mount_h, mount_color);
        if (connector_w > 0) {
            DrawRect(render, connector_x, connector_y, connector_w, connector_h, stem_color);
        }
        DrawRect(render, cap_x, cap_y, cap_w, cap_h, press_shadow);
        DrawRect(render, cap_x + 2, cap_y + 3, StageMaxI(2, cap_w - 4), StageMaxI(2, cap_h - 6), press);
    }
}

static void DrawRoomPressureSwitches(const StageRenderState* state) {
    int switch_count = state->room->pressure_switch_count;
    for (int i = 0; i < switch_count; ++i) {
        int pressed = state->pressure_switch_pressed ? state->pressure_switch_pressed[i] : 0;
        float anim = state->pressure_switch_anim ? state->pressure_switch_anim[i] : 0.0f;
        DrawPressureSwitchDevice(state, &state->room->pressure_switches[i], pressed, anim);
    }
}

static void DrawPressurePlatformDevice(const StageRenderState* state, const PressurePlatformDevice* platform, float open_amount, float alpha) {
    open_amount = StageClampF(open_amount, 0.0f, 1.0f);
    RectF current = PressurePlatformRectAt(platform, open_amount);

    RectF pieces[16];
    RectF next[16];
    int piece_count = 1;
    pieces[0] = current;
    for (int platform_index = 0; platform_index < state->room->platform_count; ++platform_index) {
        const RectF* solid = &state->room->platforms[platform_index];
        int next_count = 0;
        for (int piece_index = 0; piece_index < piece_count && next_count < 16; ++piece_index) {
            RectF piece = pieces[piece_index];
            float ix0 = piece.x > solid->x ? piece.x : solid->x;
            float iy0 = piece.y > solid->y ? piece.y : solid->y;
            float ix1 = piece.x + piece.w < solid->x + solid->w ? piece.x + piece.w : solid->x + solid->w;
            float iy1 = piece.y + piece.h < solid->y + solid->h ? piece.y + piece.h : solid->y + solid->h;
            if (ix1 <= ix0 || iy1 <= iy0) {
                next[next_count++] = piece;
                continue;
            }
            if (iy0 > piece.y && next_count < 16) {
                next[next_count++] = { piece.x, piece.y, piece.w, iy0 - piece.y };
            }
            if (iy1 < piece.y + piece.h && next_count < 16) {
                next[next_count++] = { piece.x, iy1, piece.w, piece.y + piece.h - iy1 };
            }
            if (ix0 > piece.x && next_count < 16) {
                next[next_count++] = { piece.x, iy0, ix0 - piece.x, iy1 - iy0 };
            }
            if (ix1 < piece.x + piece.w && next_count < 16) {
                next[next_count++] = { ix1, iy0, piece.x + piece.w - ix1, iy1 - iy0 };
            }
        }
        for (int i = 0; i < next_count; ++i) {
            pieces[i] = next[i];
        }
        piece_count = next_count;
    }
    for (int i = 0; i < piece_count; ++i) {
        if (alpha >= 1.0f) {
            DrawPlatform(state, &pieces[i]);
        } else {
            DrawPlatformBlend(state, &pieces[i], alpha);
        }
    }
}

static void DrawRoomPressurePlatforms(const StageRenderState* state) {
    int platform_count = state->room->pressure_platform_count;
    for (int i = 0; i < platform_count; ++i) {
        const PressurePlatformDevice* platform = &state->room->pressure_platforms[i];
        float open_amount = state->pressure_platform_open_amount ? state->pressure_platform_open_amount[i] : (state->room_exit_unlocked ? 1.0f : 0.0f);
        float alpha = platform->disappears_when_open ? 1.0f - StageClampF(open_amount, 0.0f, 1.0f) : 1.0f;
        if (alpha <= 0.0f) {
            continue;
        }
        DrawPressurePlatformDevice(state, platform, open_amount, alpha);
    }
}

static TrailVertex CheckpointFlagVertex(int x, int y, uint32_t color) {
    TrailVertex vertex;
    vertex.x = (float)x;
    vertex.y = (float)y;
    vertex.r = (float)((color >> 16) & 255) / 255.0f;
    vertex.g = (float)((color >> 8) & 255) / 255.0f;
    vertex.b = (float)(color & 255) / 255.0f;
    vertex.a = 1.0f;
    return vertex;
}

static void DrawRoomCheckpoint(const StageRenderState* state) {
    const RectF* checkpoint = &state->room->checkpoint;
    if (checkpoint->w <= 0.0f || checkpoint->h <= 0.0f) {
        return;
    }

    int x = WorldX(state->render, checkpoint->x);
    int y = WorldY(state->render, checkpoint->y);
    int w = WorldW(state->render, checkpoint->w);
    int h = WorldH(state->render, checkpoint->h);
    int pole_w = StageMaxI(3, w / 12);
    int pole_x = x + w / 5;
    int pole_top = y + 2;
    int pole_bottom = y + h - 3;
    uint32_t pole_color = state->text_color;

    DrawRect(state->render, pole_x, pole_top, pole_w, pole_bottom - pole_top, pole_color);
    DrawRect(state->render, pole_x - 4, pole_bottom - 2, pole_w + 8, 3, pole_color);

    float drop = state->checkpoint_active ? StageClampF(state->checkpoint_flag_drop, 0.0f, 1.0f) : 0.0f;
    drop = drop * drop * (3.0f - 2.0f * drop);
    int flag_w = StageMaxI(16, w * 11 / 20);
    int flag_h = StageMaxI(10, h * 3 / 10);
    int flag_travel = StageMaxI(0, h - flag_h - 9);
    int flag_x = pole_x + pole_w;
    int flag_y = pole_top + 2 + (int)(drop * (float)flag_travel + 0.5f);
    TrailVertex top = CheckpointFlagVertex(flag_x, flag_y, state->platform_color);
    TrailVertex tip = CheckpointFlagVertex(flag_x + flag_w, flag_y + flag_h / 2, state->platform_color);
    TrailVertex bottom = CheckpointFlagVertex(flag_x, flag_y + flag_h, state->platform_color);
    DrawAlphaTriangle(state->render, &top, &tip, &bottom);
}

static int BoxSpriteSourceCornerPixel(int sx, int sy) {
    const int corner_size = 2;
    int near_left = sx < corner_size;
    int near_right = sx >= BOX_SPRITE_WIDTH - corner_size;
    int near_top = sy < corner_size;
    int near_bottom = sy >= BOX_SPRITE_HEIGHT - corner_size;
    return (near_left || near_right) && (near_top || near_bottom);
}

static void DrawGravityBox(const StageRenderState* state, const RectF* box) {
    int x = WorldX(state->render, box->x);
    int y = WorldY(state->render, box->y);
    int w = WorldW(state->render, box->w);
    int h = WorldH(state->render, box->h);

    for (int i = 0; i < BOX_SPRITE_RECT_COUNT; ++i) {
        const BoxSpriteRect* rect = &BOX_SPRITE_RECTS[i];
        uint32_t pixel = rect->rgba;
        if ((pixel & 255u) == 0) {
            continue;
        }
        uint32_t color = (uint32_t)((((pixel >> 24) & 255u) << 16) |
                                    (((pixel >> 16) & 255u) << 8) |
                                    ((pixel >> 8) & 255u));
        for (int sy = (int)rect->y; sy < (int)rect->y + (int)rect->h; ++sy) {
            for (int sx = (int)rect->x; sx < (int)rect->x + (int)rect->w; ++sx) {
                if (BoxSpriteSourceCornerPixel(sx, sy)) {
                    continue;
                }
                int rx = x + sx * w / BOX_SPRITE_WIDTH;
                int ry = y + sy * h / BOX_SPRITE_HEIGHT;
                int rw = (sx + 1) * w / BOX_SPRITE_WIDTH - sx * w / BOX_SPRITE_WIDTH;
                int rh = (sy + 1) * h / BOX_SPRITE_HEIGHT - sy * h / BOX_SPRITE_HEIGHT;
                DrawObjectClippedRect(state, x, y, w, h, rx, ry, rw, rh, color);
            }
        }
    }
}

static void DrawRoomGravityBoxes(const StageRenderState* state) {
    for (int i = 0; i < state->gravity_box_count; ++i) {
        DrawGravityBox(state, &state->gravity_boxes[i]);
    }
}
static float WalkerTriangleEdge(float ax, float ay, float bx, float by, float px, float py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static void DrawWalkerSolidTriangle(RenderContext* render,
                                    float ax,
                                    float ay,
                                    float bx,
                                    float by,
                                    float cx,
                                    float cy,
                                    uint32_t color) {
    float scale = (float)render->scale;
    ax *= scale;
    ay *= scale;
    bx *= scale;
    by *= scale;
    cx *= scale;
    cy *= scale;
    float min_x = ax < bx ? ax : bx;
    float max_x = ax > bx ? ax : bx;
    float min_y = ay < by ? ay : by;
    float max_y = ay > by ? ay : by;
    if (cx < min_x) min_x = cx;
    if (cx > max_x) max_x = cx;
    if (cy < min_y) min_y = cy;
    if (cy > max_y) max_y = cy;
    int x0 = (int)min_x - 2;
    int x1 = (int)max_x + 3;
    int y0 = (int)min_y - 2;
    int y1 = (int)max_y + 3;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > render->width) x1 = render->width;
    if (y1 > render->height) y1 = render->height;
    float area = WalkerTriangleEdge(ax, ay, bx, by, cx, cy);
    if (area > -0.001f && area < 0.001f) {
        return;
    }
    float sign = area < 0.0f ? -1.0f : 1.0f;
    for (int y = y0; y < y1; ++y) {
        float py = (float)y + 0.5f;
        for (int x = x0; x < x1; ++x) {
            float px = (float)x + 0.5f;
            float e0 = WalkerTriangleEdge(bx, by, cx, cy, px, py) * sign;
            float e1 = WalkerTriangleEdge(cx, cy, ax, ay, px, py) * sign;
            float e2 = WalkerTriangleEdge(ax, ay, bx, by, px, py) * sign;
            if (e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f) {
                render->pixels[y * render->width + x] = color;
            }
        }
    }
}

static void DrawWalkerSpike(const StageRenderState* state,
                            const WalkerEnemySpikeGeometry* spike,
                            uint32_t color) {
    DrawWalkerSolidTriangle(state->render,
                            spike->left_x,
                            spike->left_y,
                            spike->right_x,
                            spike->right_y,
                            spike->tip_x,
                            spike->tip_y,
                            color);
    if (spike->is_side_wedge) {
        // Fill the clipped lower half so the wedge has a horizontal floor-contact edge.
        DrawWalkerSolidTriangle(state->render,
                                spike->right_x,
                                spike->right_y,
                                spike->cut_x,
                                spike->cut_y,
                                spike->tip_x,
                                spike->tip_y,
                                color);
    }
}

static void DrawWalkerRoundedTop(const StageRenderState* state,
                                 int center_x,
                                 int top_y,
                                 int radius_x,
                                 int radius_y,
                                 uint32_t color) {
    for (int row = 0; row <= radius_y; ++row) {
        int vertical = radius_y - row;
        int inside = radius_y * radius_y - vertical * vertical;
        int extent = 0;
        while ((extent + 1) * (extent + 1) * radius_y * radius_y <= radius_x * radius_x * inside) {
            ++extent;
        }
        DrawRect(state->render, center_x - extent, top_y + row, extent * 2 + 1, 1, color);
    }
}

static void DrawWalkerEnemy(const StageRenderState* state,
                            const RectF* enemy,
                            float spike_amount,
                            float response_squash,
                            float turn_squash) {
    WalkerEnemyRenderGeometry geometry;
    WalkerEnemyBuildRenderGeometry(state->render,
                                   enemy,
                                   spike_amount,
                                   response_squash,
                                   turn_squash,
                                   &geometry);
    uint32_t body = 0x00f22632;
    uint32_t spike_color = 0x00ed2b3a;

    // Spikes first: their roots are then buried by the single rounded body, with no extra crest.
    for (int i = 0; i < geometry.spike_count; ++i) {
        DrawWalkerSpike(state, &geometry.spikes[i], spike_color);
    }
    DrawWalkerRoundedTop(state,
                         geometry.center_x,
                         geometry.top_y,
                         geometry.radius_x,
                         geometry.radius_y,
                         body);
    int base_y = geometry.top_y + geometry.radius_y;
    DrawRect(state->render,
             geometry.body_x,
             base_y,
             geometry.body_w,
             geometry.body_y + geometry.body_h - base_y,
             body);

}
static void DrawWalkerEnemies(const StageRenderState* state) {
    for (int i = 0; i < state->walker_enemy_count; ++i) {
        float spike = state->walker_enemy_spike_amount ? state->walker_enemy_spike_amount[i] : 0.0f;
        float response_squash = state->walker_enemy_squash_amount ? state->walker_enemy_squash_amount[i] : 0.0f;
        float turn_squash = state->walker_enemy_turn_squash ? state->walker_enemy_turn_squash[i] : 0.0f;
        DrawWalkerEnemy(state, &state->walker_enemies[i], spike, response_squash, turn_squash);
    }
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
    // Waves are drawn first, then redrawn static solids naturally mask them.
    for (int i = 0; i < state->room->speaker_count; ++i) {
        DrawSpeakerWaves(state, &state->room->speakers[i]);
    }
    StageRenderDrawStatic(state);
    if (state->highlight_type_a || state->type_a_bump_visible || state->type_a_setting_feedback_visible) {
        for (int i = 0; i < state->room->type_a_count; ++i) {
            DrawTypeAWall(state, &state->room->type_a_walls[i], 1);
        }
    }
    for (int i = 0; i < state->room->speaker_count; ++i) {
        DrawSpeakerDevice(state, &state->room->speakers[i]);
    }
    DrawRoomCheckpoint(state);
    DrawRoomPistons(state);
    DrawRoomPressureSwitches(state);
    DrawRoomPressurePlatforms(state);
    DrawRoomGravityBoxes(state);
    DrawWalkerEnemies(state);
    ExitSequenceDrawExit(state->render, &state->room->exit);
    if (state->player_visible) {
        DrawPlayer(state->render,
                   state->player,
                   state->player_color,
                   StageLerpColor(state->bg_color, state->platform_color, 0.78f),
                   state->gravity_direction);
    }
    DrawPlayerParticles(state->render,
                        state->player_particles,
                        state->player_particle_count,
                        StageLerpColor(state->platform_color, state->type_a_off_pattern_color, 0.48f));
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
