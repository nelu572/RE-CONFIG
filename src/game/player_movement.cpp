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

static void RegisterTypeAContact(PlayerMovementFeedback* feedback, double now_seconds, int blocked) {
    if (!feedback->type_a_contacted) {
        feedback->type_a_bump_until = now_seconds + 0.22;
    }
    feedback->type_a_contacted = 1;
    if (blocked) {
        feedback->type_a_blocked_this_frame = 1;
    }
}

static void ResolveHorizontal(Player* player, const RoomDef* room, int type_a_collision_active, double now_seconds, PlayerMovementFeedback* feedback, RectF* p) {
    int platform_count = PlatformSolidCount(room);
    int total_count = TotalSolidCount(room, type_a_collision_active);
    for (int i = 0; i < total_count; ++i) {
        const RectF* solid = SolidAt(room, i);
        if (!RectsOverlap(p, solid)) continue;
        if (i >= platform_count) {
            RegisterTypeAContact(feedback, now_seconds, 1);
        }
        if (player->vx > 0.0f) {
            p->x = solid->x - p->w;
        } else if (player->vx < 0.0f) {
            p->x = solid->x + solid->w;
        }
        player->x = p->x - 8.0f;
        player->vx = 0.0f;
    }
}

static void ResolveVertical(Player* player, const RoomDef* room, int type_a_collision_active, double now_seconds, PlayerMovementFeedback* feedback, RectF* p) {
    int platform_count = PlatformSolidCount(room);
    int total_count = TotalSolidCount(room, type_a_collision_active);
    player->grounded = 0;
    for (int i = 0; i < total_count; ++i) {
        const RectF* solid = SolidAt(room, i);
        if (!RectsOverlap(p, solid)) continue;
        if (i >= platform_count) {
            RegisterTypeAContact(feedback, now_seconds, 0);
        }
        if (player->vy > 0.0f) {
            p->y = solid->y - p->h;
            player->grounded = 1;
        } else if (player->vy < 0.0f) {
            p->y = solid->y + solid->h;
        }
        player->y = p->y - 5.0f;
        player->vy = 0.0f;
    }
}

static int HasGroundSupport(const Player* player, const RoomDef* room, int type_a_collision_active) {
    RectF probe = PlayerCollisionRect(player);
    probe.y += 1.0f;
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
                                          int type_a_collision_active,
                                          double now_seconds,
                                          PlayerMovementFeedback* feedback) {
    const float speed = 360.0f;
    const float gravity = 1550.0f;
    const float jump = -675.0f;

    feedback->type_a_blocked_this_frame = 0;

    int was_grounded = player->grounded;
    int jump_started = 0;
    player->vx = move * speed;
    if (jump_active &&
        jump_pressed &&
        player->grounded) {
        player->vy = jump;
        player->grounded = 0;
        jump_started = 1;
    }

    if (gravity_active) {
        player->vy += gravity * dt;
    }
    player->vy = MovementClampF(player->vy, -900.0f, 1100.0f);
    float vertical_speed_before_resolve = player->vy;

    player->x += player->vx * dt;
    RectF pr = PlayerCollisionRect(player);
    ResolveHorizontal(player, room, type_a_collision_active, now_seconds, feedback, &pr);

    player->y += player->vy * dt;
    pr = PlayerCollisionRect(player);
    ResolveVertical(player, room, type_a_collision_active, now_seconds, feedback, &pr);
    if (!player->grounded && player->vy >= 0.0f && HasGroundSupport(player, room, type_a_collision_active)) {
        player->grounded = 1;
    }

    PlayerMovementResult result;
    result.jump_started = jump_started;
    result.landed = !was_grounded && player->grounded && vertical_speed_before_resolve > 80.0f;
    return result;
}
