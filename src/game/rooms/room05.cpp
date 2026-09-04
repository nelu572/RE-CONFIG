#include "room_common.h"
#include "room_defs.h"

static const RectF g_room05_platforms[] = {
    { T(0), T(0), T(81), T(3) },
    { T(0), T(3), T(3), T(23) },
    { T(50), T(3), T(2), T(10) },
    { T(78), T(3), T(3), T(23) },
    { T(56), T(12), T(15), T(4) },
    { T(33), T(14), T(21), T(1) },
    { T(74), T(14), T(4), T(2) },
    { T(36), T(15), T(18), T(1) },
    { T(36), T(16), T(2), T(10) },
    { T(27), T(18), T(3), T(1) },
    { T(65), T(19), T(3), T(1) },
    { T(54), T(20), T(2), T(1) },
    { T(58), T(20), T(4), T(1) },
    { T(3), T(23), T(33), T(3) },
    { T(38), T(23), T(40), T(3) },
};

static const RectF g_room05_type_a_walls[] = {
    { T(54), T(14), T(2), T(2) },
};

static const PistonDevice g_room05_pistons[] = {
    { T(8), T(2), T(5), T(1), T(1.05f), T(1), T(19), 0.000f, PISTON_DOWN },
    { T(42), T(2), T(4), T(1), T(1.05f), T(1), T(10), 0.330f, PISTON_DOWN },
    { T(78), T(8), T(4), T(1), T(1.05f), T(1), T(25), 0.594f, PISTON_LEFT },
    { T(30), T(19), T(3), T(1), T(0.90f), T(1), T(5), 0.000f, PISTON_UP },
    { T(71), T(21), T(3), T(1), T(0.90f), T(1), T(5), 0.000f, PISTON_UP },
    { T(37), T(21), T(2), T(1), T(0.90f), T(1), T(39), 0.000f, PISTON_RIGHT },
    { T(13), T(23), T(5), T(1), T(1.05f), T(1), T(19), 0.264f, PISTON_UP },
    { T(24), T(23), T(3), T(1), T(1.05f), T(1), T(5), 0.066f, PISTON_UP },
};


static const StaticSpikeDef g_room05_static_spikes[] = {
    StaticSpikeAt(T(61), T(19), STATIC_SPIKE_ROTATION_0_DEGREES),
};

extern const RoomDef g_room05 = {
    g_room05_platforms,
    (int)(sizeof(g_room05_platforms) / sizeof(g_room05_platforms[0])),
    g_room05_type_a_walls,
    (int)(sizeof(g_room05_type_a_walls) / sizeof(g_room05_type_a_walls[0])),
    0,
    0,
    { T(75), T(12), T(2), T(2) }, T(5), T(22), { T(0), T(0), T(81), T(26) },
    T(4),
    GRAVITY_DOWN,
    kDefaultDeleteState,
    g_room05_pistons,
    (int)(sizeof(g_room05_pistons) / sizeof(g_room05_pistons[0])),
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {}, 0, 0, g_room05_static_spikes,
    (int)(sizeof(g_room05_static_spikes) / sizeof(g_room05_static_spikes[0])),
};
