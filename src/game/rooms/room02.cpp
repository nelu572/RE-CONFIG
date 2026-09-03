#include "room_common.h"
#include "room_defs.h"

static const RectF g_room02_platforms[] = {
    { T(0), T(0), T(48), T(4) },
    { T(0), T(21), T(48), T(6) },
    { T(0), T(4), T(3), T(17) },
    { T(45), T(4), T(3), T(17) },
};

static const RectF g_room02_type_a_walls[] = {
    { T(23), T(4), T(2), T(17) },
};

extern const RoomDef g_room02 = {
    g_room02_platforms,
    (int)(sizeof(g_room02_platforms) / sizeof(g_room02_platforms[0])),
    g_room02_type_a_walls,
    (int)(sizeof(g_room02_type_a_walls) / sizeof(g_room02_type_a_walls[0])),
    0,
    0,
    { T(38), T(19), T(2), T(2) },
    T(9),
    T(21) - 40.0f,
    { T(0), T(0), T(48), T(27) },
    T(4),
    GRAVITY_DOWN,
    kDefaultDeleteState,
};