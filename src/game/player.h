#pragma once

#include <stdint.h>

#include "render.h"
#include "world.h"

static constexpr int PLAYER_PARTICLE_COUNT = 64;

struct Player {
    float x;
    float y;
    float vx;
    float vy;
    float visual_sx;
    float visual_sy;
    float visual_vx;
    float visual_vy;
    float face_dir;
    float jump_squash_timer;
    float run_cycle;
    int run_step;
    int grounded;
};

struct PlayerParticle {
    float x;
    float y;
    float vx;
    float vy;
    float age;
    float life;
    float size;
};

RectF PlayerCollisionRect(const Player* player);
void ResetPlayerPresentation(Player* player, PlayerParticle* particles, int particle_count);
void SpawnPlayerDeathParticles(PlayerParticle* particles, int particle_count, float x, float y, GravityDirection gravity_direction);
void UpdatePlayerParticles(PlayerParticle* particles, int particle_count, float dt, GravityDirection gravity_direction);
void UpdatePlayerPresentation(Player* player, PlayerParticle* particles, int particle_count, float dt, float move, int jump_started, int landed, GravityDirection gravity_direction);
void DrawPlayerParticles(RenderContext* render, const PlayerParticle* particles, int particle_count, uint32_t effect_color);
void DrawPlayer(RenderContext* render, const Player* player, uint32_t player_color, uint32_t face_color, GravityDirection gravity_direction);
