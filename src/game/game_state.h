#pragma once

#include "camera.h"
#include "delete_rules.h"
#include "player.h"

static constexpr int GAME_MAX_GRAVITY_BOXES = 4;
static constexpr int GAME_MAX_PISTONS = 16;
static constexpr int GAME_MAX_PRESSURE_SWITCHES = 8;
static constexpr int GAME_MAX_PRESSURE_PLATFORMS = 8;
static constexpr int GAME_MAX_WALKER_ENEMIES = 8;

enum GameAudioEvent {
    GAME_AUDIO_JUMP = 1 << 0,
    GAME_AUDIO_LAND = 1 << 1,
    GAME_AUDIO_DEATH = 1 << 2,
    GAME_AUDIO_CLEAR = 1 << 3,
    GAME_AUDIO_SWITCH = 1 << 4,
};

struct GameRoomStartState {
    float player_x;
    float player_y;
    GravityDirection gravity_direction;
    DeleteState delete_state;
};

struct GameState {
    Camera camera;
    Player player;
    PlayerParticle player_particles[PLAYER_PARTICLE_COUNT];
    DeleteState delete_state;
    GameRoomStartState room_start_state;
    GravityDirection gravity_direction;
    int current_room;
    int checkpoint_room;
    int checkpoint_active;
    float checkpoint_flag_drop;
    int cleared_room_this_frame;
    int player_dead;
    int audio_events;
    float death_respawn_timer;
    int type_a_contacted;
    int type_a_blocked_this_frame;
    double type_a_bump_until;
    double type_a_setting_feedback_until;
    double gravity_setting_feedback_until;
    double room_started_at_seconds;
    float speaker_push_vx;
    float speaker_push_vy;
    float piston_time_seconds;
    int player_on_piston_support;
    float piston_effective_extension[GAME_MAX_PISTONS];
    RectF gravity_boxes[GAME_MAX_GRAVITY_BOXES];
    float gravity_box_vx[GAME_MAX_GRAVITY_BOXES];
    float gravity_box_vy[GAME_MAX_GRAVITY_BOXES];
    int gravity_box_grounded[GAME_MAX_GRAVITY_BOXES];
    int gravity_box_piston_driven[GAME_MAX_GRAVITY_BOXES];
    int pressure_switch_pressed[GAME_MAX_PRESSURE_SWITCHES];
    float pressure_switch_anim[GAME_MAX_PRESSURE_SWITCHES];
    float pressure_platform_open_amount[GAME_MAX_PRESSURE_PLATFORMS];
    int pressure_platform_open_cycle_pending[GAME_MAX_PRESSURE_PLATFORMS];
    int room_exit_unlocked;
    RectF walker_enemies[GAME_MAX_WALKER_ENEMIES];
    float walker_enemy_gravity_speed[GAME_MAX_WALKER_ENEMIES];
    int walker_enemy_direction[GAME_MAX_WALKER_ENEMIES];
    int walker_enemy_grounded[GAME_MAX_WALKER_ENEMIES];
    float walker_enemy_spike_amount[GAME_MAX_WALKER_ENEMIES];
    float walker_enemy_spike_delay[GAME_MAX_WALKER_ENEMIES];
    float walker_enemy_squash_amount[GAME_MAX_WALKER_ENEMIES];
    float walker_enemy_eye_crouch_amount[GAME_MAX_WALKER_ENEMIES];
    float walker_enemy_turn_squash[GAME_MAX_WALKER_ENEMIES];
    int walker_enemy_player_near[GAME_MAX_WALKER_ENEMIES];
};
