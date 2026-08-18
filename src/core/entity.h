#pragma once

#include "game_config.h"

struct Entity {
    float x;
    float y;
    float w;
    float h;
};

static inline float EntityCenterX(const Entity* e) {
    return e->x + e->w * 0.5f;
}

static inline float EntityCenterY(const Entity* e) {
    return e->y + e->h * 0.5f;
}
