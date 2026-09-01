#include "room_common.h"
#include "room_defs.h"

static const RectF g_room09_platforms[] = {
    // A. The start path looks down into the first walker's low pocket.
    { T(19), T(23), T(2), T(1) },
    { T(0), T(24), T(13), T(1) },
    { T(19), T(24), T(5), T(1) },
    { T(0), T(25), T(24), T(2) },

    // B. Ordinary-jump platforms rise and fall across the lower route.
    { T(29), T(20), T(6), T(1) },
    { T(26), T(21), T(9), T(1) },
    { T(26), T(22), T(5), T(1) },
    { T(37), T(17), T(7), T(2) },
    { T(46), T(20), T(6), T(2) },
    { T(49), T(24), T(10), T(3) },

    // D. The piston leads into the upper crossing route.
    { T(58), T(14), T(9), T(2) },
    { T(69), T(11), T(7), T(2) },
    { T(78), T(14), T(7), T(2) },
    { T(86), T(16), T(4), T(2) },

    // E. The final walker is confined to this lower switch corridor.
    { T(81), T(18), T(9), T(2) },
    { T(77), T(20), T(1), T(5) },
    { T(91), T(20), T(5), T(4) },
    { T(77), T(24), T(19), T(1) },
    { T(78), T(25), T(18), T(2) },
};

static const WalkerEnemyDef g_room09_walker_enemies[] = {
    { { T(16.25f), T(24.1f), T(1.5f), T(0.9f) }, 120.0f, 1 },
    { { T(82.25f), T(23.1f), T(1.5f), T(0.9f) }, 120.0f, 1 },
};

static const PistonDevice g_room09_pistons[] = {
    // I at row 23 and i at row 15: the plate rises by 8T into the upper route.
    { T(52), T(24), T(5), T(1), T(0.90f), T(1), T(8), 0.00f, PISTON_UP },
};

static const PressureSwitchDevice g_room09_pressure_switches[] = {
    // Only the final walker may hold this floor switch; the player cannot open the exit route directly.
    { { T(88), T(23), T(2), T(1) }, PRESSURE_SWITCH_WALKER_ENEMY, PRESSURE_SWITCH_MOUNT_DOWN },
};

static const PressurePlatformDevice g_room09_pressure_platforms[] = {
    // Closed at the final corridor's right edge and lifted above the passage while the switch is held.
    { { T(90), T(15), T(1), T(5) }, 0.0f, -T(5), 1u << 0 },
};

extern const RoomDef g_room09 = {
    g_room09_platforms,
    (int)(sizeof(g_room09_platforms) / sizeof(g_room09_platforms[0])),
    0,
    0,
    0,
    0,
    { T(93), T(17), T(2), T(3) },
    T(4),
    T(23),
    { T(0), T(0), T(96), T(27) },
    T(4),
    GRAVITY_DOWN,
    kDefaultDeleteState,
    g_room09_pistons,
    (int)(sizeof(g_room09_pistons) / sizeof(g_room09_pistons[0])),
    0,
    0,
    g_room09_pressure_switches,
    (int)(sizeof(g_room09_pressure_switches) / sizeof(g_room09_pressure_switches[0])),
    g_room09_pressure_platforms,
    (int)(sizeof(g_room09_pressure_platforms) / sizeof(g_room09_pressure_platforms[0])),
    1,
    g_room09_walker_enemies,
    (int)(sizeof(g_room09_walker_enemies) / sizeof(g_room09_walker_enemies[0])),
};