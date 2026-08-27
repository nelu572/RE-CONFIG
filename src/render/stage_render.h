#pragma once

#include <stdint.h>

#include "player.h"
#include "render.h"
#include "world.h"

typedef void (*StageRenderDrawTextSmallCallback)(int x, int y, const char* text, int scale, uint32_t color);

struct StageRenderState {
    RenderContext* render;
    const RoomDef* room;
    const Player* player;
    const PlayerParticle* player_particles;
    int player_particle_count;
    int player_visible;
    GravityDirection gravity_direction;
    uint32_t bg_color;
    uint32_t platform_color;
    uint32_t player_color;
    uint32_t type_a_color;
    uint32_t type_a_pattern_color;
    uint32_t type_a_off_color;
    uint32_t type_a_off_pattern_color;
    double render_time_seconds;
    double speaker_time_seconds;
    float piston_time_seconds;
    const RectF* gravity_boxes;
    int gravity_box_count;
    const int* pressure_switch_pressed;
    const float* pressure_switch_anim;
    const float* pressure_platform_open_amount;
    int room_exit_unlocked;
    int type_a_off_line_thickness;
    int type_a_off_visible_path_len;
    uint32_t effect_color;
    uint32_t text_color;
    uint32_t text_dim_color;
    int type_a_active;
    float speaker_volume;
    int highlight_type_a;
    int type_a_bump_visible;
    int type_a_setting_feedback_visible;
    int settings_overlay_visible;
    int disable_static_cache;
    StageRenderDrawTextSmallCallback draw_text_small;
};

void StageRenderDrawStatic(const StageRenderState* state);
void StageRenderDrawDynamic(const StageRenderState* state);
void StageRenderDrawFrame(const StageRenderState* state);
void StageRenderDrawTypeAArtTest(const StageRenderState* state, int mode);