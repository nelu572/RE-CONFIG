#include "room_common.h"
#include "room_defs.h"

static const RectF g_room09_platforms[] = {
    { T(0), T(0), T(48), T(2) },
    { T(0), T(52), T(48), T(2) },
    { T(0), T(2), T(3), T(50) },
    { T(45), T(2), T(3), T(50) },

    { T(33), T(2), T(2), T(13) },
    { T(32), T(19), T(2), T(19) },
    { T(3), T(18), T(32), T(1) },

    { T(30), T(40), T(2), T(1) },
    { T(27), T(43), T(5), T(1) },
    { T(24), T(46), T(8), T(1) },
    { T(21), T(49), T(11), T(1) },

    { T(14), T(48), T(1), T(4) },
};

static const SpeakerDevice g_room09_speakers[] = {
    DefaultSpeakerAt(T(37), T(39)),
};

static const PistonDevice g_room09_pistons[] = {
    // The upper piston blocks the final leftward run to EXIT.
    { T(18), T(8), T(5), T(1.00f), T(0.90f), T(1.00f), T(8), 0.25f, PISTON_DOWN },
    // This wide rising piston delivers the player into the speaker's vertical lane.
    { T(34), T(52), T(11), T(1.00f), T(0.90f), T(1.00f), T(13), 0.00f, PISTON_UP },
};

static const GravityBoxDef g_room09_gravity_boxes[] = {
    { DefaultGravityBoxAt(T(8), T(50.5f)) },
};

static const PressureSwitchDevice g_room09_pressure_switches[] = {
    { { T(13.25f), T(50), T(1), T(2) }, PRESSURE_SWITCH_MOUNT_RIGHT },
};

static const PressurePlatformDevice g_room09_pressure_platforms[] = {
    { { T(18), T(40), T(2), T(12) }, T(14), 0.0f, 1u << 0 },
};

extern const RoomDef g_room09 = {
    g_room09_platforms,
    (int)(sizeof(g_room09_platforms) / sizeof(g_room09_platforms[0])),
    0,
    0,
    g_room09_speakers,
    (int)(sizeof(g_room09_speakers) / sizeof(g_room09_speakers[0])),
    { T(8), T(15), T(2), T(2) },
    T(4),
    T(50),
    { T(0), T(0), T(48), T(54) },
    T(4),
    GRAVITY_DOWN,
    kDefaultDeleteState,
    g_room09_pistons,
    (int)(sizeof(g_room09_pistons) / sizeof(g_room09_pistons[0])),
    g_room09_gravity_boxes,
    (int)(sizeof(g_room09_gravity_boxes) / sizeof(g_room09_gravity_boxes[0])),
    g_room09_pressure_switches,
    (int)(sizeof(g_room09_pressure_switches) / sizeof(g_room09_pressure_switches[0])),
    g_room09_pressure_platforms,
    (int)(sizeof(g_room09_pressure_platforms) / sizeof(g_room09_pressure_platforms[0])),
    0,
};