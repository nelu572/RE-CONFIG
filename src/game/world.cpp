#include "world.h"

static constexpr int TILE_SIZE = 40;

static constexpr float T(int tiles) {
    return (float)(tiles * TILE_SIZE);
}

static const RectF g_room00_platforms[] = {
    { T(6),  T(21), T(36), T(6) },
};

static const RectF g_room01_platforms[] = {
    { T(0),  T(0),  T(48), T(4) },
    { T(0),  T(21), T(48), T(6) },
    { T(0),  T(4),  T(3),  T(17) },
    { T(45), T(4),  T(3),  T(17) },
};

static const RectF g_room01_type_a_walls[] = {
    { T(23), T(4), T(2), T(17) },
};

static const RoomDef g_rooms[] = {
    { g_room00_platforms, (int)(sizeof(g_room00_platforms) / sizeof(g_room00_platforms[0])), 0, 0, { T(37), T(19), T(2), T(2) }, T(10), T(21) - 55.0f },
    { g_room01_platforms, (int)(sizeof(g_room01_platforms) / sizeof(g_room01_platforms[0])), g_room01_type_a_walls, (int)(sizeof(g_room01_type_a_walls) / sizeof(g_room01_type_a_walls[0])), { T(38), T(19), T(2), T(2) }, T(9), T(21) - 55.0f },
};

const RoomDef* GetRoom(int index) {
    return &g_rooms[index];
}

int RoomCount() {
    return (int)(sizeof(g_rooms) / sizeof(g_rooms[0]));
}
