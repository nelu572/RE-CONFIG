#include "room_common.h"
#include "room_defs.h"

static const RectF g_room05_platforms[] = {
    { T(0), T(0), T(48), T(3) },
    { T(0), T(3), T(3), T(20) },
    { T(45), T(3), T(3), T(20) },
    { T(0), T(23), T(48), T(4) },
};

static const GravityBoxDef g_room05_gravity_boxes[] = {
    { { T(15), T(21), T(2), T(2) } },
};

static const PressureSwitchDevice g_room05_pressure_switches[] = {
    { { T(30), T(21), T(1), T(2) }, PRESSURE_SWITCH_ANY },
};

static const PressurePlatformDevice g_room05_pressure_platforms[] = {
    { { T(35), T(20), T(2), T(3) }, 0.0f, -T(3) },
};

extern const RoomDef g_room05 = {
    g_room05_platforms,
    (int)(sizeof(g_room05_platforms) / sizeof(g_room05_platforms[0])),
    0,
    0,
    0,
    0,
    { T(40), T(21), T(2), T(2) },
    T(7),
    T(22),
    { T(0), T(0), T(48), T(27) },
    T(4),
    GRAVITY_DOWN,
    kDefaultDeleteState,
    0,
    0,
    g_room05_gravity_boxes,
    (int)(sizeof(g_room05_gravity_boxes) / sizeof(g_room05_gravity_boxes[0])),
    g_room05_pressure_switches,
    (int)(sizeof(g_room05_pressure_switches) / sizeof(g_room05_pressure_switches[0])),
    g_room05_pressure_platforms,
    (int)(sizeof(g_room05_pressure_platforms) / sizeof(g_room05_pressure_platforms[0])),
    0,
};

