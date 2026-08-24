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
static float g_static_cache_camera_x = -999999.0f;
static float g_static_cache_camera_y = -999999.0f;
static RectI g_prev_player_dirty = { 0, 0, 0, 0 };
static RectI g_prev_player_particles_dirty = { 0, 0, 0, 0 };
static RectI g_prev_tutorial_hint_dirty = { 0, 0, 0, 0 };
static int g_prev_menu_open = 0;
static int g_prev_room_solved = 0;
static int g_prev_context_room = -1;
static int g_prev_tutorial_hint_visible = 0;
static int g_prev_type_a_bump_visible = 0;
static int g_prev_type_a_setting_feedback_visible = 0;
static int g_prev_transition_visible = 0;
static int g_prev_player_dead = 0;

static RectI PlayerDirtyRect(RenderContext* render, const Player* player) {
    RectI rect;
    rect.x = WorldX(render, player->x) - 72;
    rect.y = WorldY(render, player->y) - 72;
    rect.w = 198;
    rect.h = 198;
    return FramebufferClampRect(rect);
}

static RectI PlayerParticlesDirtyRect(RenderContext* render, const PlayerParticle* particles, int particle_count) {
    RectI rect = { 0, 0, 0, 0 };
    if (!particles || particle_count <= 0) {
        return rect;
    }

    int active = 0;
    for (int i = 0; i < particle_count; ++i) {
        const PlayerParticle* p = &particles[i];
        if (p->life <= 0.0f || p->age >= p->life) {
            continue;
        }

        int pad = 36;
        int x = WorldX(render, p->x) - pad;
        int y = WorldY(render, p->y) - pad;
        int w = pad * 2;
        int h = pad * 2;
        if (!active) {
            rect = { x, y, w, h };
            active = 1;
        } else {
            int x2 = rect.x + rect.w;
            int y2 = rect.y + rect.h;
            int px2 = x + w;
            int py2 = y + h;
            if (x < rect.x) rect.x = x;
            if (y < rect.y) rect.y = y;
            if (px2 > x2) x2 = px2;
            if (py2 > y2) y2 = py2;
            rect.w = x2 - rect.x;
            rect.h = y2 - rect.y;
        }
    }

    return active ? FramebufferClampRect(rect) : rect;
}

static RectI TypeAWallsDirtyRect(RenderContext* render, const RoomDef* room) {
    RectI rect = { 860, 610, 190, 300 };
    if (room->type_a_count <= 0) {
        return rect;
    }
    const RectF* wall = &room->type_a_walls[0];
    rect.x = WorldX(render, wall->x) - 18;
    rect.y = WorldY(render, wall->y) - 18;
    rect.w = (int)(wall->w + 0.5f) + 36;
    rect.h = (int)(wall->h + 0.5f) + 36;
    for (int i = 1; i < room->type_a_count; ++i) {
        wall = &room->type_a_walls[i];
        int x = WorldX(render, wall->x) - 18;
        int y = WorldY(render, wall->y) - 18;
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

static RectI TypeAContextDirtyRect(RenderContext* render, const RoomDef* room) {
    if (room->type_a_count <= 0) {
        return { 0, 0, 0, 0 };
    }
    const RectF* wall = &room->type_a_walls[0];
    RectI rect = {
        WorldX(render, wall->x) - 96,
        WorldY(render, wall->y) - 170,
        (int)(wall->w + 0.5f) + 192,
        (int)(wall->h + 0.5f) + 228
    };
    return FramebufferClampRect(rect);
}

static RectI SpeakerWavesDirtyRect(RenderContext* render, const RoomDef* room) {
    if (room->speaker_count <= 0) {
        return { 0, 0, 0, 0 };
    }

    const SpeakerDevice* speaker = &room->speakers[0];
    RectI rect = {
        WorldX(render, speaker->x - 1160.0f) - 32,
        WorldY(render, speaker->y + speaker->height * 0.66f - 480.0f) - 32,
        1500,
        1024
    };
    for (int i = 1; i < room->speaker_count; ++i) {
        speaker = &room->speakers[i];
        int x = WorldX(render, speaker->x - 1160.0f) - 32;
        int y = WorldY(render, speaker->y + speaker->height * 0.66f - 480.0f) - 32;
        int w = 1500;
        int h = 1024;
        int x2 = rect.x + rect.w;
        int y2 = rect.y + rect.h;
        int sx2 = x + w;
        int sy2 = y + h;
        if (x < rect.x) rect.x = x;
        if (y < rect.y) rect.y = y;
        if (sx2 > x2) x2 = sx2;
        if (sy2 > y2) y2 = sy2;
        rect.w = x2 - rect.x;
        rect.h = y2 - rect.y;
    }
    return FramebufferClampRect(rect);
}

static void CaptureCachedFrameState(const StageCacheState* state) {
    g_prev_player_dirty = PlayerDirtyRect(state->render, state->player);
    g_prev_player_particles_dirty = PlayerParticlesDirtyRect(state->render, state->player_particles, state->player_particle_count);
    g_prev_menu_open = state->settings_overlay_visible;
    g_prev_room_solved = state->room_solved;
    g_prev_context_room = state->current_room;
    g_prev_tutorial_hint_visible = state->tutorial_hint_visible;
    g_prev_tutorial_hint_dirty = TutorialUiWorldHintDirtyRect(state->render, state->room, state->player);
    g_prev_type_a_bump_visible = state->type_a_bump_visible;
    g_prev_type_a_setting_feedback_visible = state->type_a_setting_feedback_visible;
    g_prev_transition_visible = state->transition_visible;
    g_prev_player_dead = state->player_dead;
    SettingsUiClearDirty();
}

void StageCacheInvalidate() {
    g_static_cache_valid = 0;
}

void StageCacheEnsure(const StageCacheState* state, StageCacheDrawCallback draw_static) {
    if (!g_static_cache_valid ||
        g_static_cache_type_a_active != state->type_a_active ||
        g_static_cache_room != state->current_room ||
        g_static_cache_camera_x != state->camera_x ||
        g_static_cache_camera_y != state->camera_y) {
        FramebufferBuildStaticCache(state->render, state->bg_color, draw_static, !state->settings_overlay_visible);
        g_static_cache_type_a_active = state->type_a_active;
        g_static_cache_room = state->current_room;
        g_static_cache_camera_x = state->camera_x;
        g_static_cache_camera_y = state->camera_y;
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

    RectI dirty[18];
    int count = 0;
    if (!state->settings_overlay_visible) {
        dirty[count++] = g_prev_player_dirty;
        dirty[count++] = PlayerDirtyRect(state->render, state->player);
        dirty[count++] = g_prev_player_particles_dirty;
        dirty[count++] = PlayerParticlesDirtyRect(state->render, state->player_particles, state->player_particle_count);
        dirty[count++] = ExitSequenceDirtyRect(state->render, &state->room->exit);
        dirty[count++] = SpeakerWavesDirtyRect(state->render, state->room);
    }
    if (state->player_dead || g_prev_player_dead) {
        dirty[count++] = { 0, 0, FB_W, FB_H };
    }
    if (state->tutorial_hint_visible || g_prev_tutorial_hint_visible ||
        state->current_room != g_prev_context_room) {
        dirty[count++] = g_prev_tutorial_hint_dirty;
        dirty[count++] = TutorialUiWorldHintDirtyRect(state->render, state->room, state->player);
    }
    if (state->type_a_bump_visible || g_prev_type_a_bump_visible ||
        state->current_room != g_prev_context_room) {
        dirty[count++] = TypeAContextDirtyRect(state->render, state->room);
    }
    if ((state->settings_overlay_visible || g_prev_menu_open) && !settings_overlay_closed) {
        dirty[count++] = SettingsUiMenuDirtyRect();
    }
    if ((state->type_a_highlighted || state->type_a_setting_feedback_visible || g_prev_type_a_setting_feedback_visible) &&
        !settings_overlay_closed) {
        dirty[count++] = TypeAWallsDirtyRect(state->render, state->room);
    }
    if (state->type_a_setting_feedback_visible || g_prev_type_a_setting_feedback_visible) {
        dirty[count++] = TypeAWallsDirtyRect(state->render, state->room);
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
