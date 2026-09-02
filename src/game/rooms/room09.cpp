#include "room_common.h"
#include "room_defs.h"

static const RectF g_room09_platforms[] = {
    { T(71), T(0), T(62), T(5) },
    { T(133), T(0), T(35), T(1) },
    { T(163), T(3), T(5), T(2) },
    { T(80), T(5), T(53), T(2) },
    { T(80), T(7), T(18), T(2) },
    { T(123), T(7), T(10), T(4) },
    { T(86), T(9), T(12), T(2) },
    { T(71), T(10), T(3), T(6) },
    { T(86), T(11), T(2), T(2) },
    { T(112), T(10), T(4), T(1) },
    { T(118), T(10), T(2), T(1) },
    { T(110), T(11), T(10), T(1) },
    { T(107), T(13), T(1), T(5) },
    { T(106), T(14), T(1), T(4) },
    { T(104), T(15), T(2), T(3) },
    { T(108), T(15), T(23), T(3) },
    { T(131), T(17), T(2), T(1) },
    { T(53), T(14), T(8), T(2) },
    { T(71), T(16), T(9), T(11) },
    { T(35), T(17), T(5), T(2) },
    { T(90), T(17), T(8), T(2) },
    { T(121), T(18), T(1), T(3) },
    { T(80), T(19), T(22), T(3) },
    { T(125), T(19), T(18), T(2) },
    { T(29), T(20), T(4), T(1) },
    { T(42), T(20), T(5), T(2) },
    { T(26), T(21), T(7), T(1) },
    { T(125), T(21), T(8), T(6) },
    { T(142), T(21), T(1), T(3) },
    { T(26), T(22), T(4), T(1) },
    { T(86), T(22), T(18), T(5) },
    { T(106), T(22), T(7), T(5) },
    { T(19), T(23), T(2), T(1) },
    { T(0), T(24), T(13), T(1) },
    { T(19), T(24), T(5), T(1) },
    { T(45), T(24), T(8), T(3) },
    { T(0), T(25), T(24), T(2) },
};

static const RectF g_room09_type_a_walls[] = {
    { T(133), T(13), T(5), T(1) },
};

static const SpeakerDevice g_room09_speakers[] = {
    DefaultSpeakerAt(T(5), T(10), SPEAKER_MOUNT_LEFT),
    DefaultSpeakerAt(T(59), T(5), SPEAKER_MOUNT_LEFT),
    DefaultSpeakerAt(T(113), T(8), SPEAKER_MOUNT_RIGHT),
    DefaultSpeakerAt(T(142), T(5), SPEAKER_MOUNT_RIGHT),
};
static const WalkerEnemyDef g_room09_walker_enemies[] = {
    { { T(16.25f), T(22.55f), T(1.5f), T(0.9f) }, 120.0f, 1 },
    { { T(110.25f), T(13.55f), T(1.5f), T(0.9f) }, 120.0f, 1 },
};

static const PistonDevice g_room09_pistons[] = {
    { T(92), T(10), T(4), T(1), T(0.90f), T(1), T(5), 0.00f, PISTON_DOWN },
    { T(47), T(24), T(4), T(1), T(0.90f), T(1), T(8), 0.00f, PISTON_UP },
};

static const GravityBoxDef g_room09_gravity_boxes[] = {
    { DefaultGravityBoxAt(T(113.25f), T(8.25f)) },
};
static const PressureSwitchDevice g_room09_pressure_switches[] = {
    { { T(58), T(13), T(2), T(1) }, PRESSURE_SWITCH_ANY, PRESSURE_SWITCH_MOUNT_AUTO },
    { { T(116), T(10), T(2), T(1) }, PRESSURE_SWITCH_ANY, PRESSURE_SWITCH_MOUNT_AUTO },
    { { T(120), T(19), T(1), T(2) }, PRESSURE_SWITCH_ANY, PRESSURE_SWITCH_MOUNT_AUTO },
};

static const PressurePlatformDevice g_room09_pressure_platforms[] = {
    { { T(63), T(3), T(5), T(2) }, 0.0f, T(15), 1u << 0 },
    { { T(113), T(13), T(16), T(2) }, 0.0f, 0.0f, 1u << 1, 1 },
    { { T(115), T(21), T(7), T(1) }, T(7), 0.0f, 1u << 2 },
};

extern const RoomDef g_room09 = {
    g_room09_platforms,
    (int)(sizeof(g_room09_platforms) / sizeof(g_room09_platforms[0])),
    g_room09_type_a_walls,
    (int)(sizeof(g_room09_type_a_walls) / sizeof(g_room09_type_a_walls[0])),
    g_room09_speakers,
    (int)(sizeof(g_room09_speakers) / sizeof(g_room09_speakers[0])),
    { -T(4), -T(4), T(1), T(1) },
    T(72),
    T(9),
    { T(0), T(0), T(168), T(27) },
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
    g_room09_walker_enemies,
    (int)(sizeof(g_room09_walker_enemies) / sizeof(g_room09_walker_enemies[0])),
    0,
    { T(165), T(2), T(1), T(1) },
};