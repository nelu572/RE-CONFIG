#include "stage_cache.h"

#include "exit_sequence.h"
#include "framebuffer.h"
#include "game_config.h"
#include "perf.h"
#include "settings_ui.h"
#include "tutorial_ui.h"

static int g_static_cache_valid = 0;
static int g_static_cache_type_a_active = -1;
static int g_static_cache_room = -1;
static RectI g_prev_player_dirty = { 0, 0, 0, 0 };
static RectI g_prev_tutorial_hint_dirty = { 0, 0, 0, 0 };
static int g_prev_menu_open = 0;
static int g_prev_room_solved = 0;
static int g_prev_context_room = -1;
static int g_prev_tutorial_hint_visible = 0;
static int g_prev_type_a_bump_visible = 0;
static int g_prev_type_a_setting_feedback_visible = 0;
static int g_prev_transition_visible = 0;

static RectI PlayerDirtyRect(const Player* player) {
    RectI rect;
    rect.x = (int)(player->x + 0.5f) - 72;
    rect.y = (int)(player->y + 0.5f) - 20;
    rect.w = 198;
    rect.h = 126;
    return FramebufferClampRect(rect);
}

static RectI TypeAWallsDirtyRect(const RoomDef* room) {
    RectI rect = { 860, 610, 190, 300 };
    if (room->type_a_count <= 0) {
        return rect;
    }
    const RectF* wall = &room->type_a_walls[0];
    rect.x = (int)(wall->x + 0.5f) - 18;
    rect.y = (int)(wall->y + 0.5f) - 18;
    rect.w = (int)(wall->w + 0.5f) + 36;
    rect.h = (int)(wall->h + 0.5f) + 36;
    for (int i = 1; i < room->type_a_count; ++i) {
        wall = &room->type_a_walls[i];
        int x = (int)(wall->x + 0.5f) - 18;
        int y = (int)(wall->y + 0.5f) - 18;
        int w = (int)(wall->w + 0.5f) + 36;
        int h = (int)(wall->h + 0.5f) + 36;
        int x2 = rect.x + rect.w;
        int y2 = rect.y + rect.h;
        int wx2 = x + w;
        int wy2 = y + h;
        if (x < rect.x) rect.x = x;
        if (y < rect.y) rect.y = y;
        if (wx2 > x2) x2 = wx2;
        if (wy2 > y2) y2 = wy2;
        rect.w = x2 - rect.x;
        rect.h = y2 - rect.y;
    }
    return FramebufferClampRect(rect);
}

static RectI TypeAContextDirtyRect(const RoomDef* room) {
    if (room->type_a_count <= 0) {
        return { 0, 0, 0, 0 };
    }
    const RectF* wall = &room->type_a_walls[0];
    RectI rect = {
        (int)(wall->x + 0.5f) - 96,
        (int)(wall->y + 0.5f) - 170,
        (int)(wall->w + 0.5f) + 192,
        (int)(wall->h + 0.5f) + 228
    };
    return FramebufferClampRect(rect);
}

static void CaptureCachedFrameState(const StageCacheState* state) {
    g_prev_player_dirty = PlayerDirtyRect(state->player);
    g_prev_menu_open = state->settings_overlay_visible;
    g_prev_room_solved = state->room_solved;
    g_prev_context_room = state->current_room;
    g_prev_tutorial_hint_visible = state->tutorial_hint_visible;
    g_prev_tutorial_hint_dirty = TutorialUiWorldHintDirtyRect(state->render, state->room, state->player);
    g_prev_type_a_bump_visible = state->type_a_bump_visible;
    g_prev_type_a_setting_feedback_visible = state->type_a_setting_feedback_visible;
    g_prev_transition_visible = state->transition_visible;
    SettingsUiClearDirty();
}

void StageCacheInvalidate() {
    g_static_cache_valid = 0;
}

void StageCacheEnsure(const StageCacheState* state, StageCacheDrawCallback draw_static) {
    if (!g_static_cache_valid ||
        g_static_cache_type_a_active != state->type_a_active ||
        g_static_cache_room != state->current_room) {
        FramebufferBuildStaticCache(state->render, state->bg_color, draw_static, !state->settings_overlay_visible);
        g_static_cache_type_a_active = state->type_a_active;
        g_static_cache_room = state->current_room;
        g_static_cache_valid = 1;
    }
}

void StageCacheDrawCached(const StageCacheState* state, StageCacheDrawCallback draw_dynamic) {
    int settings_overlay_closed = !state->settings_overlay_visible && g_prev_menu_open;

    if (state->settings_overlay_visible &&
        !state->settings_dirty &&
        !state->type_a_setting_feedback_visible &&
        !g_prev_type_a_setting_feedback_visible &&
        !state->transition_visible &&
        !g_prev_transition_visible) {
        return;
    }

    if (settings_overlay_closed) {
        FramebufferCopyStaticToLive();
    }

    RectI dirty[16];
    int count = 0;
    if (!state->settings_overlay_visible) {
        dirty[count++] = g_prev_player_dirty;
        dirty[count++] = PlayerDirtyRect(state->player);
        dirty[count++] = ExitSequenceDirtyRect(&state->room->exit);
    }
    if (state->tutorial_hint_visible || g_prev_tutorial_hint_visible ||
        state->current_room != g_prev_context_room) {
        dirty[count++] = g_prev_tutorial_hint_dirty;
        dirty[count++] = TutorialUiWorldHintDirtyRect(state->render, state->room, state->player);
    }
    if (state->type_a_bump_visible || g_prev_type_a_bump_visible ||
        state->current_room != g_prev_context_room) {
        dirty[count++] = TypeAContextDirtyRect(state->room);
    }
    if ((state->settings_overlay_visible || g_prev_menu_open) && !settings_overlay_closed) {
        dirty[count++] = SettingsUiMenuDirtyRect();
    }
    if ((state->type_a_highlighted || state->type_a_setting_feedback_visible || g_prev_type_a_setting_feedback_visible) &&
        !settings_overlay_closed) {
        dirty[count++] = TypeAWallsDirtyRect(state->room);
    }
    if (state->type_a_setting_feedback_visible || g_prev_type_a_setting_feedback_visible) {
        dirty[count++] = TypeAWallsDirtyRect(state->room);
    }
    if (state->room_solved || g_prev_room_solved) {
        dirty[count++] = { 690, 50, 540, 90 };
    }
    if (state->transition_visible || g_prev_transition_visible) {
        dirty[count++] = { 0, 0, FB_W, FB_H };
    }

    unsigned int dirty_area = 0;
    for (int i = 0; i < count; ++i) {
        RectI r = FramebufferClampRect(dirty[i]);
        dirty_area += (unsigned int)(r.w * r.h);
    }
    PerfBucketAddDirty(dirty_area);

    double t0 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
    for (int i = 0; i < count; ++i) {
        FramebufferRestoreStaticRect(dirty[i]);
    }
    double t1 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
    if (g_perf_config.enabled) {
        double ms = (t1 - t0) * 1000.0;
        g_perf_stats.restore_ms += ms;
        PerfMax(&g_perf_stats.max_restore_ms, ms);
    }

    draw_dynamic();

    double t2 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
    if (g_perf_config.enabled) {
        double ms = (t2 - t1) * 1000.0;
        g_perf_stats.render_ms += ms;
        PerfMax(&g_perf_stats.max_render_ms, ms);
    }

    for (int i = 0; i < count; ++i) {
        FramebufferDownsampleRectFromTo(FramebufferRenderPixels(), FramebufferPixels(), dirty[i]);
    }
    double t3 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
    if (g_perf_config.enabled) {
        double ms = (t3 - t2) * 1000.0;
        g_perf_stats.downsample_ms += ms;
        PerfMax(&g_perf_stats.max_downsample_ms, ms);
    }

    CaptureCachedFrameState(state);
}
