#include "room_common.h"
#include "room_defs.h"

static const RectF g_room01_platforms[] = {
    { T(6), T(21), T(36), T(6) },
};

extern const RoomDef g_room01 = {
    g_room01_platforms,
    (int)(sizeof(g_room01_platforms) / sizeof(g_room01_platforms[0])),
    0,
    0,
    0,
    0,
    { T(37), T(19), T(2), T(2) },
    T(10),
    T(21) - 40.0f,
    { T(0), T(0), T(48), T(27) },
    T(4),
    GRAVITY_DOWN,
    kDefaultDeleteState,
};