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
    int x = (int)(r->x + 0.5f);
    int y = (int)(r->y + 0.5f);
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

static void DrawTypeABrickTilePattern(const StageRenderState* state, int object_x, int object_y, int object_w, int object_h, int tile_x, int tile_y, uint32_t pattern_color) {
    uint32_t block = pattern_color;
    DrawObjectClippedRect(state, object_x, object_y, object_w, object_h, tile_x + 2, tile_y + 3, 36, 9, block);
    DrawObjectClippedRect(state, object_x, object_y, object_w, object_h, tile_x, tile_y + 16, 18, 9, block);
    DrawObjectClippedRect(state, object_x, object_y, object_w, object_h, tile_x + 22, tile_y + 16, 18, 9, block);
    DrawObjectClippedRect(state, object_x, object_y, object_w, object_h, tile_x + 2, tile_y + 29, 36, 8, block);
}

static void DrawTypeATilePattern(const StageRenderState* state, int x, int y, int w, int h, uint32_t pattern_color) {
    const int tile_size = 40;
    for (int local_y = 0; local_y < h; local_y += tile_size) {
        for (int local_x = 0; local_x < w; local_x += tile_size) {
            int tile_x = x + local_x;
            int tile_y = y + local_y;
            DrawTypeABrickTilePattern(state, x, y, w, h, tile_x, tile_y, pattern_color);
        }
    }
}

static void DrawTypeAWall(const StageRenderState* state, const RectF* wall, int highlight);

static void DrawBrickRect(const StageRenderState* state, const RectF* wall, uint32_t base_color, uint32_t pattern_color) {
    int x = (int)(wall->x + 0.5f);
    int y = (int)(wall->y + 0.5f);
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

static void DrawObjectClippedOutlinePathPoint(const StageRenderState* state,
                                              int object_x,
                                              int object_y,
                                              int object_w,
                                              int object_h,
                                              int x,
                                              int y,
                                              int w,
                                              int h,
                                              int path_pos,
                                              uint32_t color) {
    int px = x;
    int py = y;
    int top_len = w;
    int right_len = h - 1;
    int bottom_len = w - 1;
    int left_len = h - 2;
    if (path_pos < top_len) {
        px = x + path_pos;
        py = y;
    } else if ((path_pos -= top_len) < right_len) {
        px = x + w - 1;
        py = y + 1 + path_pos;
    } else if ((path_pos -= right_len) < bottom_len) {
        px = x + w - 2 - path_pos;
        py = y + h - 1;
    } else {
        path_pos -= bottom_len;
        px = x;
        py = y + h - 2 - path_pos;
    }
    DrawObjectClippedRect(state,
                          object_x,
                          object_y,
                          object_w,
                          object_h,
                          px,
                          py,
                          state->type_a_off_line_thickness,
                          state->type_a_off_line_thickness,
                          color);
}

static void DrawObjectClippedAnimatedOutlineSegment(const StageRenderState* state,
                                                    int object_x,
                                                    int object_y,
                                                    int object_w,
                                                    int object_h,
                                                    int x,
                                                    int y,
                                                    int w,
                                                    int h,
                                                    int tile_col,
                                                    int tile_row,
                                                    int brick_index,
                                                    uint32_t color) {
    int perimeter = w + (h - 1) + (w - 1) + (h - 2);
    if (perimeter <= 0) {
        return;
    }

    const float cycle_seconds = 1.5f;
    float offset = (float)((tile_col * 5 + tile_row * 9 + brick_index * 7) % 19) / 19.0f;
    float phase = StageWrap01((float)(state->render_time_seconds / cycle_seconds) + offset);
    int start = (int)(phase * (float)perimeter);
    for (int i = 0; i < state->type_a_off_visible_path_len; ++i) {
        int path_pos = start + i;
        while (path_pos >= perimeter) {
            path_pos -= perimeter;
        }
        DrawObjectClippedOutlinePathPoint(state, object_x, object_y, object_w, object_h, x, y, w, h, path_pos, color);
    }
}

static void DrawTypeABrickTileAnimatedTrace(const StageRenderState* state,
                                            int object_x,
                                            int object_y,
                                            int object_w,
                                            int object_h,
                                            int tile_x,
                                            int tile_y,
                                            int tile_col,
                                            int tile_row,
                                            uint32_t color) {
    DrawObjectClippedAnimatedOutlineSegment(state, object_x, object_y, object_w, object_h, tile_x + 2, tile_y + 3, 36, 9, tile_col, tile_row, 0, color);
    DrawObjectClippedAnimatedOutlineSegment(state, object_x, object_y, object_w, object_h, tile_x, tile_y + 16, 18, 9, tile_col, tile_row, 1, color);
    DrawObjectClippedAnimatedOutlineSegment(state, object_x, object_y, object_w, object_h, tile_x + 22, tile_y + 16, 18, 9, tile_col, tile_row, 2, color);
    DrawObjectClippedAnimatedOutlineSegment(state, object_x, object_y, object_w, object_h, tile_x + 2, tile_y + 29, 36, 8, tile_col, tile_row, 3, color);
}

static void DrawTypeAOffAnimatedTrace(const StageRenderState* state, const RectF* wall, uint32_t pattern_color) {
    int x = (int)(wall->x + 0.5f);
    int y = (int)(wall->y + 0.5f);
    int w = (int)(wall->w + 0.5f);
    int h = (int)(wall->h + 0.5f);
    const int tile_size = 40;
    for (int local_y = 0; local_y < h; local_y += tile_size) {
        for (int local_x = 0; local_x < w; local_x += tile_size) {
            DrawTypeABrickTileAnimatedTrace(state,
                                            x,
                                            y,
                                            w,
                                            h,
                                            x + local_x,
                                            y + local_y,
                                            local_x / tile_size,
                                            local_y / tile_size,
                                            pattern_color);
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
    TutorialUiDrawWorldHint(state->render, state->room);
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
    ExitSequenceDrawExit(state->render, &state->room->exit);
    DrawPlayerParticles(state->render, state->player_particles, state->player_particle_count, state->effect_color);
    DrawPlayer(state->render,
               state->player,
               state->player_color,
               StageLerpColor(state->bg_color, state->platform_color, 0.78f));

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
