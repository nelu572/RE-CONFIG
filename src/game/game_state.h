#pragma once

#include "camera.h"
#include "delete_rules.h"
#include "player.h"

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
    int player_dead;
    float death_respawn_timer;
    int type_a_contacted;
    int type_a_blocked_this_frame;
    double type_a_bump_until;
    double type_a_setting_feedback_until;
    double gravity_setting_feedback_until;
};
