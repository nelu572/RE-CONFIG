#include "room_common.h"
#include "room_defs.h"

static const RectF g_room02_platforms[] = {
    { T(0), T(0), T(19), T(12) },
    { T(50), T(7), T(8), T(2) },
    { T(0), T(12), T(3), T(13) },
    { T(0), T(25), T(25), T(2) },
    { T(31), T(25), T(27), T(2) },
};

static const RectF g_room02_type_a_walls[] = {
    { T(45), T(0), T(13), T(2) },
    { T(17), T(12), T(2), T(13) },
};

extern const RoomDef g_room02 = {
    g_room02_platforms,
    (int)(sizeof(g_room02_platforms) / sizeof(g_room02_platforms[0])),
    g_room02_type_a_walls,
    (int)(sizeof(g_room02_type_a_walls) / sizeof(g_room02_type_a_walls[0])),
    0,
    0,
    { T(55), T(5), T(2), T(2) },
    T(7),
    T(25) - 40.0f,
    { T(0), T(0), T(58), T(27) },
    T(4),
    GRAVITY_DOWN,
    kDefaultDeleteState,
};