#include "player_movement.h"

#include "collision.h"

static float MovementClampF(float value, float lo, float hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static int PlatformSolidCount(const RoomDef* room) {
    return room->platform_count;
}

static int TotalSolidCount(const RoomDef* room, int type_a_collision_active) {
    return PlatformSolidCount(room) + (type_a_collision_active ? room->type_a_count : 0);
}

static const RectF* SolidAt(const RoomDef* room, int index) {
    if (index < room->platform_count) {
        return &room->platforms[index];
    }
    return &room->type_a_walls[index - room->platform_count];
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
                        double now_seconds,
                        PlayerMovementFeedback* feedback,
                        RectF* p,
                        int axis_x,
                        int axis_y,
                        int gravity_x,
                        int gravity_y,
                        int gravity_axis) {
    int platform_count = PlatformSolidCount(room);
    int total_count = TotalSolidCount(room, type_a_collision_active);
    for (int i = 0; i < total_count; ++i) {
        const RectF* solid = SolidAt(room, i);
        if (!RectsOverlap(p, solid)) continue;
        if (i >= platform_count) {
            RegisterTypeAContact(feedback, now_seconds, !gravity_axis);
        }
        if (axis_x != 0) {
            if (player->vx > 0.0f) {
                p->x = solid->x - p->w;
                if (gravity_axis && gravity_x > 0) player->grounded = 1;
            } else if (player->vx < 0.0f) {
                p->x = solid->x + solid->w;
                if (gravity_axis && gravity_x < 0) player->grounded = 1;
            }
            player->x = p->x;
            player->vx = 0.0f;
        } else if (axis_y != 0) {
            if (player->vy > 0.0f) {
                p->y = solid->y - p->h;
                if (gravity_axis && gravity_y > 0) player->grounded = 1;
            } else if (player->vy < 0.0f) {
                p->y = solid->y + solid->h;
                if (gravity_axis && gravity_y < 0) player->grounded = 1;
            }
            player->y = p->y;
            player->vy = 0.0f;
        }
    }
}

static int HasGroundSupport(const Player* player,
                            const RoomDef* room,
                            int type_a_collision_active,
                            int gravity_x,
                            int gravity_y) {
    RectF probe = PlayerCollisionRect(player);
    probe.x += (float)gravity_x;
    probe.y += (float)gravity_y;
    int total_count = TotalSolidCount(room, type_a_collision_active);
    for (int i = 0; i < total_count; ++i) {
        const RectF* solid = SolidAt(room, i);
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
                                          GravityDirection gravity_direction,
                                          int type_a_collision_active,
                                          double now_seconds,
                                          PlayerMovementFeedback* feedback) {
    const float speed = 360.0f;
    const float gravity = 1550.0f;
    const float jump = -675.0f;

    feedback->type_a_blocked_this_frame = 0;

    int gravity_x;
    int gravity_y;
    int tangent_x;
    int tangent_y;
    MovementGravityVector(gravity_direction, &gravity_x, &gravity_y);
    MovementTangentVector(gravity_x, gravity_y, &tangent_x, &tangent_y);

    int was_grounded = player->grounded;
    int jump_started = 0;
    float gravity_speed = MovementVelocityOnAxis(player, gravity_x, gravity_y);
    float tangent_speed = move * speed;
    if (jump_active &&
        jump_pressed &&
        player->grounded) {
        gravity_speed = jump;
        player->grounded = 0;
        jump_started = 1;
    }

    if (gravity_active) {
        gravity_speed += gravity * dt;
    }
    gravity_speed = MovementClampF(gravity_speed, -900.0f, 1100.0f);
    float gravity_speed_before_resolve = gravity_speed;
    MovementSetVelocity(player, tangent_x, tangent_y, tangent_speed, gravity_x, gravity_y, gravity_speed);

    if (tangent_x != 0) {
        player->x += player->vx * dt;
    } else {
        player->y += player->vy * dt;
    }
    RectF pr = PlayerCollisionRect(player);
    ResolveAxis(player, room, type_a_collision_active, now_seconds, feedback, &pr, tangent_x, tangent_y, gravity_x, gravity_y, 0);

    player->grounded = 0;
    if (gravity_x != 0) {
        player->x += player->vx * dt;
    } else {
        player->y += player->vy * dt;
    }
    pr = PlayerCollisionRect(player);
    ResolveAxis(player, room, type_a_collision_active, now_seconds, feedback, &pr, gravity_x, gravity_y, gravity_x, gravity_y, 1);
    gravity_speed = MovementVelocityOnAxis(player, gravity_x, gravity_y);
    if (!player->grounded && gravity_speed >= 0.0f && HasGroundSupport(player, room, type_a_collision_active, gravity_x, gravity_y)) {
        player->grounded = 1;
    }

    PlayerMovementResult result;
    result.jump_started = jump_started;
    result.landed = !was_grounded && player->grounded && gravity_speed_before_resolve > 80.0f;
    return result;
}
