#include "player.h"

#include "math_util.h"

static float PlayerAbsF(float v) {
    return v < 0.0f ? -v : v;
}

static float PlayerClampF(float value, float lo, float hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static float PlayerSmooth01(float value) {
    value = PlayerClampF(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}

static uint32_t PlayerLerpColor(uint32_t a, uint32_t b, float t) {
    t = PlayerClampF(t, 0.0f, 1.0f);
    int ar = (int)((a >> 16) & 255);
    int ag = (int)((a >> 8) & 255);
    int ab = (int)(a & 255);
    int br = (int)((b >> 16) & 255);
    int bg = (int)((b >> 8) & 255);
    int bb = (int)(b & 255);
    int r = ar + (int)((float)(br - ar) * t + 0.5f);
    int g = ag + (int)((float)(bg - ag) * t + 0.5f);
    int bl = ab + (int)((float)(bb - ab) * t + 0.5f);
    return (uint32_t)((r << 16) | (g << 8) | bl);
}

static float PlayerEaseOutFollowF(float value, float target, float dt, float speed) {
    float amount = dt * speed;
    if (amount >= 1.0f) {
        return target;
    }
    if (amount <= 0.0f) {
        return value;
    }
    amount = 1.0f - (1.0f - amount) * (1.0f - amount);
    return value + (target - value) * amount;
}

static float PlayerSpringF(float value, float* velocity, float target, float dt, float stiffness, float damping) {
    float accel = (target - value) * stiffness - *velocity * damping;
    *velocity += accel * dt;
    value += *velocity * dt;
    if (PlayerAbsF(target - value) < 0.0005f && PlayerAbsF(*velocity) < 0.0005f) {
        value = target;
        *velocity = 0.0f;
    }
    return value;
}

static constexpr float PLAYER_VISUAL_SIZE = 40.0f;
static constexpr float PLAYER_DEFAULT_COLLISION_W = 40.0f;
static constexpr float PLAYER_DEFAULT_COLLISION_H = 40.0f;
static unsigned int g_player_particle_seed = 0x2c9277b5u;

static void PlayerGravityVector(GravityDirection direction, int* x, int* y) {
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

static void PlayerTangentVector(int gravity_x, int gravity_y, int* x, int* y) {
    if (gravity_y != 0) {
        *x = 1;
        *y = 0;
    } else {
        *x = 0;
        *y = 1;
    }
}

static float PlayerCollisionWidth(const Player* player) {
    return player->collision_w > 0.0f ? player->collision_w : PLAYER_DEFAULT_COLLISION_W;
}

static float PlayerCollisionHeight(const Player* player) {
    return player->collision_h > 0.0f ? player->collision_h : PLAYER_DEFAULT_COLLISION_H;
}

RectF PlayerCollisionRect(const Player* player) {
    RectF r;
    r.x = player->x;
    r.y = player->y;
    r.w = PlayerCollisionWidth(player);
    r.h = PlayerCollisionHeight(player);
    return r;
}

void PlayerSetCollisionSizeAnchored(Player* player, float tangent_size, float gravity_size, GravityDirection size_direction, GravityDirection anchor_direction) {
    if (tangent_size < 4.0f) tangent_size = 4.0f;
    if (gravity_size < 4.0f) gravity_size = 4.0f;

    float width = tangent_size;
    float height = gravity_size;
    if (size_direction == GRAVITY_LEFT || size_direction == GRAVITY_RIGHT) {
        width = gravity_size;
        height = tangent_size;
    }

    RectF old = PlayerCollisionRect(player);
    player->collision_w = width;
    player->collision_h = height;

    player->x = old.x + old.w * 0.5f - width * 0.5f;
    player->y = old.y + old.h * 0.5f - height * 0.5f;

    if (anchor_direction == GRAVITY_LEFT) {
        player->x = old.x;
    } else if (anchor_direction == GRAVITY_RIGHT) {
        player->x = old.x + old.w - width;
    }

    if (anchor_direction == GRAVITY_UP) {
        player->y = old.y;
    } else if (anchor_direction == GRAVITY_DOWN) {
        player->y = old.y + old.h - height;
    }

}

void PlayerSetCollisionSize(Player* player, float tangent_size, float gravity_size, GravityDirection gravity_direction) {
    PlayerSetCollisionSizeAnchored(player, tangent_size, gravity_size, gravity_direction, gravity_direction);
}

static RectF PlayerVisualBodyRect(const Player* player, GravityDirection gravity_direction) {
    RectF pr = PlayerCollisionRect(player);
    int gravity_x;
    int gravity_y;
    PlayerGravityVector(gravity_direction, &gravity_x, &gravity_y);
    float sx = PlayerClampF(player->visual_sx, 0.72f, 1.34f);
    float sy = PlayerClampF(player->visual_sy, 0.60f, 1.36f);
    float tangent_size = PLAYER_VISUAL_SIZE * sx;
    float gravity_size = PLAYER_VISUAL_SIZE * sy;
    if (tangent_size < 26.0f) tangent_size = 26.0f;
    if (gravity_size < 24.0f) gravity_size = 24.0f;

    RectF body;
    if (gravity_y != 0) {
        float center_x = pr.x + pr.w * 0.5f;
        if (gravity_y > 0) {
            body = { center_x - tangent_size * 0.5f, pr.y + pr.h - gravity_size, tangent_size, gravity_size };
        } else {
            body = { center_x - tangent_size * 0.5f, pr.y, tangent_size, gravity_size };
        }
    } else {
        float center_y = pr.y + pr.h * 0.5f;
        if (gravity_x > 0) {
            body = { pr.x + pr.w - gravity_size, center_y - tangent_size * 0.5f, gravity_size, tangent_size };
        } else {
            body = { pr.x, center_y - tangent_size * 0.5f, gravity_size, tangent_size };
        }
    }
    return body;
}

struct PlayerVisualAxes {
    RectF body;
    float center_x;
    float center_y;
    float half_tangent;
    float half_gravity;
    int tangent_x;
    int tangent_y;
    int gravity_x;
    int gravity_y;
};

static PlayerVisualAxes PlayerBuildVisualAxes(const Player* player, GravityDirection gravity_direction) {
    PlayerVisualAxes axes;
    axes.body = PlayerVisualBodyRect(player, gravity_direction);
    axes.center_x = axes.body.x + axes.body.w * 0.5f;
    axes.center_y = axes.body.y + axes.body.h * 0.5f;
    PlayerGravityVector(gravity_direction, &axes.gravity_x, &axes.gravity_y);
    PlayerTangentVector(axes.gravity_x, axes.gravity_y, &axes.tangent_x, &axes.tangent_y);
    if (axes.gravity_y != 0) {
        axes.half_tangent = axes.body.w * 0.5f;
        axes.half_gravity = axes.body.h * 0.5f;
    } else {
        axes.half_tangent = axes.body.h * 0.5f;
        axes.half_gravity = axes.body.w * 0.5f;
    }
    return axes;
}

static void PlayerFootPoint(const Player* player,
                            GravityDirection gravity_direction,
                            float tangent_offset,
                            float gravity_offset,
                            float* x,
                            float* y) {
    PlayerVisualAxes axes = PlayerBuildVisualAxes(player, gravity_direction);
    float foot_distance = axes.half_gravity + gravity_offset;
    *x = axes.center_x +
         (float)axes.tangent_x * tangent_offset +
         (float)axes.gravity_x * foot_distance;
    *y = axes.center_y +
         (float)axes.tangent_y * tangent_offset +
         (float)axes.gravity_y * foot_distance;
}

static void PlayerParticleVelocity(GravityDirection gravity_direction, float tangent_speed, float gravity_speed, float* vx, float* vy) {
    int gravity_x;
    int gravity_y;
    int tangent_x;
    int tangent_y;
    PlayerGravityVector(gravity_direction, &gravity_x, &gravity_y);
    PlayerTangentVector(gravity_x, gravity_y, &tangent_x, &tangent_y);
    *vx = (float)tangent_x * tangent_speed + (float)gravity_x * gravity_speed;
    *vy = (float)tangent_y * tangent_speed + (float)gravity_y * gravity_speed;
}

static float PlayerRandom01() {
    g_player_particle_seed = g_player_particle_seed * 1664525u + 1013904223u;
    return (float)((g_player_particle_seed >> 8) & 0xffffu) / 65535.0f;
}

static float PlayerRandomRange(float lo, float hi) {
    return lo + (hi - lo) * PlayerRandom01();
}

static void SpawnPlayerParticle(PlayerParticle* particles, int particle_count, float x, float y, float vx, float vy, float life, float size, PlayerParticleStyle style) {
    int index = -1;
    float oldest_age = -1.0f;
    for (int i = 0; i < particle_count; ++i) {
        PlayerParticle* p = &particles[i];
        if (p->age >= p->life) {
            index = i;
            break;
        }
        if (p->age > oldest_age) {
            oldest_age = p->age;
            index = i;
        }
    }
    if (index < 0) {
        return;
    }
    PlayerParticle* p = &particles[index];
    p->x = x;
    p->y = y;
    p->vx = vx;
    p->vy = vy;
    p->age = 0.0f;
    p->life = life;
    p->size = size * PlayerRandomRange(0.78f, 1.14f);
    p->style = style;
}

static float PlayerParticleDirectionBias(const Player* player, GravityDirection gravity_direction) {
    int gravity_x;
    int gravity_y;
    int tangent_x;
    int tangent_y;
    PlayerGravityVector(gravity_direction, &gravity_x, &gravity_y);
    PlayerTangentVector(gravity_x, gravity_y, &tangent_x, &tangent_y);
    float tangent_speed = player->vx * (float)tangent_x + player->vy * (float)tangent_y;
    if (PlayerAbsF(tangent_speed) < 28.0f) {
        return 0.0f;
    }
    return tangent_speed > 0.0f ? 1.0f : -1.0f;
}

static void SpawnWalkParticles(Player* player, PlayerParticle* particles, int particle_count, float move_dir, GravityDirection gravity_direction) {
    PlayerVisualAxes axes = PlayerBuildVisualAxes(player, gravity_direction);
    float back = move_dir > 0.0f ? -1.0f : 1.0f;
    float x;
    float y;
    float vx;
    float vy;
    PlayerFootPoint(player, gravity_direction, back * (axes.half_tangent + 1.0f), -3.0f, &x, &y);
    PlayerParticleVelocity(gravity_direction, back * 138.0f, -56.0f, &vx, &vy);
    SpawnPlayerParticle(particles, particle_count, x, y, vx, vy, 0.42f, 3.4f, PLAYER_PARTICLE_STYLE_PLAYER);
    ++player->run_step;
}

static void SpawnJumpParticles(Player* player, PlayerParticle* particles, int particle_count, GravityDirection gravity_direction) {
    PlayerVisualAxes axes = PlayerBuildVisualAxes(player, gravity_direction);
    float bias = PlayerParticleDirectionBias(player, gravity_direction);
    float rear = bias == 0.0f ? -1.0f : -bias;
    static const float symmetric_offsets[] = { -1.06f, -0.82f, 0.82f, 1.06f };
    static const float symmetric_speeds[] = { -220.0f, -135.0f, 135.0f, 220.0f };
    static const float gravity_speeds[] = { -72.0f, -38.0f, -38.0f, -72.0f };
    static const float sizes[] = { 2.7f, 2.1f, 2.1f, 2.7f };
    for (int i = 0; i < 4; ++i) {
        float side_offset = symmetric_offsets[i];
        float tangent_speed = symmetric_speeds[i];
        float gravity_speed = gravity_speeds[i];
        if (bias != 0.0f) {
            static const float biased_offsets[] = { 1.20f, 1.02f, 0.80f, 0.56f };
            static const float biased_speeds[] = { 252.0f, 184.0f, 112.0f, 42.0f };
            static const float biased_gravity_speeds[] = { -138.0f, -58.0f, -28.0f, -48.0f };
            side_offset = rear * biased_offsets[i];
            tangent_speed = rear * biased_speeds[i];
            gravity_speed = biased_gravity_speeds[i];
        }

        float x;
        float y;
        float vx;
        float vy;
        PlayerFootPoint(player, gravity_direction, side_offset * (axes.half_tangent + 2.0f), 2.0f, &x, &y);
        PlayerParticleVelocity(gravity_direction, tangent_speed, gravity_speed, &vx, &vy);
        SpawnPlayerParticle(particles, particle_count, x, y, vx, vy, i == 0 || i == 3 ? 0.50f : 0.42f, sizes[i], PLAYER_PARTICLE_STYLE_PLAYER);
    }
}

static void SpawnLandingParticles(Player* player, PlayerParticle* particles, int particle_count, GravityDirection gravity_direction) {
    PlayerVisualAxes axes = PlayerBuildVisualAxes(player, gravity_direction);
    float bias = PlayerParticleDirectionBias(player, gravity_direction);
    float rear = bias == 0.0f ? -1.0f : -bias;
    static const float symmetric_offsets[] = { -1.10f, -0.86f, 0.86f, 1.10f };
    static const float symmetric_speeds[] = { -122.0f, -82.0f, 82.0f, 122.0f };
    static const float gravity_speeds[] = { -78.0f, -44.0f, -44.0f, -78.0f };
    static const float sizes[] = { 2.9f, 2.1f, 2.1f, 2.9f };
    for (int i = 0; i < 4; ++i) {
        float side_offset = symmetric_offsets[i];
        float tangent_speed = symmetric_speeds[i];
        float gravity_speed = gravity_speeds[i];
        if (bias != 0.0f) {
            static const float biased_offsets[] = { 1.22f, 1.02f, 0.78f, 0.52f };
            static const float biased_speeds[] = { 150.0f, 108.0f, 62.0f, 24.0f };
            static const float biased_gravity_speeds[] = { -148.0f, -62.0f, -28.0f, -46.0f };
            side_offset = rear * biased_offsets[i];
            tangent_speed = rear * biased_speeds[i];
            gravity_speed = biased_gravity_speeds[i];
        }

        float x;
        float y;
        float vx;
        float vy;
        PlayerFootPoint(player, gravity_direction, side_offset * (axes.half_tangent + 1.0f), -4.0f, &x, &y);
        PlayerParticleVelocity(gravity_direction, tangent_speed, gravity_speed, &vx, &vy);
        SpawnPlayerParticle(particles, particle_count, x, y, vx, vy, i == 0 || i == 3 ? 0.46f : 0.36f, sizes[i], PLAYER_PARTICLE_STYLE_PLAYER);
    }
}

void SpawnPlayerDeathParticles(PlayerParticle* particles, int particle_count, float x, float y, GravityDirection gravity_direction) {
    int gravity_x;
    int gravity_y;
    int tangent_x;
    int tangent_y;
    PlayerGravityVector(gravity_direction, &gravity_x, &gravity_y);
    PlayerTangentVector(gravity_x, gravity_y, &tangent_x, &tangent_y);

    for (int i = 0; i < particle_count; ++i) {
        float angle = ((float)i / (float)particle_count) * 6.2831853f;
        float wave_x = CosApprox(angle);
        float wave_y = SinApprox(angle);
        float burst = PlayerRandomRange(220.0f, 760.0f);
        float tangent_speed = wave_x * burst + PlayerRandomRange(-95.0f, 95.0f);
        float gravity_speed = wave_y * burst - 120.0f + PlayerRandomRange(-120.0f, 140.0f);
        float vx = (float)tangent_x * tangent_speed + (float)gravity_x * gravity_speed;
        float vy = (float)tangent_y * tangent_speed + (float)gravity_y * gravity_speed;
        float px = x + PlayerRandomRange(-18.0f, 18.0f);
        float py = y + PlayerRandomRange(-18.0f, 18.0f);
        float life = PlayerRandomRange(0.32f, 0.72f);
        float size = PlayerRandomRange(2.0f, 5.4f);
        SpawnPlayerParticle(particles, particle_count, px, py, vx, vy, life, size, PLAYER_PARTICLE_STYLE_PLAYER);
    }
}

void SpawnWalkerEnemyCrushParticles(PlayerParticle* particles,
                                    int particle_count,
                                    float x,
                                    float y,
                                    GravityDirection gravity_direction) {
    int gravity_x;
    int gravity_y;
    int tangent_x;
    int tangent_y;
    PlayerGravityVector(gravity_direction, &gravity_x, &gravity_y);
    PlayerTangentVector(gravity_x, gravity_y, &tangent_x, &tangent_y);

    static constexpr int piece_count = 12;
    for (int i = 0; i < piece_count; ++i) {
        float angle = ((float)i / (float)piece_count) * 6.2831853f;
        float tangent_speed = CosApprox(angle) * PlayerRandomRange(95.0f, 220.0f);
        float gravity_speed = SinApprox(angle) * PlayerRandomRange(70.0f, 170.0f) - 48.0f;
        float vx = (float)tangent_x * tangent_speed + (float)gravity_x * gravity_speed;
        float vy = (float)tangent_y * tangent_speed + (float)gravity_y * gravity_speed;
        float life = PlayerRandomRange(0.20f, 0.42f);
        float size = PlayerRandomRange(1.4f, 3.0f);
        SpawnPlayerParticle(particles, particle_count, x, y, vx, vy, life, size, PLAYER_PARTICLE_STYLE_WALKER_ENEMY_CRUSH);
    }
}

void SpawnCheckpointParticles(PlayerParticle* particles,
                              int particle_count,
                              float x,
                              float y,
                              GravityDirection gravity_direction) {
    int gravity_x;
    int gravity_y;
    int tangent_x;
    int tangent_y;
    PlayerGravityVector(gravity_direction, &gravity_x, &gravity_y);
    PlayerTangentVector(gravity_x, gravity_y, &tangent_x, &tangent_y);

    static constexpr int piece_count = 18;
    for (int i = 0; i < piece_count; ++i) {
        float angle = ((float)i / (float)piece_count) * 6.2831853f;
        float tangent_speed = CosApprox(angle) * PlayerRandomRange(105.0f, 250.0f);
        float gravity_speed = SinApprox(angle) * PlayerRandomRange(95.0f, 225.0f) - 72.0f;
        float vx = (float)tangent_x * tangent_speed + (float)gravity_x * gravity_speed;
        float vy = (float)tangent_y * tangent_speed + (float)gravity_y * gravity_speed;
        float px = x + PlayerRandomRange(-5.0f, 5.0f);
        float py = y + PlayerRandomRange(-5.0f, 5.0f);
        float life = PlayerRandomRange(0.42f, 0.78f);
        float size = PlayerRandomRange(1.8f, 4.2f);
        SpawnPlayerParticle(particles, particle_count, px, py, vx, vy, life, size, PLAYER_PARTICLE_STYLE_CHECKPOINT);
    }
}

void ResetPlayerPresentation(Player* player, PlayerParticle* particles, int particle_count) {
    player->visual_sx = 1.0f;
    player->visual_sy = 1.0f;
    player->visual_vx = 0.0f;
    player->visual_vy = 0.0f;
    player->face_dir = 1.0f;
    player->jump_squash_timer = 0.0f;
    player->run_cycle = 0.0f;
    player->run_step = 0;
    for (int i = 0; i < particle_count; ++i) {
        particles[i].x = 0.0f;
        particles[i].y = 0.0f;
        particles[i].vx = 0.0f;
        particles[i].vy = 0.0f;
        particles[i].age = 0.0f;
        particles[i].life = 0.0f;
        particles[i].size = 0.0f;
        particles[i].style = PLAYER_PARTICLE_STYLE_PLAYER;
    }
}

static void BouncePlayerParticleOnPlatforms(PlayerParticle* p, const RoomDef* room, float prev_x, float prev_y, int gravity_x, int gravity_y) {
    if (!room) {
        return;
    }

    float radius = p->size + 0.75f;
    for (int i = 0; i < room->platform_count; ++i) {
        const RectF* solid = &room->platforms[i];
        if (gravity_y > 0) {
            float contact = solid->y - radius;
            int tangent_overlap = p->x >= solid->x - radius && p->x <= solid->x + solid->w + radius;
            if (tangent_overlap && p->vy > 0.0f && prev_y <= contact && p->y >= contact) {
                float rebound = PlayerAbsF(p->vy) * 0.54f;
                if (rebound < 105.0f) rebound = 105.0f;
                p->y = contact;
                p->vy = -rebound;
                p->vx *= 0.88f;
                return;
            }
        } else if (gravity_y < 0) {
            float contact = solid->y + solid->h + radius;
            int tangent_overlap = p->x >= solid->x - radius && p->x <= solid->x + solid->w + radius;
            if (tangent_overlap && p->vy < 0.0f && prev_y >= contact && p->y <= contact) {
                float rebound = PlayerAbsF(p->vy) * 0.54f;
                if (rebound < 105.0f) rebound = 105.0f;
                p->y = contact;
                p->vy = rebound;
                p->vx *= 0.88f;
                return;
            }
        } else if (gravity_x > 0) {
            float contact = solid->x - radius;
            int tangent_overlap = p->y >= solid->y - radius && p->y <= solid->y + solid->h + radius;
            if (tangent_overlap && p->vx > 0.0f && prev_x <= contact && p->x >= contact) {
                float rebound = PlayerAbsF(p->vx) * 0.54f;
                if (rebound < 105.0f) rebound = 105.0f;
                p->x = contact;
                p->vx = -rebound;
                p->vy *= 0.88f;
                return;
            }
        } else if (gravity_x < 0) {
            float contact = solid->x + solid->w + radius;
            int tangent_overlap = p->y >= solid->y - radius && p->y <= solid->y + solid->h + radius;
            if (tangent_overlap && p->vx < 0.0f && prev_x >= contact && p->x <= contact) {
                float rebound = PlayerAbsF(p->vx) * 0.54f;
                if (rebound < 105.0f) rebound = 105.0f;
                p->x = contact;
                p->vx = rebound;
                p->vy *= 0.88f;
                return;
            }
        }
    }
}

void UpdatePlayerParticles(PlayerParticle* particles, int particle_count, const RoomDef* room, float dt, GravityDirection gravity_direction) {
    int gravity_x;
    int gravity_y;
    PlayerGravityVector(gravity_direction, &gravity_x, &gravity_y);
    for (int i = 0; i < particle_count; ++i) {
        PlayerParticle* p = &particles[i];
        if (p->age >= p->life) {
            continue;
        }
        p->age += dt;
        p->vx += (float)gravity_x * 980.0f * dt;
        p->vy += (float)gravity_y * 980.0f * dt;
        float prev_x = p->x;
        float prev_y = p->y;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        BouncePlayerParticleOnPlatforms(p, room, prev_x, prev_y, gravity_x, gravity_y);
    }
}

void UpdatePlayerPresentation(Player* player, PlayerParticle* particles, int particle_count, const RoomDef* room, float dt, float move, int jump_started, int landed, int stretch_blocked, GravityDirection gravity_direction) {
    UpdatePlayerParticles(particles, particle_count, room, dt, gravity_direction);
    int gravity_x;
    int gravity_y;
    int tangent_x;
    int tangent_y;
    PlayerGravityVector(gravity_direction, &gravity_x, &gravity_y);
    PlayerTangentVector(gravity_x, gravity_y, &tangent_x, &tangent_y);


    float collision_gravity_size = gravity_x != 0 ? PlayerCollisionWidth(player) : PlayerCollisionHeight(player);
    float collision_tangent_size = gravity_x != 0 ? PlayerCollisionHeight(player) : PlayerCollisionWidth(player);
    float collision_height_scale = collision_gravity_size / PLAYER_DEFAULT_COLLISION_H;
    float collision_tangent_scale = collision_tangent_size / PLAYER_DEFAULT_COLLISION_W;
    int soft_visual = PlayerAbsF(collision_height_scale - 1.0f) > 0.01f || PlayerAbsF(collision_tangent_scale - 1.0f) > 0.01f;

    if (jump_started) {
        if (soft_visual) {
            player->visual_vx *= 0.35f;
            player->visual_vy *= 0.35f;
            player->jump_squash_timer = stretch_blocked ? 0.0f : 0.095f;
        } else {
            player->visual_sx = 1.14f;
            player->visual_sy = 0.84f;
            player->visual_vx = -6.5f;
            player->visual_vy = 8.4f;
            player->jump_squash_timer = 0.055f;
        }
        player->run_cycle = 0.0f;
        SpawnJumpParticles(player, particles, particle_count, gravity_direction);
    }
    if (landed) {
        if (soft_visual) {
            player->visual_sx = PlayerClampF(player->visual_sx, 1.08f, 1.18f);
            player->visual_sy = PlayerClampF(player->visual_sy, 0.80f, 0.94f);
            player->visual_vx = 0.9f;
            player->visual_vy = -0.7f;
        } else {
            player->visual_sx = 1.30f;
            player->visual_sy = 0.72f;
            player->visual_vx = -4.6f;
            player->visual_vy = 6.2f;
        }
        player->jump_squash_timer = 0.0f;
        player->run_cycle = 0.0f;
        SpawnLandingParticles(player, particles, particle_count, gravity_direction);
    }

    float target_sx = 1.0f;
    float target_sy = 1.0f;
    float tangent_speed = player->vx * (float)tangent_x + player->vy * (float)tangent_y;
    float gravity_speed = player->vx * (float)gravity_x + player->vy * (float)gravity_y;
    int moving_on_ground = player->grounded && PlayerAbsF(move) > 0.01f && PlayerAbsF(tangent_speed) > 0.01f;
    float target_face_dir = PlayerAbsF(move) > 0.01f ? move : 0.0f;
    player->face_dir = PlayerEaseOutFollowF(player->face_dir, target_face_dir, dt, PlayerAbsF(move) > 0.01f ? 14.0f : 5.0f);
    if (player->jump_squash_timer > 0.0f) {
        player->jump_squash_timer -= dt;
        if (player->jump_squash_timer < 0.0f) {
            player->jump_squash_timer = 0.0f;
        }
        float timer_seconds = soft_visual ? 0.095f : 0.055f;
        float elapsed = timer_seconds - player->jump_squash_timer;
        if (soft_visual) {
            const float delay_seconds = 0.012f;
            const float stretch_seconds = 0.040f;
            if (elapsed < delay_seconds) {
                target_sx = 0.99f;
                target_sy = 1.01f;
            } else if (elapsed < delay_seconds + stretch_seconds) {
                float stretch_t = PlayerSmooth01((elapsed - delay_seconds) / stretch_seconds);
                target_sx = 0.99f + (0.88f - 0.99f) * stretch_t;
                target_sy = 1.01f + (1.18f - 1.01f) * stretch_t;
            } else {
                float release_t = PlayerSmooth01((elapsed - delay_seconds - stretch_seconds) / (timer_seconds - delay_seconds - stretch_seconds));
                target_sx = 0.88f + (0.96f - 0.88f) * release_t;
                target_sy = 1.18f + (1.08f - 1.18f) * release_t;
            }
        } else {
            float t = PlayerSmooth01(1.0f - player->jump_squash_timer / timer_seconds);
            target_sx = 1.14f + (0.84f - 1.14f) * t;
            target_sy = 0.84f + (1.22f - 0.84f) * t;
        }
    } else if (moving_on_ground) {
        player->run_cycle += dt * 5.7f;
        while (player->run_cycle >= 1.0f) {
            player->run_cycle -= 1.0f;
            SpawnWalkParticles(player, particles, particle_count, move, gravity_direction);
        }
        target_sx = 1.075f;
        target_sy = 0.905f;
    } else if (!player->grounded) {
        if (soft_visual) {
            target_sx = 0.98f;
            target_sy = 1.04f;
        } else if (gravity_speed < 0.0f) {
            float rise = PlayerClampF((-gravity_speed) / 675.0f, 0.0f, 1.0f);
            target_sx = 1.0f - rise * 0.055f;
            target_sy = 1.0f + rise * 0.115f;
        } else {
            float fall = PlayerClampF(gravity_speed / 900.0f, 0.0f, 1.0f);
            target_sx = 1.0f - fall * 0.035f;
            target_sy = 1.0f + fall * 0.075f;
        }
        player->run_cycle = 0.0f;
    } else {
        player->run_cycle = 0.0f;
    }

    if (soft_visual && player->grounded && player->jump_squash_timer <= 0.0f) {
        target_sx = 1.16f;
        target_sy = 0.80f;
    }

    int soft_input = soft_visual && PlayerAbsF(move) > 0.01f;
    float sx_stiffness = 240.0f;
    float sx_damping = 19.0f;
    float sy_stiffness = 240.0f;
    float sy_damping = 19.0f;
    if (soft_visual && target_sx > player->visual_sx) {
        sx_stiffness = soft_input ? 170.0f : 58.0f;
        sx_damping = soft_input ? 18.0f : 12.5f;
    }
    if (soft_visual && target_sy < player->visual_sy) {
        sy_stiffness = soft_input ? 160.0f : 38.0f;
        sy_damping = soft_input ? 18.0f : 10.5f;
    }
    player->visual_sx = PlayerSpringF(player->visual_sx, &player->visual_vx, target_sx, dt, sx_stiffness, sx_damping);
    player->visual_sy = PlayerSpringF(player->visual_sy, &player->visual_vy, target_sy, dt, sy_stiffness, sy_damping);
    player->visual_sx = PlayerClampF(player->visual_sx, 0.72f, 1.34f);
    player->visual_sy = PlayerClampF(player->visual_sy, 0.60f, 1.36f);
}

void DrawPlayerParticles(RenderContext* render,
                         const PlayerParticle* particles,
                         int particle_count,
                         uint32_t player_color,
                         uint32_t enemy_crush_color,
                         uint32_t checkpoint_color) {
    for (int i = 0; i < particle_count; ++i) {
        const PlayerParticle* p = &particles[i];
        if (p->age >= p->life || p->life <= 0.0f) {
            continue;
        }
        float t = PlayerClampF(p->age / p->life, 0.0f, 1.0f);
        float fade = 1.0f - PlayerSmooth01(t);
        float scale = 1.0f - t * 0.62f;
        int radius = WorldW(render, p->size * scale);
        if (radius < 1 || fade <= 0.05f) {
            continue;
        }
        uint32_t color = player_color;
        if (p->style == PLAYER_PARTICLE_STYLE_WALKER_ENEMY_CRUSH) {
            color = enemy_crush_color;
        } else if (p->style == PLAYER_PARTICLE_STYLE_CHECKPOINT) {
            color = checkpoint_color;
        }
        FillCircleBlend(render, WorldX(render, p->x), WorldY(render, p->y), radius, color, fade);
    }
}

void DrawPlayer(RenderContext* render, const Player* player, uint32_t player_color, uint32_t face_color, GravityDirection gravity_direction) {
    PlayerVisualAxes axes = PlayerBuildVisualAxes(player, gravity_direction);
    RectF body = axes.body;
    int body_w = WorldW(render, body.w);
    int body_h = WorldH(render, body.h);
    int x = WorldX(render, body.x);
    int y = WorldY(render, body.y);

    DrawRect(render, x, y, body_w, body_h, player_color);

    float sx = PlayerClampF(player->visual_sx, 0.72f, 1.34f);
    float sy = PlayerClampF(player->visual_sy, 0.60f, 1.36f);
    int eye_tangent = (int)(4.0f * sx + 0.5f);
    int eye_gravity = (int)(9.0f * sy + 0.5f);
    if (eye_tangent < 4) eye_tangent = 4;
    if (eye_gravity < 7) eye_gravity = 7;
    int eye_gap = (int)(axes.half_tangent * 2.0f * 0.18f + 0.5f);
    if (eye_gap < 7) eye_gap = 7;
    float look_offset = axes.half_tangent * 0.23f * PlayerClampF(player->face_dir, -1.0f, 1.0f);
    float eye_separation = (float)(eye_tangent + eye_gap) * 0.5f;
    float head_offset = -axes.half_gravity * 0.42f;

    for (int i = 0; i < 2; ++i) {
        float side = i == 0 ? -1.0f : 1.0f;
        float eye_center_x = axes.center_x +
                             (float)axes.gravity_x * head_offset +
                             (float)axes.tangent_x * (look_offset + side * eye_separation);
        float eye_center_y = axes.center_y +
                             (float)axes.gravity_y * head_offset +
                             (float)axes.tangent_y * (look_offset + side * eye_separation);
        int eye_w = WorldW(render, (float)(axes.gravity_x != 0 ? eye_gravity : eye_tangent));
        int eye_h = WorldH(render, (float)(axes.gravity_x != 0 ? eye_tangent : eye_gravity));
        DrawRect(render,
                 WorldX(render, eye_center_x) - eye_w / 2,
                 WorldY(render, eye_center_y) - eye_h / 2,
                 eye_w,
                 eye_h,
                 face_color);
    }
}
