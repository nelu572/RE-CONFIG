#include "player.h"

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

RectF PlayerCollisionRect(const Player* player) {
    RectF r;
    r.x = player->x + 8.0f;
    r.y = player->y + 5.0f;
    r.w = 38.0f;
    r.h = 50.0f;
    return r;
}

static RectF PlayerVisualBodyRect(const Player* player) {
    RectF pr = PlayerCollisionRect(player);
    float sx = PlayerClampF(player->visual_sx, 0.78f, 1.34f);
    float sy = PlayerClampF(player->visual_sy, 0.68f, 1.26f);
    float body_w = PLAYER_VISUAL_SIZE * sx;
    float body_h = PLAYER_VISUAL_SIZE * sy;
    if (body_w < 28.0f) body_w = 28.0f;
    if (body_h < 28.0f) body_h = 28.0f;
    float center_x = pr.x + pr.w * 0.5f;
    float foot_y = pr.y + pr.h;
    return { center_x - body_w * 0.5f, foot_y - body_h, body_w, body_h };
}

static RectF PlayerFootRect(const Player* player) {
    RectF body = PlayerVisualBodyRect(player);
    return { body.x, body.y + body.h - 1.0f, body.w, 1.0f };
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

static void SpawnWalkParticles(Player* player, PlayerParticle* particles, int particle_count, float move_dir) {
    RectF feet = PlayerFootRect(player);
    float back = move_dir > 0.0f ? -1.0f : 1.0f;
    float edge_x = back < 0.0f ? feet.x - 1.0f : feet.x + feet.w + 1.0f;
    SpawnPlayerParticle(particles, particle_count, edge_x, feet.y + 1.0f, back * 66.0f, -16.0f, 0.11f, 1.8f);
    SpawnPlayerParticle(particles, particle_count, edge_x + back * 3.0f, feet.y + 2.0f, back * 86.0f, -8.0f, 0.09f, 1.1f);
    ++player->run_step;
}

static void SpawnJumpParticles(Player* player, PlayerParticle* particles, int particle_count) {
    RectF feet = PlayerFootRect(player);
    float left_x = feet.x - 1.0f;
    float right_x = feet.x + feet.w + 1.0f;
    float y = feet.y + 2.0f;
    SpawnPlayerParticle(particles, particle_count, left_x, y, -58.0f, 26.0f, 0.11f, 1.6f);
    SpawnPlayerParticle(particles, particle_count, right_x, y, 58.0f, 26.0f, 0.11f, 1.6f);
}

static void SpawnLandingParticles(Player* player, PlayerParticle* particles, int particle_count) {
    RectF feet = PlayerFootRect(player);
    float left_x = feet.x - 1.0f;
    float right_x = feet.x + feet.w + 1.0f;
    float y = feet.y + 1.0f;
    SpawnPlayerParticle(particles, particle_count, left_x, y, -160.0f, -42.0f, 0.17f, 2.8f);
    SpawnPlayerParticle(particles, particle_count, left_x + 6.0f, y, -118.0f, -24.0f, 0.15f, 2.3f);
    SpawnPlayerParticle(particles, particle_count, left_x + 12.0f, y, -72.0f, -8.0f, 0.13f, 1.9f);
    SpawnPlayerParticle(particles, particle_count, right_x - 12.0f, y, 72.0f, -8.0f, 0.13f, 1.9f);
    SpawnPlayerParticle(particles, particle_count, right_x - 6.0f, y, 118.0f, -24.0f, 0.15f, 2.3f);
    SpawnPlayerParticle(particles, particle_count, right_x, y, 160.0f, -42.0f, 0.17f, 2.8f);
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

static void UpdatePlayerParticles(PlayerParticle* particles, int particle_count, float dt) {
    for (int i = 0; i < particle_count; ++i) {
        PlayerParticle* p = &particles[i];
        if (p->age >= p->life) {
            continue;
        }
        p->age += dt;
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->vy += 260.0f * dt;
    }
}

void UpdatePlayerPresentation(Player* player, PlayerParticle* particles, int particle_count, float dt, float move, int jump_started, int landed) {
    UpdatePlayerParticles(particles, particle_count, dt);

    if (jump_started) {
        player->visual_sx = 1.14f;
        player->visual_sy = 0.84f;
        player->visual_vx = -6.5f;
        player->visual_vy = 8.4f;
        player->jump_squash_timer = 0.055f;
        player->run_cycle = 0.0f;
        SpawnJumpParticles(player, particles, particle_count);
    }
    if (landed) {
        player->visual_sx = 1.30f;
        player->visual_sy = 0.72f;
        player->visual_vx = -4.6f;
        player->visual_vy = 6.2f;
        player->jump_squash_timer = 0.0f;
        player->run_cycle = 0.0f;
        SpawnLandingParticles(player, particles, particle_count);
    }

    float target_sx = 1.0f;
    float target_sy = 1.0f;
    int moving_on_ground = player->grounded && PlayerAbsF(move) > 0.01f && PlayerAbsF(player->vx) > 0.01f;
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
            SpawnWalkParticles(player, particles, particle_count, move);
        }
        target_sx = 1.075f;
        target_sy = 0.905f;
    } else if (!player->grounded) {
        if (player->vy < 0.0f) {
            float rise = PlayerClampF((-player->vy) / 675.0f, 0.0f, 1.0f);
            target_sx = 1.0f - rise * 0.055f;
            target_sy = 1.0f + rise * 0.115f;
        } else {
            float fall = PlayerClampF(player->vy / 900.0f, 0.0f, 1.0f);
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
        FillCircleBlend(render, (int)(p->x + 0.5f), (int)(p->y + 0.5f), radius, particle_color, fade * 0.92f);
    }
}

void DrawPlayer(RenderContext* render, const Player* player, uint32_t player_color, uint32_t face_color) {
    RectF body = PlayerVisualBodyRect(player);
    RectF pr = PlayerCollisionRect(player);
    float center_x = pr.x + pr.w * 0.5f;
    int body_w = (int)(body.w + 0.5f);
    int body_h = (int)(body.h + 0.5f);
    int x = (int)(body.x + 0.5f);
    int y = (int)(body.y + 0.5f);

    DrawRect(render, x, y, body_w, body_h, player_color);

    float sx = PlayerClampF(player->visual_sx, 0.78f, 1.34f);
    float sy = PlayerClampF(player->visual_sy, 0.68f, 1.26f);
    int eye_w = (int)(4.0f * sx + 0.5f);
    int eye_h = (int)(9.0f * sy + 0.5f);
    if (eye_w < 4) eye_w = 4;
    if (eye_h < 7) eye_h = 7;
    int center_i = (int)(center_x + 0.5f);
    int eye_gap = (int)((float)body_w * 0.18f + 0.5f);
    if (eye_gap < 7) eye_gap = 7;
    int eyes_w = eye_w * 2 + eye_gap;
    int look_offset = (int)((float)body_w * 0.115f * PlayerClampF(player->face_dir, -1.0f, 1.0f));
    int eye_y = y + (int)((float)body_h * 0.29f);
    int left_eye_x = center_i + look_offset - eyes_w / 2;
    int right_eye_x = left_eye_x + eye_w + eye_gap;
    DrawRect(render, left_eye_x, eye_y, eye_w, eye_h, face_color);
    DrawRect(render, right_eye_x, eye_y, eye_w, eye_h, face_color);
}
