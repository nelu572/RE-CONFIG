#include "room_common.h"
#include "room_defs.h"

static const RectF g_room03_platforms[] = {
    { T(0), T(0), T(60), T(2) },
    { T(0), T(2), T(3), T(25) },
    { T(57), T(2), T(3), T(25) },
    { T(3), T(7), T(41), T(2) },
    { T(10), T(18), T(8), T(1) },
    { T(36), T(18), T(21), T(9) },
    { T(17), T(19), T(1), T(8) },
    { T(3), T(25), T(14), T(2) },
};

static const SpeakerDevice g_room03_speakers[] = {
    MiniSpeakerAt(T(24), T(2)),
    DefaultSpeakerAt(T(29), T(18)),
};

extern const RoomDef g_room03 = {
    g_room03_platforms,
    (int)(sizeof(g_room03_platforms) / sizeof(g_room03_platforms[0])),
    0,
    0,
    g_room03_speakers,
    (int)(sizeof(g_room03_speakers) / sizeof(g_room03_speakers[0])),
    { T(15), T(23), T(2), T(2) }, T(6), T(6), { T(0), T(0), T(60), T(27) },
    T(4),
    GRAVITY_DOWN,
    kDefaultDeleteState,
};

