#include "room_common.h"
#include "room_defs.h"

static const RectF g_room02_platforms[] = {
    { T(0), T(0), T(60), T(4) },
    { T(0), T(23), T(60), T(4) },
    { T(0), T(4), T(3), T(19) },
    { T(57), T(4), T(3), T(19) },
};

static const SpeakerDevice g_room02_speakers[] = {
    { T(50), T(5), T(5), T(9) },
};

extern const RoomDef g_room02 = {
    g_room02_platforms,
    (int)(sizeof(g_room02_platforms) / sizeof(g_room02_platforms[0])),
    0,
    0,
    g_room02_speakers,
    (int)(sizeof(g_room02_speakers) / sizeof(g_room02_speakers[0])),
    { T(54), T(21), T(2), T(2) },
    T(6),
    T(23) - 40.0f,
    { T(0), T(0), T(60), T(27) },
    T(4),
    GRAVITY_DOWN,
    kDefaultDeleteState,
};

