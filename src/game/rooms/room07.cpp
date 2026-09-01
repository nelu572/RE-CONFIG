#include "room_common.h"
#include "room_defs.h"

static const RectF g_room07_platforms[] = {
    { T(0), T(0), T(96), T(2) },
    { T(0), T(2), T(3), T(25) },
    { T(93), T(2), T(3), T(25) },
    { T(40), T(17), T(9), T(1) },
    { T(64), T(17), T(9), T(1) },
    { T(80), T(18), T(13), T(1) },
    { T(3), T(25), T(90), T(2) },
};

static const RectF g_room07_type_a_walls[] = {
    { T(30), T(14), T(8), T(1) },
    { T(84), T(14), T(2), T(4) },
    { T(51), T(17), T(12), T(1) },
    { T(74), T(19), T(5), T(1) },
    { T(12), T(20), T(3), T(5) },
};

static const PistonDevice g_room07_pistons[] = {
    { T(74), T(5), T(5), T(1.00f), T(0.90f), T(1.00f), T(12), 0.40f, PISTON_DOWN },
    { T(22), T(8), T(5), T(1.00f), T(0.90f), T(1.00f), T(15), 0.00f, PISTON_DOWN },
    { T(2), T(19), T(5), T(1.00f), T(0.90f), T(1.00f), T(18), 0.00f, PISTON_RIGHT },
    { T(38), T(20), T(5), T(1.00f), T(0.90f), T(1.00f), T(53), 0.00f, PISTON_RIGHT },
    { T(31), T(24), T(5), T(1.00f), T(0.90f), T(1.00f), T(8), 0.35f, PISTON_UP },
};

extern const RoomDef g_room07 = {
    g_room07_platforms,
    (int)(sizeof(g_room07_platforms) / sizeof(g_room07_platforms[0])),
    g_room07_type_a_walls,
    (int)(sizeof(g_room07_type_a_walls) / sizeof(g_room07_type_a_walls[0])),
    0,
    0,
    { T(89), T(16), T(2), T(2) },
    T(6),
    T(24),
    { T(0), T(0), T(96), T(27) },
    T(4),
    GRAVITY_DOWN,
    kDefaultDeleteState,
    g_room07_pistons,
    (int)(sizeof(g_room07_pistons) / sizeof(g_room07_pistons[0])),
    0,
    0,
    0,
    0,
    0,
    0,
    0,
};
