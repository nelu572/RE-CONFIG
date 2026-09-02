#include "stage_cache.h"

#include "exit_sequence.h"
#include "framebuffer.h"
#include "game_config.h"
#include "perf.h"
#include "piston.h"
#include "settings_ui.h"
#include "tutorial_ui.h"
#include "walker_enemy_render_geometry.h"

static int g_static_cache_valid = 0;
static int g_static_cache_type_a_active = -1;
static int g_static_cache_room = -1;
static float g_static_cache_camera_x = -999999.0f;
static float g_static_cache_camera_y = -999999.0f;
static RectI g_prev_player_dirty = { 0, 0, 0, 0 };
static RectI g_prev_player_particles_dirty = { 0, 0, 0, 0 };
static RectI g_prev_gravity_boxes_dirty = { 0, 0, 0, 0 };
static RectI g_prev_walker_enemies_dirty = { 0, 0, 0, 0 };
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

static RectI RectUnion(RectI a, RectI b) {
    if (a.w <= 0 || a.h <= 0) return b;
    if (b.w <= 0 || b.h <= 0) return a;
    int right = a.x + a.w;
    int bottom = a.y + a.h;
    int b_right = b.x + b.w;
    int b_bottom = b.y + b.h;
    if (b.x < a.x) a.x = b.x;
    if (b.y < a.y) a.y = b.y;
    if (b_right > right) right = b_right;
    if (b_bottom > bottom) bottom = b_bottom;
    a.w = right - a.x;
    a.h = bottom - a.y;
    return a;
}

static void AppendDirtyRect(RectI* dirty, int* count, int max_count, RectI rect) {
    rect = FramebufferClampRect(rect);
    if (rect.w <= 0 || rect.h <= 0) {
        return;
    }

    for (int i = 0; i < *count; ++i) {
        RectI combined = RectUnion(dirty[i], rect);
        unsigned int combined_area = (unsigned int)(combined.w * combined.h);
        unsigned int separate_area = (unsigned int)(dirty[i].w * dirty[i].h + rect.w * rect.h);
        if (combined_area <= separate_area) {
            rect = combined;
            --(*count);
            dirty[i] = dirty[*count];
            i = -1;
        }
    }

    if (*count < max_count) {
        dirty[(*count)++] = rect;
        return;
    }
    dirty[0] = { 0, 0, FB_W, FB_H };
    *count = 1;
}

static RectI WorldRectDirtyRect(RenderContext* render, const RectF* world_rect, int padding) {
    return {
        WorldX(render, world_rect->x) - padding,
        WorldY(render, world_rect->y) - padding,
        WorldW(render, world_rect->w) + padding * 2,
        WorldH(render, world_rect->h) + padding * 2
    };
}

static RectI GravityBoxesDirtyRect(RenderContext* render, const RectF* boxes, int box_count) {
    RectI dirty = { 0, 0, 0, 0 };
    for (int i = 0; boxes && i < box_count; ++i) {
        dirty = RectUnion(dirty, WorldRectDirtyRect(render, &boxes[i], 4));
    }
    return FramebufferClampRect(dirty);
}

static void AppendPressureDeviceDirtyRects(const StageCacheState* state, RectI* dirty, int* count, int max_count) {
    for (int i = 0; i < state->room->pressure_switch_count; ++i) {
        AppendDirtyRect(dirty, count, max_count,
                        WorldRectDirtyRect(state->render, &state->room->pressure_switches[i].rect, 4));
    }
    for (int i = 0; i < state->room->pressure_platform_count; ++i) {
        const PressurePlatformDevice* platform = &state->room->pressure_platforms[i];
        RectF closed = PressurePlatformRectAt(platform, 0.0f);
        RectF opened = PressurePlatformRectAt(platform, 1.0f);
        RectI travel = RectUnion(WorldRectDirtyRect(state->render, &closed, 4),
                                 WorldRectDirtyRect(state->render, &opened, 4));
        AppendDirtyRect(dirty, count, max_count, travel);
    }
}

static int CacheFloorF(float value) {
    int whole = (int)value;
    return value < (float)whole ? whole - 1 : whole;
}

static int CacheCeilF(float value) {
    int whole = (int)value;
    return value > (float)whole ? whole + 1 : whole;
}

static void ExpandWalkerRenderBounds(float x,
                                     float y,
                                     float* min_x,
                                     float* min_y,
                                     float* max_x,
                                     float* max_y) {
    if (x < *min_x) *min_x = x;
    if (y < *min_y) *min_y = y;
    if (x > *max_x) *max_x = x;
    if (y > *max_y) *max_y = y;
}

static RectI WalkerEnemiesDirtyRect(RenderContext* render,
                                    const RectF* enemies,
                                    int enemy_count,
                                    const float* spike_amounts,
                                    const float* squash_amounts,
                                    const float* turn_squash_amounts) {
    RectI dirty = { 0, 0, 0, 0 };
    if (!enemies || enemy_count <= 0) {
        return dirty;
    }
    for (int i = 0; i < enemy_count; ++i) {
        float spike_amount = spike_amounts ? spike_amounts[i] : 0.0f;
        float squash_amount = squash_amounts ? squash_amounts[i] : 0.0f;
        float turn_squash = turn_squash_amounts ? turn_squash_amounts[i] : 0.0f;
        WalkerEnemyRenderGeometry geometry;
        WalkerEnemyBuildRenderGeometry(render,
                                       &enemies[i],
                                       spike_amount,
                                       squash_amount,
                                       turn_squash,
                                       &geometry);
        float min_x = (float)geometry.body_x;
        float min_y = (float)geometry.body_y;
        float max_x = (float)(geometry.body_x + geometry.body_w);
        float max_y = (float)(geometry.body_y + geometry.body_h);
        for (int spike = 0; spike < geometry.spike_count; ++spike) {
            const WalkerEnemySpikeGeometry* vertex = &geometry.spikes[spike];
            ExpandWalkerRenderBounds(vertex->left_x, vertex->left_y, &min_x, &min_y, &max_x, &max_y);
            ExpandWalkerRenderBounds(vertex->right_x, vertex->right_y, &min_x, &min_y, &max_x, &max_y);
            ExpandWalkerRenderBounds(vertex->tip_x, vertex->tip_y, &min_x, &min_y, &max_x, &max_y);
            if (vertex->is_side_wedge) {
                ExpandWalkerRenderBounds(vertex->cut_x, vertex->cut_y, &min_x, &min_y, &max_x, &max_y);
            }
        }
        // Keep a small rasterization margin after deriving bounds from the actual rendered vertices.
        const int padding = 4;
        RectI bounds = {
            CacheFloorF(min_x) - padding,
            CacheFloorF(min_y) - padding,
            CacheCeilF(max_x) - CacheFloorF(min_x) + padding * 2,
            CacheCeilF(max_y) - CacheFloorF(min_y) + padding * 2
        };
        dirty = RectUnion(dirty, bounds);
    }
    return FramebufferClampRect(dirty);
}
static RectI TypeAWallsDirtyRect(RenderContext* render, const RoomDef* room) {
    RectI rect = { 860, 610, 190, 300 };
    if (room->type_a_count <= 0) {
        return rect;
    }
    const RectF* wall = &room->type_a_walls[0];
    rect.x = WorldX(render, wall->x) - 18;
    rect.y = WorldY(render, wall->y) - 18;
    rect.w = WorldW(render, wall->w) + 36;
    rect.h = WorldH(render, wall->h) + 36;
    for (int i = 1; i < room->type_a_count; ++i) {
        wall = &room->type_a_walls[i];
        int x = WorldX(render, wall->x) - 18;
        int y = WorldY(render, wall->y) - 18;
        int w = WorldW(render, wall->w) + 36;
        int h = WorldH(render, wall->h) + 36;
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
        WorldW(render, wall->w) + 192,
        WorldH(render, wall->h) + 228
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

static RectI PistonsDirtyRect(RenderContext* render, const RoomDef* room) {
    if (room->piston_count <= 0) {
        return { 0, 0, 0, 0 };
    }

    RectF dirty = PistonTravelDirtyRect(&room->pistons[0]);
    RectI rect = {
        WorldX(render, dirty.x),
        WorldY(render, dirty.y),
        WorldW(render, dirty.w),
        WorldH(render, dirty.h)
    };
    for (int i = 1; i < room->piston_count; ++i) {
        dirty = PistonTravelDirtyRect(&room->pistons[i]);
        int x = WorldX(render, dirty.x);
        int y = WorldY(render, dirty.y);
        int w = WorldW(render, dirty.w);
        int h = WorldH(render, dirty.h);
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
    return FramebufferClampRect(rect);
}

static void CaptureCachedFrameState(const StageCacheState* state) {
    g_prev_player_dirty = PlayerDirtyRect(state->render, state->player);
    g_prev_player_particles_dirty = PlayerParticlesDirtyRect(state->render, state->player_particles, state->player_particle_count);
    g_prev_gravity_boxes_dirty = GravityBoxesDirtyRect(state->render, state->gravity_boxes, state->gravity_box_count);
    g_prev_walker_enemies_dirty = WalkerEnemiesDirtyRect(state->render, state->walker_enemies, state->walker_enemy_count, state->walker_enemy_spike_amount, state->walker_enemy_squash_amount, state->walker_enemy_turn_squash);
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
    g_prev_gravity_boxes_dirty = { 0, 0, 0, 0 };
    g_prev_walker_enemies_dirty = { 0, 0, 0, 0 };
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

    static constexpr int MAX_DIRTY_RECTS = 48;
    RectI dirty[MAX_DIRTY_RECTS];
    int count = 0;
    if (!state->settings_overlay_visible) {
        AppendDirtyRect(dirty, &count, MAX_DIRTY_RECTS, g_prev_gravity_boxes_dirty);
        AppendDirtyRect(dirty, &count, MAX_DIRTY_RECTS,
                        GravityBoxesDirtyRect(state->render, state->gravity_boxes, state->gravity_box_count));
        AppendPressureDeviceDirtyRects(state, dirty, &count, MAX_DIRTY_RECTS);
        RectI walker_current_dirty = WalkerEnemiesDirtyRect(state->render, state->walker_enemies, state->walker_enemy_count, state->walker_enemy_spike_amount, state->walker_enemy_squash_amount, state->walker_enemy_turn_squash);
        AppendDirtyRect(dirty, &count, MAX_DIRTY_RECTS, RectUnion(g_prev_walker_enemies_dirty, walker_current_dirty));
        AppendDirtyRect(dirty, &count, MAX_DIRTY_RECTS, g_prev_player_dirty);
        AppendDirtyRect(dirty, &count, MAX_DIRTY_RECTS, PlayerDirtyRect(state->render, state->player));
        AppendDirtyRect(dirty, &count, MAX_DIRTY_RECTS, g_prev_player_particles_dirty);
        AppendDirtyRect(dirty, &count, MAX_DIRTY_RECTS, PlayerParticlesDirtyRect(state->render, state->player_particles, state->player_particle_count));
        AppendDirtyRect(dirty, &count, MAX_DIRTY_RECTS, ExitSequenceDirtyRect(state->render, &state->room->exit));
        AppendDirtyRect(dirty, &count, MAX_DIRTY_RECTS, SpeakerWavesDirtyRect(state->render, state->room));
        AppendDirtyRect(dirty, &count, MAX_DIRTY_RECTS, PistonsDirtyRect(state->render, state->room));
    }
    if (state->player_dead || g_prev_player_dead) {
        AppendDirtyRect(dirty, &count, MAX_DIRTY_RECTS, { 0, 0, FB_W, FB_H });
    }
    if (state->tutorial_hint_visible || g_prev_tutorial_hint_visible ||
        state->current_room != g_prev_context_room) {
        AppendDirtyRect(dirty, &count, MAX_DIRTY_RECTS, g_prev_tutorial_hint_dirty);
        AppendDirtyRect(dirty, &count, MAX_DIRTY_RECTS, TutorialUiWorldHintDirtyRect(state->render, state->room, state->player));
    }
    if (state->type_a_bump_visible || g_prev_type_a_bump_visible ||
        state->current_room != g_prev_context_room) {
        AppendDirtyRect(dirty, &count, MAX_DIRTY_RECTS, TypeAContextDirtyRect(state->render, state->room));
    }
    if ((state->settings_overlay_visible || g_prev_menu_open) && !settings_overlay_closed) {
        AppendDirtyRect(dirty, &count, MAX_DIRTY_RECTS, SettingsUiMenuDirtyRect());
    }
    if ((state->type_a_highlighted || state->type_a_setting_feedback_visible || g_prev_type_a_setting_feedback_visible) &&
        !settings_overlay_closed) {
        AppendDirtyRect(dirty, &count, MAX_DIRTY_RECTS, TypeAWallsDirtyRect(state->render, state->room));
    }
    if (state->type_a_setting_feedback_visible || g_prev_type_a_setting_feedback_visible) {
        AppendDirtyRect(dirty, &count, MAX_DIRTY_RECTS, TypeAWallsDirtyRect(state->render, state->room));
    }
    if (state->room_solved || g_prev_room_solved) {
        AppendDirtyRect(dirty, &count, MAX_DIRTY_RECTS, { 690, 50, 540, 90 });
    }
    if (state->transition_visible || g_prev_transition_visible) {
        AppendDirtyRect(dirty, &count, MAX_DIRTY_RECTS, { 0, 0, FB_W, FB_H });
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
