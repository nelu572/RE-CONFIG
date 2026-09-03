#include "room_common.h"
#include "room_defs.h"

static const RectF g_room06_platforms[] = {
    { T(0), T(0), T(57), T(2) },
    { T(0), T(2), T(3), T(23) },
    { T(54), T(2), T(3), T(11) },
    { T(11), T(2), T(23), T(1) },
    { T(11), T(3), T(1), T(2) },
    { T(33), T(3), T(1), T(2) },
    { T(11), T(5), T(23), T(1) },
    { T(14), T(9), T(43), T(1) },
    { T(14), T(10), T(1), T(3) },
    { T(14), T(13), T(30), T(1) },
    { T(47), T(13), T(10), T(1) },
    { T(46), T(14), T(11), T(1) },
    { T(45), T(15), T(12), T(1) },
    { T(44), T(16), T(13), T(1) },
    { T(43), T(17), T(14), T(1) },
    { T(42), T(18), T(15), T(1) },
    { T(41), T(19), T(16), T(1) },
    { T(40), T(20), T(17), T(1) },
    { T(39), T(21), T(18), T(1) },
    { T(38), T(22), T(19), T(1) },
    { T(37), T(23), T(20), T(1) },
    { T(36), T(24), T(21), T(1) },
    { T(0), T(25), T(57), T(2) },
};

static const SpeakerDevice g_room06_speakers[] = {
    DefaultSpeakerAt(T(5), T(5)),
};

static const GravityBoxDef g_room06_gravity_boxes[] = {
    { DefaultGravityBoxAt(T(18.25f), T(3.25f)) },
};

static const PressureSwitchDevice g_room06_pressure_switches[] = {
    { { T(32), T(3), T(1), T(2) } },
};

static const PressurePlatformDevice g_room06_pressure_platforms[] = {
    { { T(30), T(10), T(1), T(3) }, 0.0f, 0.0f, 1u << 0, 1 },
    { { T(44), T(13), T(3), T(1) }, 0.0f, 0.0f, 1u << 0, 1 },
};

extern const RoomDef g_room06 = {
    g_room06_platforms,
    (int)(sizeof(g_room06_platforms) / sizeof(g_room06_platforms[0])),
    0,
    0,
    g_room06_speakers,
    (int)(sizeof(g_room06_speakers) / sizeof(g_room06_speakers[0])),
    { T(16), T(11), T(2), T(2) },
    T(9),
    T(24),
    { T(0), T(0), T(57), T(27) },
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

