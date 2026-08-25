#include "room_common.h"
#include "room_defs.h"

static const RectF g_room04_platforms[] = {
    { T(0), T(0), T(48), T(4) },
    { T(0), T(21), T(48), T(6) },
    { T(0), T(4), T(3), T(17) },
    { T(45), T(4), T(3), T(17) },
    { T(14), T(4), T(20), T(16.25f) },
};

extern const RoomDef g_room04 = {
    g_room04_platforms,
    (int)(sizeof(g_room04_platforms) / sizeof(g_room04_platforms[0])),
    0,
    0,
    0,
    0,
    { T(40), T(19), T(2), T(2) },
    T(6),
    T(21) - 40.0f,
    { T(0), T(0), T(48), T(27) },
    T(4),
    GRAVITY_DOWN,
    kDefaultDeleteState,
};