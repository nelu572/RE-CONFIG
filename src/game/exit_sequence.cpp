#include "exit_sequence.h"

#include "game_config.h"

static constexpr float EXIT_VISUAL_W = 74.0f;
static constexpr float EXIT_VISUAL_H = 100.0f;
static constexpr float EXIT_OPEN_DISTANCE = 120.0f;
static constexpr float EXIT_DOOR_SPRING_STIFFNESS = 360.0f;
static constexpr float EXIT_DOOR_SPRING_DAMPING = 26.0f;
static constexpr float EXIT_TRANSITION_HOLD_SECONDS = 0.09f;

static float g_exit_open_amount = 0.0f;
static float g_exit_open_velocity = 0.0f;
static int g_room_solved = 0;
static float g_room_transition_fade = 0.0f;
static int g_room_transition_pending = -1;
static float g_room_transition_hold_seconds = 0.0f;
static uint32_t g_exit_color = 0x00f04a5b;
static uint32_t g_exit_door_color = 0x00a83540;
static uint32_t g_exit_soft_color = 0x006f3038;

static float ExitClampF(float value, float lo, float hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static float ExitAbsF(float value) {
    return value < 0.0f ? -value : value;
}

static float ExitApproachF(float value, float target, float step) {
    if (value < target) {
        value += step;
        if (value > target) value = target;
    } else if (value > target) {
        value -= step;
        if (value < target) value = target;
    }
    return value;
}

static float ExitSmooth01(float value) {
    value = ExitClampF(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}

static int ExitFloorSqrtI(int value) {
    if (value <= 0) {
        return 0;
    }

    int result = 0;
    int bit = 1 << 30;
    while (bit > value) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return result;
}

static uint32_t ExitFadeColor(uint32_t color, float fade) {
    fade = ExitClampF(fade, 0.0f, 1.0f);
    int r = (int)((float)((color >> 16) & 255) * fade);
    int g = (int)((float)((color >> 8) & 255) * fade);
    int b = (int)((float)(color & 255) * fade);
    return (uint32_t)((r << 16) | (g << 8) | b);
}

static float ExitSpringF(float value, float* velocity, float target, float dt, float stiffness, float damping) {
    float accel = (target - value) * stiffness - *velocity * damping;
    *velocity += accel * dt;
    value += *velocity * dt;
    if (ExitAbsF(target - value) < 0.0005f && ExitAbsF(*velocity) < 0.0005f) {
        value = target;
        *velocity = 0.0f;
    }
    return value;
}

static RectI ExitClampRect(RectI rect) {
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

static int PlayerNearExit(const RectF* player, const RectF* exit_rect) {
    float dx = 0.0f;
    if (player->x + player->w < exit_rect->x) {
        dx = exit_rect->x - (player->x + player->w);
    } else if (exit_rect->x + exit_rect->w < player->x) {
        dx = player->x - (exit_rect->x + exit_rect->w);
    }

    float dy = 0.0f;
    if (player->y + player->h < exit_rect->y) {
        dy = exit_rect->y - (player->y + player->h);
    } else if (exit_rect->y + exit_rect->h < player->y) {
        dy = player->y - (exit_rect->y + exit_rect->h);
    }

    return dx * dx + dy * dy <= EXIT_OPEN_DISTANCE * EXIT_OPEN_DISTANCE;
}

static int CircleSpanAtY(int x, int w, int center_y, int radius, int yy, int* left, int* right) {
    int center_x2 = x * 2 + w;
    int center_y2 = center_y * 2;
    int radius2 = radius * 2;
    int limit = radius2 * radius2;
    int py2 = yy * 2 + 1;
    int dy = py2 - center_y2;

    *left = x;
    *right = x + w;
    while (*left < *right) {
        int px2 = *left * 2 + 1;
        int dx = px2 - center_x2;
        if (dx * dx + dy * dy <= limit) {
            break;
        }
        ++(*left);
    }
    if (*left >= *right) {
        return 0;
    }
    while (*right > *left) {
        int px2 = (*right - 1) * 2 + 1;
        int dx = px2 - center_x2;
        if (dx * dx + dy * dy <= limit) {
            break;
        }
        --(*right);
    }
    return *right > *left;
}

static void DrawHardCircleOutside(RenderContext* render, int cx, int cy, int radius, uint32_t color) {
    if (radius <= 0) {
        DrawRect(render, 0, 0, FB_W, FB_H, color);
        return;
    }

    int y0 = cy - radius;
    int y1 = cy + radius;
    if (y0 > 0) {
        DrawRect(render, 0, 0, FB_W, y0, color);
    }
    if (y1 < FB_H - 1) {
        DrawRect(render, 0, y1 + 1, FB_W, FB_H - y1 - 1, color);
    }
    if (y0 < 0) y0 = 0;
    if (y1 >= FB_H) y1 = FB_H - 1;

    int radius_sq = radius * radius;
    for (int y = y0; y <= y1; ++y) {
        int dy = y - cy;
        int half_w = ExitFloorSqrtI(radius_sq - dy * dy);
        int left = cx - half_w;
        int right = cx + half_w;
        if (left > 0) {
            DrawRect(render, 0, y, left, 1, color);
        }
        if (right < FB_W - 1) {
            int x = right + 1;
            if (x < 0) x = 0;
            DrawRect(render, x, y, FB_W - x, 1, color);
        }
    }
}

static int ExitTransitionCoverRadius(int cx, int cy) {
    int left = cx;
    int right = FB_W - 1 - cx;
    int top = cy;
    int bottom = FB_H - 1 - cy;
    int dx = left > right ? left : right;
    int dy = top > bottom ? top : bottom;
    return ExitFloorSqrtI(dx * dx + dy * dy) + 2;
}

static void FillExitDoor(RenderContext* render, int x, int y, int w, int h, int thickness, float open_amount) {
    int radius = w / 2;
    int overlap = 2;
    int inner_x = x + thickness - overlap;
    int inner_w = w - thickness * 2 + overlap * 2;
    int inner_radius = inner_w / 2;
    int center_y = y + radius;
    int inner_top = center_y - inner_radius;
    int inner_bottom = y + h;
    int inner_h = inner_bottom - inner_top;
    int door_y = inner_top - (int)(ExitClampF(open_amount, 0.0f, 1.08f) * (float)inner_h + 0.5f);
    int door_bottom = door_y + inner_h;
    if (door_y < inner_top) door_y = inner_top;
    if (door_bottom > inner_bottom) door_bottom = inner_bottom;

    for (int yy = door_y; yy < door_bottom; ++yy) {
        int left = inner_x;
        int right = inner_x + inner_w;
        if (yy < center_y) {
            if (!CircleSpanAtY(inner_x, inner_w, center_y, inner_radius, yy, &left, &right)) {
                continue;
            }
        }
        if (right > left) {
            DrawRect(render, left, yy, right - left, 1, g_exit_door_color);
        }
    }
}

static void DrawExitFrame(RenderContext* render, int x, int y, int w, int h, int thickness) {
    int radius = w / 2;
    int inner_radius = radius - thickness;
    int center_x = x + radius;
    int center_y = y + radius;

    for (int yy = y; yy < y + radius; ++yy) {
        int outer_left = center_x;
        int outer_right = center_x;
        if (!CircleSpanAtY(x, w, center_y, radius, yy, &outer_left, &outer_right)) {
            continue;
        }

        int inner_left = center_x;
        int inner_right = center_x;
        int has_inner = CircleSpanAtY(x + thickness, w - thickness * 2, center_y, inner_radius, yy, &inner_left, &inner_right);
        if (!has_inner) {
            DrawRect(render, outer_left, yy, outer_right - outer_left, 1, g_exit_color);
            continue;
        }

        if (inner_left > outer_left) {
            DrawRect(render, outer_left, yy, inner_left - outer_left, 1, g_exit_color);
        }
        if (outer_right > inner_right) {
            DrawRect(render, inner_right, yy, outer_right - inner_right, 1, g_exit_color);
        }
    }

    DrawRect(render, x, y + radius, thickness, h - radius, g_exit_color);
    DrawRect(render, x + w - thickness, y + radius, thickness, h - radius, g_exit_color);
}

void ExitSequenceSetExitColor(uint32_t exit_color) {
    g_exit_color = exit_color;
    g_exit_door_color = ExitFadeColor(exit_color, 0.70f);
    g_exit_soft_color = ExitFadeColor(exit_color, 0.48f);
}

void ExitSequenceResetStageState() {
    g_room_solved = 0;
    g_exit_open_amount = 0.0f;
    g_exit_open_velocity = 0.0f;
}

void ExitSequenceUpdateDoor(float dt, const RectF* player_rect, const RectF* exit_rect) {
    float target = PlayerNearExit(player_rect, exit_rect) ? 1.0f : 0.0f;
    g_exit_open_amount = ExitSpringF(g_exit_open_amount,
                                     &g_exit_open_velocity,
                                     target,
                                     dt,
                                     EXIT_DOOR_SPRING_STIFFNESS,
                                     EXIT_DOOR_SPRING_DAMPING);
    g_exit_open_amount = ExitClampF(g_exit_open_amount, -0.08f, 1.08f);
}

void ExitSequenceStartTransition(int room) {
    if (room >= 0 && room < RoomCount()) {
        g_room_transition_pending = room;
        g_room_transition_fade = 0.001f;
        g_room_transition_hold_seconds = 0.0f;
    }
}

int ExitSequenceUpdateTransition(float dt, int* current_room, ExitSequenceResetStageCallback reset_stage, void* reset_user_data) {
    if (g_room_transition_pending >= 0) {
        g_room_transition_fade = ExitApproachF(g_room_transition_fade, 1.0f, dt * 3.0f);
        if (g_room_transition_fade >= 0.999f) {
            *current_room = g_room_transition_pending;
            g_room_transition_pending = -1;
            reset_stage(reset_user_data);
            g_room_transition_fade = 1.0f;
            g_room_transition_hold_seconds = EXIT_TRANSITION_HOLD_SECONDS;
        }
        return 1;
    }
    if (g_room_transition_hold_seconds > 0.0f) {
        g_room_transition_hold_seconds -= dt;
        if (g_room_transition_hold_seconds < 0.0f) {
            g_room_transition_hold_seconds = 0.0f;
        }
        return 1;
    }
    if (g_room_transition_fade > 0.0f) {
        g_room_transition_fade = ExitApproachF(g_room_transition_fade, 0.0f, dt * 2.6f);
    }
    return g_room_transition_fade > 0.001f;
}

int ExitSequenceTransitionVisible() {
    return g_room_transition_pending >= 0 ||
           g_room_transition_hold_seconds > 0.001f ||
           g_room_transition_fade > 0.001f;
}

void ExitSequenceSetRoomSolved(int solved) {
    g_room_solved = solved;
}

int ExitSequenceRoomSolved() {
    return g_room_solved;
}

RectF ExitSequenceVisualRect(const RectF* exit_rect) {
    RectF visual;
    visual.x = exit_rect->x + exit_rect->w * 0.5f - EXIT_VISUAL_W * 0.5f;
    visual.y = exit_rect->y + exit_rect->h - EXIT_VISUAL_H;
    visual.w = EXIT_VISUAL_W;
    visual.h = EXIT_VISUAL_H;
    return visual;
}

RectI ExitSequenceDirtyRect(RenderContext* render, const RectF* exit_rect) {
    RectF visual = ExitSequenceVisualRect(exit_rect);
    RectI rect = {
        WorldX(render, visual.x) - 14,
        WorldY(render, visual.y) - 14,
        WorldW(render, visual.w) + 28,
        WorldH(render, visual.h) + 28
    };
    return ExitClampRect(rect);
}

void ExitSequenceDrawExit(RenderContext* render, const RectF* exit_rect) {
    RectF visual = ExitSequenceVisualRect(exit_rect);
    int x = WorldX(render, visual.x);
    int y = WorldY(render, visual.y);
    int w = WorldW(render, visual.w);
    int h = WorldH(render, visual.h);
    int thickness = WorldW(render, 8.0f);

    FillExitDoor(render, x, y, w, h, thickness, g_exit_open_amount);
    DrawExitFrame(render, x, y, w, h, thickness);
}

void ExitSequenceDrawSolvedUi(RenderContext* render, uint32_t text_color, ExitSequenceDrawTextSmallCallback draw_text_small) {
    if (!g_room_solved) {
        return;
    }
    DrawRect(render, 1492, 62, 92, 3, g_exit_color);
    DrawRect(render, 1492, 74, 44, 2, g_exit_soft_color);
    draw_text_small(1602, 52, "CLEAR", 3, text_color);
}

void ExitSequenceDrawTransitionAmount(RenderContext* render, float amount) {
    if (amount <= 0.001f) {
        return;
    }

    float t = ExitSmooth01(amount);
    int center_x = FB_W / 2;
    int center_y = FB_H / 2;
    int cover_radius = ExitTransitionCoverRadius(center_x, center_y);
    int radius = (int)((float)cover_radius * (1.0f - t) + 0.5f);
    DrawHardCircleOutside(render, center_x, center_y, radius, 0x00000000);
}

void ExitSequenceDrawTransition(RenderContext* render) {
    if (!ExitSequenceTransitionVisible()) {
        return;
    }
    ExitSequenceDrawTransitionAmount(render, g_room_transition_fade);
}
