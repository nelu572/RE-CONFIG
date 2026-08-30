#include "room_common.h"
#include "room_defs.h"

static const RectF g_room07_platforms[] = {
    { T(0), T(0), T(96), T(2) },
    { T(0), T(2), T(3), T(23) },
    { T(93), T(2), T(3), T(23) },
    { T(31), T(23), T(8), T(2) },
    { T(50), T(22), T(3), T(3) },
    { T(53), T(22), T(7), T(1) },
    { T(72), T(19), T(5), T(1) },
    { T(78), T(18), T(15), T(1) },
    { T(0), T(25), T(96), T(2) },
};

static const RectF g_room07_type_a_walls[] = {
    { T(13), T(21), T(2), T(4) },
    { T(42), T(22), T(8), T(1) },
    { T(86), T(14), T(2), T(4) },
};

static const PistonDevice g_room07_pistons[] = {
    { T(22), T(8), T(5), T(1.00f), T(0.90f), T(1.00f), T(15.00f), 1.18f, 0.00f, PISTON_DOWN },
    { T(60), T(5), T(5), T(1.00f), T(0.90f), T(1.00f), T(16.00f), 1.28f, 0.22f, PISTON_DOWN },
    { T(75), T(3), T(5), T(1.00f), T(0.90f), T(1.00f), T(14.00f), 1.45f, 0.55f, PISTON_DOWN },
    { T(91), T(13), T(5), T(1.00f), T(0.90f), T(1.00f), T(10.00f), 1.35f, 0.15f, PISTON_LEFT },
    { T(66), T(24), T(5), T(1.00f), T(0.90f), T(1.00f), T(4.00f), 1.55f, 0.35f, PISTON_UP },
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