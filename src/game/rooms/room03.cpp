#include "room_common.h"
#include "room_defs.h"

static const RectF g_room03_platforms[] = {
    { T(0), T(0), T(60), T(4) },
    { T(0), T(23), T(60), T(4) },
    { T(0), T(4), T(3), T(19) },
    { T(57), T(4), T(3), T(19) },
};

static const SpeakerDevice g_room03_speakers[] = {
    DefaultSpeakerAt(T(50), T(5)),
};

extern const RoomDef g_room03 = {
    g_room03_platforms,
    (int)(sizeof(g_room03_platforms) / sizeof(g_room03_platforms[0])),
    0,
    0,
    g_room03_speakers,
    (int)(sizeof(g_room03_speakers) / sizeof(g_room03_speakers[0])),
    { T(54), T(21), T(2), T(2) },
    T(6),
    T(23) - 40.0f,
    { T(0), T(0), T(60), T(27) },
    T(4),
    GRAVITY_DOWN,
    kDefaultDeleteState,
};

