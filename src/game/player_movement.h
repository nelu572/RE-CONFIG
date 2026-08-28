#pragma once

#include "player.h"
#include "world.h"

struct PlayerMovementFeedback {
    int type_a_contacted;
    int type_a_blocked_this_frame;
    double type_a_bump_until;
};

struct PlayerMovementResult {
    int jump_started;
    int landed;
};

PlayerMovementResult UpdatePlayerMovement(Player* player,
                                          const RoomDef* room,
                                          float dt,
                                          float move,
                                          int jump_pressed,
                                          int jump_released,
                                          int jump_active,
                                          int gravity_active,
                                          GravityDirection gravity_direction,
                                          int type_a_collision_active,
                                          const RectF* extra_solids,
                                          int extra_solid_count,
                                          float external_vx,
                                          float external_vy,
                                          double now_seconds,
                                          PlayerMovementFeedback* feedback);
