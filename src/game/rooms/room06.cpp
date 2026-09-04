#include "room_common.h"
#include "room_defs.h"

static const RectF g_room06_platforms[] = {
    { T(0), T(0), T(48), T(3) },
    { T(0), T(3), T(3), T(24) },
    { T(45), T(3), T(3), T(24) },
    { T(5), T(13), T(7), T(14) },
    { T(36), T(19), T(1), T(8) },
    { T(3), T(24), T(2), T(3) },
    { T(12), T(24), T(8), T(3) },
    { T(25), T(24), T(11), T(3) },
    { T(37), T(24), T(8), T(3) },
    { T(18), T(21), T(9), T(0.25f) },
};

static const GravityBoxDef g_room06_gravity_boxes[] = {
    { DefaultGravityBoxAt(T(5.25f), T(11.25f)) },
    { DefaultGravityBoxAt(T(21.75f), T(18.75f)) },
};

static const PressureSwitchDevice g_room06_pressure_switches[] = {
    { { T(3), T(23), T(2), T(1) }, PRESSURE_SWITCH_MOUNT_DOWN },
};

static const PressurePlatformDevice g_room06_pressure_platforms[] = {
    { { T(36), T(18), T(9), T(1) }, T(0), T(0), 1u << 0, 1 },
};

extern const RoomDef g_room06 = {
    g_room06_platforms,
    (int)(sizeof(g_room06_platforms) / sizeof(g_room06_platforms[0])),
    0,
    0,
    0,
    0,
    { T(40), T(22), T(2), T(2) }, T(10), T(11), { T(0), T(0), T(48), T(27) },
    T(4),
    GRAVITY_DOWN,
    kDefaultDeleteState,
    0,
    0,
    g_room06_gravity_boxes,
    (int)(sizeof(g_room06_gravity_boxes) / sizeof(g_room06_gravity_boxes[0])),
    g_room06_pressure_switches,
    (int)(sizeof(g_room06_pressure_switches) / sizeof(g_room06_pressure_switches[0])),
    g_room06_pressure_platforms,
    (int)(sizeof(g_room06_pressure_platforms) / sizeof(g_room06_pressure_platforms[0])),
    0,
};
