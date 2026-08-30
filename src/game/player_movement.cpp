#include "player_movement.h"

#include "collision.h"

static float MovementClampF(float value, float lo, float hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static void MovementTickTimer(float* timer, float dt) {
    if (*timer <= 0.0f) {
        *timer = 0.0f;
        return;
    }
    *timer -= dt;
    if (*timer < 0.0f) {
        *timer = 0.0f;
    }
}

static int PlatformSolidCount(const RoomDef* room) {
    return room->platform_count;
}

static int TypeASolidCount(const RoomDef* room, int type_a_collision_active) {
    return type_a_collision_active ? room->type_a_count : 0;
}

static int TotalSolidCount(const RoomDef* room, int type_a_collision_active, int extra_solid_count) {
    return PlatformSolidCount(room) + TypeASolidCount(room, type_a_collision_active) + extra_solid_count;
}

static const RectF* SolidAt(const RoomDef* room, int type_a_collision_active, const RectF* extra_solids, int index) {
    int platform_count = PlatformSolidCount(room);
    if (index < platform_count) {
        return &room->platforms[index];
    }
    index -= platform_count;
    int type_a_count = TypeASolidCount(room, type_a_collision_active);
    if (index < type_a_count) {
        return &room->type_a_walls[index];
    }
    return &extra_solids[index - type_a_count];
}
static void MovementGravityVector(GravityDirection direction, int* x, int* y) {
    *x = 0;
    *y = 1;
    if (direction == GRAVITY_UP) {
        *y = -1;
    } else if (direction == GRAVITY_RIGHT) {
        *x = 1;
        *y = 0;
    } else if (direction == GRAVITY_LEFT) {
        *x = -1;
        *y = 0;
    }
}

static void MovementTangentVector(int gravity_x, int gravity_y, int* x, int* y) {
    if (gravity_y != 0) {
        *x = 1;
        *y = 0;
    } else {
        *x = 0;
        *y = 1;
    }
}

static float MovementVelocityOnAxis(const Player* player, int axis_x, int axis_y) {
    return player->vx * (float)axis_x + player->vy * (float)axis_y;
}

static void MovementSetVelocity(Player* player,
                                int tangent_x,
                                int tangent_y,
                                float tangent_speed,
                                int gravity_x,
                                int gravity_y,
                                float gravity_speed) {
    player->vx = (float)tangent_x * tangent_speed + (float)gravity_x * gravity_speed;
    player->vy = (float)tangent_y * tangent_speed + (float)gravity_y * gravity_speed;
}

static int MovementRangesOverlap(float a0, float a1, float b0, float b1) {
    return a0 < b1 && a1 > b0;
}

static int MovementAxisCrossOverlap(const RectF* previous, const RectF* current, const RectF* solid, int axis_x, int axis_y) {
    if (axis_x != 0) {
        float y0 = previous->y < current->y ? previous->y : current->y;
        float previous_y1 = previous->y + previous->h;
        float current_y1 = current->y + current->h;
        float y1 = previous_y1 > current_y1 ? previous_y1 : current_y1;
        return MovementRangesOverlap(y0, y1, solid->y, solid->y + solid->h);
    }
    if (axis_y != 0) {
        float x0 = previous->x < current->x ? previous->x : current->x;
        float previous_x1 = previous->x + previous->w;
        float current_x1 = current->x + current->w;
        float x1 = previous_x1 > current_x1 ? previous_x1 : current_x1;
        return MovementRangesOverlap(x0, x1, solid->x, solid->x + solid->w);
    }
    return 0;
}

static int MovementAxisHitsSolid(const RectF* previous, const RectF* current, const RectF* solid, int axis_x, int axis_y, float velocity) {
    if (!MovementAxisCrossOverlap(previous, current, solid, axis_x, axis_y)) {
        return RectsOverlap(current, solid);
    }
    if (axis_x != 0) {
        if (velocity > 0.0f) {
            return previous->x + previous->w <= solid->x && current->x + current->w > solid->x || RectsOverlap(current, solid);
        }
        if (velocity < 0.0f) {
            float solid_right = solid->x + solid->w;
            return previous->x >= solid_right && current->x < solid_right || RectsOverlap(current, solid);
        }
    } else if (axis_y != 0) {
        if (velocity > 0.0f) {
            return previous->y + previous->h <= solid->y && current->y + current->h > solid->y || RectsOverlap(current, solid);
        }
        if (velocity < 0.0f) {
            float solid_bottom = solid->y + solid->h;
            return previous->y >= solid_bottom && current->y < solid_bottom || RectsOverlap(current, solid);
        }
    }
    return RectsOverlap(current, solid);
}

static void RegisterTypeAContact(PlayerMovementFeedback* feedback, double now_seconds, int blocked) {
    if (!feedback->type_a_contacted) {
        feedback->type_a_bump_until = now_seconds + 0.22;
    }
    feedback->type_a_contacted = 1;
    if (blocked) {
        feedback->type_a_blocked_this_frame = 1;
    }
}

static void ResolveAxis(Player* player,
                        const RoomDef* room,
                        int type_a_collision_active,
                        const RectF* extra_solids,
                        int extra_solid_count,
                        double now_seconds,
                        PlayerMovementFeedback* feedback,
                        const RectF* previous_p,
                        RectF* p,
                        int axis_x,
                        int axis_y,
                        int gravity_x,
                        int gravity_y,
                        int gravity_axis) {
    int platform_count = PlatformSolidCount(room);
    int type_a_count = TypeASolidCount(room, type_a_collision_active);
    int total_count = TotalSolidCount(room, type_a_collision_active, extra_solid_count);
    float axis_velocity = axis_x != 0 ? player->vx : player->vy;
    int hit_index = -1;
    float best_position = axis_x != 0 ? p->x : p->y;

    for (int i = 0; i < total_count; ++i) {
        const RectF* solid = SolidAt(room, type_a_collision_active, extra_solids, i);
        if (!MovementAxisHitsSolid(previous_p, p, solid, axis_x, axis_y, axis_velocity)) {
            continue;
        }

        float candidate_position = best_position;
        if (axis_x != 0) {
            if (axis_velocity > 0.0f) {
                candidate_position = solid->x - p->w;
                if (hit_index >= 0 && candidate_position >= best_position) {
                    continue;
                }
            } else if (axis_velocity < 0.0f) {
                candidate_position = solid->x + solid->w;
                if (hit_index >= 0 && candidate_position <= best_position) {
                    continue;
                }
            } else if (RectsOverlap(p, solid)) {
                float push_left = p->x + p->w - solid->x;
                float push_right = solid->x + solid->w - p->x;
                candidate_position = p->x + (push_left < push_right ? -push_left : push_right);
                if (hit_index >= 0) {
                    continue;
                }
            } else {
                continue;
            }
        } else if (axis_y != 0) {
            if (axis_velocity > 0.0f) {
                candidate_position = solid->y - p->h;
                if (hit_index >= 0 && candidate_position >= best_position) {
                    continue;
                }
            } else if (axis_velocity < 0.0f) {
                candidate_position = solid->y + solid->h;
                if (hit_index >= 0 && candidate_position <= best_position) {
                    continue;
                }
            } else if (RectsOverlap(p, solid)) {
                float push_up = p->y + p->h - solid->y;
                float push_down = solid->y + solid->h - p->y;
                candidate_position = p->y + (push_up < push_down ? -push_up : push_down);
                if (hit_index >= 0) {
                    continue;
                }
            } else {
                continue;
            }
        }

        best_position = candidate_position;
        hit_index = i;
    }

    if (hit_index < 0) {
        return;
    }

    if (hit_index >= platform_count && hit_index < platform_count + type_a_count) {
        RegisterTypeAContact(feedback, now_seconds, !gravity_axis);
    }
    if (axis_x != 0) {
        p->x = best_position;
        if (gravity_axis && axis_velocity > 0.0f && gravity_x > 0) player->grounded = 1;
        if (gravity_axis && axis_velocity < 0.0f && gravity_x < 0) player->grounded = 1;
        player->x = p->x;
        player->vx = 0.0f;
    } else if (axis_y != 0) {
        p->y = best_position;
        if (gravity_axis && axis_velocity > 0.0f && gravity_y > 0) player->grounded = 1;
        if (gravity_axis && axis_velocity < 0.0f && gravity_y < 0) player->grounded = 1;
        player->y = p->y;
        player->vy = 0.0f;
    }
}
static int HasGroundSupport(const Player* player,
                            const RoomDef* room,
                            int type_a_collision_active,
                            const RectF* extra_solids,
                            int extra_solid_count,
                            int gravity_x,
                            int gravity_y) {
    RectF probe = PlayerCollisionRect(player);
    probe.x += (float)gravity_x;
    probe.y += (float)gravity_y;
    int total_count = TotalSolidCount(room, type_a_collision_active, extra_solid_count);
    for (int i = 0; i < total_count; ++i) {
        const RectF* solid = SolidAt(room, type_a_collision_active, extra_solids, i);
        if (RectsOverlap(&probe, solid)) {
            return 1;
        }
    }
    return 0;
}

PlayerMovementResult UpdatePlayerMovement(Player* player,
                                          const RoomDef* room,
                                          float dt,
                                          float move,
                                          int jump_pressed,
                                          int jump_active,
                                          int gravity_active,
                                          int skip_ground_gravity,
                                          GravityDirection gravity_direction,
                                          int type_a_collision_active,
                                          const RectF* extra_solids,
                                          int extra_solid_count,
                                          float external_vx,
                                          float external_vy,
                                          double now_seconds,
                                          PlayerMovementFeedback* feedback) {
    const float speed = 360.0f;
    const float gravity = 1550.0f;
    const float jump = -675.0f;
    const float jump_buffer_seconds = 0.12f;
    const float coyote_seconds = 0.10f;
    const float fall_gravity_multiplier = 1.35f;

    feedback->type_a_blocked_this_frame = 0;

    int gravity_x;
    int gravity_y;
    int tangent_x;
    int tangent_y;
    MovementGravityVector(gravity_direction, &gravity_x, &gravity_y);
    MovementTangentVector(gravity_x, gravity_y, &tangent_x, &tangent_y);

    int was_grounded = player->grounded;
    int jump_started = 0;
    if (jump_active) {
        if (jump_pressed) {
            player->jump_buffer_timer = jump_buffer_seconds;
        } else {
            MovementTickTimer(&player->jump_buffer_timer, dt);
        }
    } else {
        player->jump_buffer_timer = 0.0f;
    }
    if (player->grounded) {
        player->coyote_timer = coyote_seconds;
    } else {
        MovementTickTimer(&player->coyote_timer, dt);
    }

    float gravity_speed = MovementVelocityOnAxis(player, gravity_x, gravity_y);
    float tangent_speed = move * speed;
    tangent_speed += external_vx * (float)tangent_x + external_vy * (float)tangent_y;
    gravity_speed += external_vx * (float)gravity_x + external_vy * (float)gravity_y;
    if (jump_active &&
        player->jump_buffer_timer > 0.0f &&
        player->coyote_timer > 0.0f) {
        gravity_speed = gravity_speed < 0.0f ? gravity_speed + jump : jump;
        player->grounded = 0;
        player->jump_buffer_timer = 0.0f;
        player->coyote_timer = 0.0f;
        jump_started = 1;
    }
    if (gravity_active && !(skip_ground_gravity && was_grounded && !jump_started)) {
        float gravity_multiplier = gravity_speed > 0.0f ? fall_gravity_multiplier : 1.0f;
        gravity_speed += gravity * gravity_multiplier * dt;
    }
    gravity_speed = MovementClampF(gravity_speed, -900.0f, 1100.0f);
    float gravity_speed_before_resolve = gravity_speed;
    MovementSetVelocity(player, tangent_x, tangent_y, tangent_speed, gravity_x, gravity_y, gravity_speed);

    RectF previous_pr = PlayerCollisionRect(player);
    if (tangent_x != 0) {
        player->x += player->vx * dt;
    } else {
        player->y += player->vy * dt;
    }
    RectF pr = PlayerCollisionRect(player);
    ResolveAxis(player, room, type_a_collision_active, extra_solids, extra_solid_count, now_seconds, feedback, &previous_pr, &pr, tangent_x, tangent_y, gravity_x, gravity_y, 0);

    player->grounded = 0;
    previous_pr = PlayerCollisionRect(player);
    if (gravity_x != 0) {
        player->x += player->vx * dt;
    } else {
        player->y += player->vy * dt;
    }
    pr = PlayerCollisionRect(player);
    ResolveAxis(player, room, type_a_collision_active, extra_solids, extra_solid_count, now_seconds, feedback, &previous_pr, &pr, gravity_x, gravity_y, gravity_x, gravity_y, 1);
    gravity_speed = MovementVelocityOnAxis(player, gravity_x, gravity_y);
    if (!player->grounded && gravity_speed >= 0.0f && HasGroundSupport(player, room, type_a_collision_active, extra_solids, extra_solid_count, gravity_x, gravity_y)) {
        player->grounded = 1;
    }

    PlayerMovementResult result;
    result.jump_started = jump_started;
    result.landed = !was_grounded && player->grounded && gravity_speed_before_resolve > 80.0f;
    return result;
}
