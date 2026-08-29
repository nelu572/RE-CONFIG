#include "room_common.h"
#include "room_defs.h"

static const RectF g_room07_platforms[] = {
    { T(0), T(0), T(67), T(2) },
    { T(0), T(2), T(3), T(6) },
    { T(0), T(8), T(2), T(5) },
    { T(0), T(13), T(3), T(12) },
    { T(64), T(2), T(3), T(23) },
    { T(0), T(25), T(67), T(2) },
};

static const PistonDevice g_room07_pistons[] = {
    { T(1), T(8), T(5), T(1.00f), T(0.90f), T(1.00f), T(8.00f), 1.18f, 0.00f, PISTON_RIGHT },
    { T(26), T(20), T(5), T(1.00f), T(0.90f), T(1.00f), T(22.00f), 1.18f, 0.00f, PISTON_LEFT },
};

static const GravityBoxDef g_room07_gravity_boxes[] = {
    { { T(6.75f), T(22.75f), T(1.50f), T(1.50f) } },
};

extern const RoomDef g_room07 = {
    g_room07_platforms,
    (int)(sizeof(g_room07_platforms) / sizeof(g_room07_platforms[0])),
    0,
    0,
    0,
    0,
    { T(60), T(23), T(2), T(2) },
    T(7),
    T(21),
    { T(0), T(0), T(67), T(27) },
    T(4),
    GRAVITY_DOWN,
    kDefaultDeleteState,
    g_room07_pistons,
    (int)(sizeof(g_room07_pistons) / sizeof(g_room07_pistons[0])),
    g_room07_gravity_boxes,
    (int)(sizeof(g_room07_gravity_boxes) / sizeof(g_room07_gravity_boxes[0])),
    0,
    0,
    0,
    0,
    0,
};