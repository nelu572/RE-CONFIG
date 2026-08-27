#include "room_common.h"
#include "room_defs.h"

static const RectF g_room06_platforms[] = {
    { T(0), T(0), T(48), T(3) },
    { T(0), T(3), T(19), T(1) },
    { T(21), T(3), T(27), T(1) },
    { T(0), T(4), T(7), T(19) },
    { T(41), T(4), T(7), T(3) },
    { T(35), T(7), T(13), T(16) },
    { T(0), T(23), T(48), T(4) },
};

static const GravityBoxDef g_room06_gravity_boxes[] = {
    { { T(19), T(21), T(1.5f), T(1.5f) } },
};

static const PressureSwitchDevice g_room06_pressure_switches[] = {
    { { T(19), T(3), T(2), 32.0f }, PRESSURE_SWITCH_ANY },
};

static const PressurePlatformDevice g_room06_pressure_platforms[] = {
    { { T(35), T(4), T(2), T(3) }, 0.0f, -T(4) },
};

extern const RoomDef g_room06 = {
    g_room06_platforms,
    (int)(sizeof(g_room06_platforms) / sizeof(g_room06_platforms[0])),
    0,
    0,
    0,
    0,
    { T(39), T(4), T(2), T(3) },
    T(11),
    T(22),
    { T(0), T(0), T(48), T(27) },
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