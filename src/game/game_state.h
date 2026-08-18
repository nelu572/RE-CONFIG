#pragma once

#include "delete_rules.h"
#include "player.h"

struct GameState {
    Player player;
    PlayerParticle player_particles[PLAYER_PARTICLE_COUNT];
    DeleteState delete_state;
    int current_room;
    int type_a_contacted;
    int type_a_blocked_this_frame;
    double type_a_bump_until;
    double type_a_setting_feedback_until;
    double gravity_setting_feedback_until;
};
