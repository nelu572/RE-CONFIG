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

RectF PlayerCollisionRect(const Player* player) {
    RectF r;
    r.x = player->x + 8.0f;
    r.y = player->y + 5.0f;
    r.w = 38.0f;
    r.h = 50.0f;
    return r;
}

static RectF PlayerVisualBodyRect(const Player* player, GravityDirection gravity_direction) {
    RectF pr = PlayerCollisionRect(player);
    int gravity_x;
    int gravity_y;
    PlayerGravityVector(gravity_direction, &gravity_x, &gravity_y);
    float sx = PlayerClampF(player->visual_sx, 0.78f, 1.34f);
    float sy = PlayerClampF(player->visual_sy, 0.68f, 1.26f);
    float tangent_size = PLAYER_VISUAL_SIZE * sx;
    float gravity_size = PLAYER_VISUAL_SIZE * sy;
    if (tangent_size < 28.0f) tangent_size = 28.0f;
    if (gravity_size < 28.0f) gravity_size = 28.0f;

    if (gravity_y != 0) {
        float center_x = pr.x + pr.w * 0.5f;
        if (gravity_y > 0) {
            return { center_x - tangent_size * 0.5f, pr.y + pr.h - gravity_size, tangent_size, gravity_size };
        }
        return { center_x - tangent_size * 0.5f, pr.y, tangent_size, gravity_size };
    }

    float center_y = pr.y + pr.h * 0.5f;
    if (gravity_x > 0) {
        return { pr.x + pr.w - gravity_size, center_y - tangent_size * 0.5f, gravity_size, tangent_size };
    }
    return { pr.x, center_y - tangent_size * 0.5f, gravity_size, tangent_size };
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

static void SpawnPlayerParticle(PlayerParticle* particles, int particle_count, float x, float y, float vx, float vy, float life, float size) {
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
    p->size = size;
}

static void SpawnWalkParticles(Player* player, PlayerParticle* particles, int particle_count, float move_dir, GravityDirection gravity_direction) {
    PlayerVisualAxes axes = PlayerBuildVisualAxes(player, gravity_direction);
    float back = move_dir > 0.0f ? -1.0f : 1.0f;
    float x;
    float y;
    float vx;
    float vy;
    PlayerFootPoint(player, gravity_direction, back * (axes.half_tangent + 1.0f), 1.0f, &x, &y);
    PlayerParticleVelocity(gravity_direction, back * 66.0f, -16.0f, &vx, &vy);
    SpawnPlayerParticle(particles, particle_count, x, y, vx, vy, 0.11f, 1.8f);
    PlayerFootPoint(player, gravity_direction, back * (axes.half_tangent + 4.0f), 2.0f, &x, &y);
    PlayerParticleVelocity(gravity_direction, back * 86.0f, -8.0f, &vx, &vy);
    SpawnPlayerParticle(particles, particle_count, x, y, vx, vy, 0.09f, 1.1f);
    ++player->run_step;
}

static void SpawnJumpParticles(Player* player, PlayerParticle* particles, int particle_count, GravityDirection gravity_direction) {
    PlayerVisualAxes axes = PlayerBuildVisualAxes(player, gravity_direction);
    float x;
    float y;
    float vx;
    float vy;
    PlayerFootPoint(player, gravity_direction, -axes.half_tangent - 1.0f, 2.0f, &x, &y);
    PlayerParticleVelocity(gravity_direction, -58.0f, 26.0f, &vx, &vy);
    SpawnPlayerParticle(particles, particle_count, x, y, vx, vy, 0.11f, 1.6f);
    PlayerFootPoint(player, gravity_direction, axes.half_tangent + 1.0f, 2.0f, &x, &y);
    PlayerParticleVelocity(gravity_direction, 58.0f, 26.0f, &vx, &vy);
    SpawnPlayerParticle(particles, particle_count, x, y, vx, vy, 0.11f, 1.6f);
}

static void SpawnLandingParticles(Player* player, PlayerParticle* particles, int particle_count, GravityDirection gravity_direction) {
    PlayerVisualAxes axes = PlayerBuildVisualAxes(player, gravity_direction);
    static const float tangent_offsets[] = { -1.0f, -0.66f, -0.32f, 0.32f, 0.66f, 1.0f };
    static const float tangent_speeds[] = { -160.0f, -118.0f, -72.0f, 72.0f, 118.0f, 160.0f };
    static const float gravity_speeds[] = { -42.0f, -24.0f, -8.0f, -8.0f, -24.0f, -42.0f };
    static const float sizes[] = { 2.8f, 2.3f, 1.9f, 1.9f, 2.3f, 2.8f };
    for (int i = 0; i < 6; ++i) {
        float x;
        float y;
        float vx;
        float vy;
        PlayerFootPoint(player, gravity_direction, tangent_offsets[i] * (axes.half_tangent + 1.0f), 1.0f, &x, &y);
        PlayerParticleVelocity(gravity_direction, tangent_speeds[i], gravity_speeds[i], &vx, &vy);
        SpawnPlayerParticle(particles, particle_count, x, y, vx, vy, i == 0 || i == 5 ? 0.17f : (i == 1 || i == 4 ? 0.15f : 0.13f), sizes[i]);
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
        SpawnPlayerParticle(particles, particle_count, px, py, vx, vy, life, size);
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
    }
}

void UpdatePlayerParticles(PlayerParticle* particles, int particle_count, float dt, GravityDirection gravity_direction) {
    int gravity_x;
    int gravity_y;
    PlayerGravityVector(gravity_direction, &gravity_x, &gravity_y);
    for (int i = 0; i < particle_count; ++i) {
        PlayerParticle* p = &particles[i];
        if (p->age >= p->life) {
            continue;
        }
        p->age += dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->vx += (float)gravity_x * 260.0f * dt;
        p->vy += (float)gravity_y * 260.0f * dt;
    }
}

void UpdatePlayerPresentation(Player* player, PlayerParticle* particles, int particle_count, float dt, float move, int jump_started, int landed, GravityDirection gravity_direction) {
    UpdatePlayerParticles(particles, particle_count, dt, gravity_direction);
    int gravity_x;
    int gravity_y;
    int tangent_x;
    int tangent_y;
    PlayerGravityVector(gravity_direction, &gravity_x, &gravity_y);
    PlayerTangentVector(gravity_x, gravity_y, &tangent_x, &tangent_y);

    if (jump_started) {
        player->visual_sx = 1.14f;
        player->visual_sy = 0.84f;
        player->visual_vx = -6.5f;
        player->visual_vy = 8.4f;
        player->jump_squash_timer = 0.055f;
        player->run_cycle = 0.0f;
        SpawnJumpParticles(player, particles, particle_count, gravity_direction);
    }
    if (landed) {
        player->visual_sx = 1.30f;
        player->visual_sy = 0.72f;
        player->visual_vx = -4.6f;
        player->visual_vy = 6.2f;
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
        float t = PlayerSmooth01(1.0f - player->jump_squash_timer / 0.055f);
        target_sx = 1.14f + (0.84f - 1.14f) * t;
        target_sy = 0.84f + (1.22f - 0.84f) * t;
    } else if (moving_on_ground) {
        player->run_cycle += dt * 5.7f;
        while (player->run_cycle >= 1.0f) {
            player->run_cycle -= 1.0f;
            SpawnWalkParticles(player, particles, particle_count, move, gravity_direction);
        }
        target_sx = 1.075f;
        target_sy = 0.905f;
    } else if (!player->grounded) {
        if (gravity_speed < 0.0f) {
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

    player->visual_sx = PlayerSpringF(player->visual_sx, &player->visual_vx, target_sx, dt, 240.0f, 19.0f);
    player->visual_sy = PlayerSpringF(player->visual_sy, &player->visual_vy, target_sy, dt, 240.0f, 19.0f);
    player->visual_sx = PlayerClampF(player->visual_sx, 0.78f, 1.34f);
    player->visual_sy = PlayerClampF(player->visual_sy, 0.68f, 1.26f);
}

void DrawPlayerParticles(RenderContext* render, const PlayerParticle* particles, int particle_count, uint32_t effect_color) {
    uint32_t particle_color = PlayerLerpColor(effect_color, 0x00f7f0e5, 0.34f);
    for (int i = 0; i < particle_count; ++i) {
        const PlayerParticle* p = &particles[i];
        if (p->age >= p->life || p->life <= 0.0f) {
            continue;
        }
        float t = PlayerClampF(p->age / p->life, 0.0f, 1.0f);
        float fade = 1.0f - PlayerSmooth01(t);
        int radius = (int)(p->size * (1.0f - t * 0.35f) + 0.5f);
        if (radius < 1) radius = 1;
        FillCircleBlend(render, WorldX(render, p->x), WorldY(render, p->y), radius, particle_color, fade * 0.92f);
    }
}

void DrawPlayer(RenderContext* render, const Player* player, uint32_t player_color, uint32_t face_color, GravityDirection gravity_direction) {
    PlayerVisualAxes axes = PlayerBuildVisualAxes(player, gravity_direction);
    RectF body = axes.body;
    int body_w = (int)(body.w + 0.5f);
    int body_h = (int)(body.h + 0.5f);
    int x = WorldX(render, body.x);
    int y = WorldY(render, body.y);

    DrawRect(render, x, y, body_w, body_h, player_color);

    float sx = PlayerClampF(player->visual_sx, 0.78f, 1.34f);
    float sy = PlayerClampF(player->visual_sy, 0.68f, 1.26f);
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
        int eye_w = axes.gravity_x != 0 ? eye_gravity : eye_tangent;
        int eye_h = axes.gravity_x != 0 ? eye_tangent : eye_gravity;
        DrawRect(render,
                 WorldX(render, eye_center_x - (float)eye_w * 0.5f),
                 WorldY(render, eye_center_y - (float)eye_h * 0.5f),
                 eye_w,
                 eye_h,
                 face_color);
    }
}
