#include "room_common.h"
#include "room_defs.h"

static const RectF g_room04_platforms[] = {
    { T(0), T(0), T(68), T(8) },
    { T(0), T(19), T(68), T(8) },
    { T(0), T(8), T(3), T(11) },
    { T(65), T(8), T(3), T(11) },
};

static const PistonDevice g_room04_pistons[] = {
    { T(18), T(8), T(5), T(1.00f), T(1.05f), T(1.00f), T(9.00f), 1.10f, 0.00f },
    { T(23), T(8), T(5), T(1.00f), T(1.05f), T(1.00f), T(9.00f), 1.10f, 0.06f },
    { T(30), T(8), T(5), T(1.00f), T(1.05f), T(1.00f), T(9.00f), 1.10f, 0.24f },
    { T(35), T(8), T(5), T(1.00f), T(1.05f), T(1.00f), T(9.00f), 1.10f, 0.30f },
    { T(42), T(8), T(5), T(1.00f), T(1.05f), T(1.00f), T(9.00f), 1.10f, 0.48f },
    { T(47), T(8), T(5), T(1.00f), T(1.05f), T(1.00f), T(9.00f), 1.10f, 0.54f },
};

extern const RoomDef g_room04 = {
    g_room04_platforms,
    (int)(sizeof(g_room04_platforms) / sizeof(g_room04_platforms[0])),
    0,
    0,
    0,
    0,
    { T(61), T(17), T(2), T(2) },
    T(6),
    T(19) - 40.0f,
    { T(0), T(0), T(68), T(27) },
    T(4),
    GRAVITY_DOWN,
    kDefaultDeleteState,
    g_room04_pistons,
    (int)(sizeof(g_room04_pistons) / sizeof(g_room04_pistons[0])),
};

