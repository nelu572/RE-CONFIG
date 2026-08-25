#pragma once

#include "../world.h"

static constexpr int ROOM_TILE_SIZE = 40;

static constexpr float T(int tiles) {
    return (float)(tiles * ROOM_TILE_SIZE);
}

static constexpr float T(float tiles) {
    return tiles * (float)ROOM_TILE_SIZE;
}

static constexpr DeleteState kDefaultDeleteState = {};