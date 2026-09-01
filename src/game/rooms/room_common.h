#pragma once

#include "../world.h"

static constexpr int ROOM_TILE_SIZE = 40;

static constexpr float T(int tiles) {
    return (float)(tiles * ROOM_TILE_SIZE);
}

static constexpr float T(float tiles) {
    return tiles * (float)ROOM_TILE_SIZE;
}

static constexpr float kDefaultGravityBoxSize = T(1.5f);

static constexpr RectF DefaultGravityBoxAt(float x, float y) {
    return { x, y, kDefaultGravityBoxSize, kDefaultGravityBoxSize };
}
static constexpr DeleteState kDefaultDeleteState = {};