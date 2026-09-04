#include "room_common.h"
#include "room_defs.h"

static const RectF g_room05_platforms[] = {
    { T(0), T(0), T(68), T(3) },
    { T(0), T(3), T(3), T(23) },
    { T(43), T(3), T(2), T(10) },
    { T(65), T(3), T(3), T(23) },
    { T(49), T(12), T(9), T(4) },
    { T(26), T(14), T(21), T(1) },
    { T(61), T(14), T(4), T(2) },
    { T(29), T(15), T(18), T(1) },
    { T(29), T(16), T(2), T(10) },
    { T(20), T(18), T(3), T(1) },
    { T(52), T(19), T(3), T(1) },
    { T(47), T(20), T(2), T(1) },
    { T(3), T(23), T(26), T(3) },
    { T(31), T(23), T(34), T(3) },
};

static const RectF g_room05_type_a_walls[] = {
    { T(47), T(14), T(2), T(2) },
};

static const PistonDevice g_room05_pistons[] = {
    { T(8), T(2), T(5), T(1), T(1.05f), T(1), T(19), 0.000f, PISTON_DOWN },
    { T(35), T(2), T(4), T(1), T(1.05f), T(1), T(10), 0.330f, PISTON_DOWN },
    { T(65), T(8), T(4), T(1), T(1.05f), T(1), T(19), 0.594f, PISTON_LEFT },
    { T(23), T(19), T(3), T(1), T(1.05f), T(1), T(5), 0.066f, PISTON_UP },
    { T(58), T(21), T(3), T(1), T(0.90f), T(1), T(6), 0.000f, PISTON_UP },
    { T(30), T(21), T(2), T(1), T(0.90f), T(1), T(33), 0.000f, PISTON_RIGHT },
    { T(17), T(23), T(3), T(1), T(1.05f), T(1), T(5), 0.264f, PISTON_UP },
};


extern const RoomDef g_room05 = {
    g_room05_platforms,
    (int)(sizeof(g_room05_platforms) / sizeof(g_room05_platforms[0])),
    g_room05_type_a_walls,
    (int)(sizeof(g_room05_type_a_walls) / sizeof(g_room05_type_a_walls[0])),
    0,
    0,
    { T(62), T(12), T(2), T(2) }, T(5), T(22), { T(0), T(0), T(68), T(27) },
    T(4),
    GRAVITY_DOWN,
    kDefaultDeleteState,
    g_room05_pistons,
    (int)(sizeof(g_room05_pistons) / sizeof(g_room05_pistons[0])),
};
