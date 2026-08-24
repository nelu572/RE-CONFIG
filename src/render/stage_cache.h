#pragma once

#include "player.h"
#include "render.h"
#include "world.h"

typedef void (*StageCacheDrawCallback)();

struct StageCacheState {
    RenderContext* render;
    const RoomDef* room;
    const Player* player;
    const PlayerParticle* player_particles;
    int player_particle_count;
    int current_room;
    int type_a_active;
    int type_a_highlighted;
    int tutorial_hint_visible;
    int type_a_bump_visible;
    int type_a_setting_feedback_visible;
    int settings_overlay_visible;
    int settings_dirty;
    int room_solved;
    int transition_visible;
    int player_dead;
    float camera_x;
    float camera_y;
    unsigned int bg_color;
};

void StageCacheInvalidate();
void StageCacheEnsure(const StageCacheState* state, StageCacheDrawCallback draw_static);
void StageCacheDrawCached(const StageCacheState* state, StageCacheDrawCallback draw_dynamic);
