#include "room_common.h"
#include "room_defs.h"

static const RectF g_room09_platforms[] = {
    { T(77), T(0), T(60), T(5) },
    { T(88), T(5), T(49), T(7) },
    { T(77), T(10), T(3), T(17) },
    { T(88), T(12), T(13), T(2) },
    { T(134), T(12), T(3), T(15) },
    { T(58), T(14), T(9), T(2) },
    { T(88), T(14), T(5), T(1) },
    { T(104), T(14), T(27), T(2) },
    { T(133), T(14), T(1), T(2) },
    { T(97), T(15), T(4), T(5) },
    { T(131), T(15), T(2), T(1) },
    { T(80), T(16), T(17), T(4) },
    { T(37), T(17), T(7), T(2) },
    { T(101), T(17), T(1), T(3) },
    { T(102), T(18), T(30), T(2) },
    { T(29), T(20), T(6), T(2) },
    { T(46), T(20), T(6), T(2) },
    { T(80), T(20), T(10), T(7) },
    { T(26), T(21), T(3), T(2) },
    { T(29), T(22), T(2), T(1) },
    { T(90), T(22), T(42), T(2) },
    { T(19), T(23), T(2), T(4) },
    { T(0), T(24), T(13), T(3) },
    { T(21), T(24), T(3), T(3) },
    { T(49), T(24), T(10), T(3) },
    { T(13), T(25), T(6), T(2) },
    { T(90), T(26), T(44), T(1) },
    { T(88), T(15), T(5), T(0.25f) },
};
static const WalkerEnemyDef g_room09_walker_enemies[] = {
    { { T(16.25f), T(24.1f), T(1.5f), T(0.9f) }, 120.0f, 1 },
};

static const PistonDevice g_room09_pistons[] = {
    // I at row 23 and i at row 15: the plate rises by 8T into the upper route.
    { T(52), T(24), T(5), T(1), T(0.90f), T(1), T(8), 0.00f, PISTON_UP },
};

static const GravityBoxDef g_room09_gravity_boxes[] = {
    { DefaultGravityBoxAt(T(125.25f), T(12.25f)) },
    { DefaultGravityBoxAt(T(109.25f), T(20.25f)) },
    { DefaultGravityBoxAt(T(109.25f), T(24.25f)) },
};
static const PressureSwitchDevice g_room09_pressure_switches[] = {
    { { T(64), T(13), T(2), T(1) }, PRESSURE_SWITCH_ANY, PRESSURE_SWITCH_MOUNT_AUTO },
    { { T(131), T(14), T(2), T(1) }, PRESSURE_SWITCH_ANY, PRESSURE_SWITCH_MOUNT_AUTO },
};

static const PressurePlatformDevice g_room09_pressure_platforms[] = {
    { { T(69), T(3), T(5), T(2) }, 0.0f, T(15), 1u << 0 },
    { { T(90), T(20), T(19), T(2) }, 0.0f, T(2), 1u << 1 },
    { { T(111), T(20), T(21), T(2) }, 0.0f, T(2), 1u << 1 },
    { { T(90), T(24), T(19), T(2) }, 0.0f, T(2), 1u << 1 },
    { { T(111), T(24), T(21), T(2) }, 0.0f, T(2), 1u << 1 },
    { { T(132), T(18), T(2), T(8) }, T(2), 0.0f, 1u << 1 },
};

extern const RoomDef g_room09 = {
    g_room09_platforms,
    (int)(sizeof(g_room09_platforms) / sizeof(g_room09_platforms[0])),
    0,
    0,
    0,
    0,
    { -T(4), -T(4), T(1), T(1) },
    T(78),
    T(9),
    { T(0), T(0), T(137), T(27) },
    T(4),
    GRAVITY_DOWN,
    kDefaultDeleteState,
    g_room09_pistons,
    (int)(sizeof(g_room09_pistons) / sizeof(g_room09_pistons[0])),
    g_room09_gravity_boxes,
    (int)(sizeof(g_room09_gravity_boxes) / sizeof(g_room09_gravity_boxes[0])),
    g_room09_pressure_switches,
    (int)(sizeof(g_room09_pressure_switches) / sizeof(g_room09_pressure_switches[0])),
    g_room09_pressure_platforms,
    (int)(sizeof(g_room09_pressure_platforms) / sizeof(g_room09_pressure_platforms[0])),
    0,
    g_room09_walker_enemies,
    (int)(sizeof(g_room09_walker_enemies) / sizeof(g_room09_walker_enemies[0])),
};