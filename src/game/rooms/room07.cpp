#include "room_common.h"
#include "room_defs.h"

static const RectF g_room07_platforms[] = {
    { T(0), T(0), T(48), T(2) },
    { T(0), T(2), T(3), T(23) },
    { T(45), T(2), T(3), T(23) },
    { T(3), T(23), T(42), T(1) },
    { T(0), T(25), T(48), T(2) },
};

static const PistonDevice g_room07_pistons[] = {
    { T(18), T(12), T(5), T(1.00f), T(0.90f), T(1.00f), T(9.00f), 1.18f, 0.00f, PISTON_DOWN },
};

static const GravityBoxDef g_room07_gravity_boxes[] = {
    { { T(14.25f), T(21.50f), T(1.50f), T(1.50f) } },
};

extern const RoomDef g_room07 = {
    g_room07_platforms,
    (int)(sizeof(g_room07_platforms) / sizeof(g_room07_platforms[0])),
    0,
    0,
    0,
    0,
    { T(42), T(21), T(2), T(2) },
    T(7),
    T(22),
    { T(0), T(0), T(48), T(27) },
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