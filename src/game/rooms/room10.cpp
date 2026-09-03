#include "room_common.h"
#include "room_defs.h"

static const RectF g_room10_platforms[] = {
    { T(11), T(6), T(14), T(3) }, { T(40), T(6), T(13), T(3) }, { T(76), T(6), T(14), T(3) },
    { T(105), T(6), T(13), T(3) }, { T(123), T(6), T(3), T(3) }, { T(26), T(9), T(13), T(3) },
    { T(54), T(9), T(19), T(3) }, { T(91), T(9), T(13), T(3) }, { T(125), T(12), T(1), T(26) },
    { T(132), T(12), T(1), T(26) }, { T(136), T(12), T(1), T(30) }, { T(136), T(45), T(1), T(9) }, { T(5), T(22), T(1), T(3) },
    { T(11), T(22), T(1), T(3) }, { T(18), T(22), T(10), T(3) }, { T(41), T(22), T(10), T(6) },
    { T(52), T(22), T(19), T(3) }, { T(72), T(22), T(11), T(6) }, { T(89), T(22), T(29), T(6) },
    { T(28), T(25), T(13), T(3) }, { T(51), T(25), T(13), T(3) }, { T(69), T(25), T(3), T(3) },
    { T(95), T(28), T(3), T(10) }, { T(53), T(34), T(3), T(6) }, { T(103), T(34), T(22), T(4) },
    { T(126), T(34), T(6), T(4) }, { T(40), T(35), T(13), T(3) }, { T(27), T(44), T(35), T(4) },
    { T(90), T(45), T(11), T(1) }, { T(103), T(45), T(14), T(1) }, { T(117), T(45), T(3), T(1) },
    { T(122), T(45), T(14), T(1) }, { T(90), T(46), T(47), T(1) }, { T(14), T(47), T(13), T(4) },
    { T(90), T(47), T(2), T(7) }, { T(27), T(48), T(5), T(3) }, { T(0), T(50), T(14), T(4) },
    { T(14), T(51), T(5), T(3) },
};
static const RectF g_room10_type_a_walls[] = { { T(69), T(34), T(3), T(1) }, { T(48), T(38), T(2), T(6) } };
static const SpeakerDevice g_room10_speakers[] = {
    DefaultSpeakerAt(T(58), T(31)),
    MiniSpeakerAt(T(129), T(47)),
};
static const PistonDevice g_room10_pistons[] = {
    { T(6), T(22), T(5), T(1), T(0.90f), T(1), T(15), 0.05f, PISTON_UP },
    { T(118), T(22), T(5), T(1), T(0.90f), T(1), T(15), 0.42f, PISTON_UP },
    { T(64), T(44), T(5), T(1), T(0.90f), T(1), T(17), 0.71f, PISTON_UP },
};
static const PressureSwitchDevice g_room10_pressure_switches[] = {
    { { T(110), T(33), T(2), T(1) } }, { { T(135), T(50), T(1), T(2) } },
    { { T(23), T(21), T(2), T(1) } }, { { T(78), T(21), T(2), T(1) } },
    { { T(101), T(45), T(2), T(1) } }, { { T(120), T(45), T(2), T(1) } },
};
static const PressurePlatformDevice g_room10_pressure_platforms[] = {
    { { T(103), T(33), T(5), T(1) }, -T(5), 0.0f, 1u << 0 },
    { { T(12), T(23), T(6), T(1) }, 0.0f, 0.0f, 1u << 2, 1 },
    { { T(83), T(23), T(6), T(1) }, 0.0f, 0.0f, 1u << 3, 1 },
    { { T(101), T(50), T(2), T(2) }, 0.0f, -T(2), 1u << 4 },
    { { T(120), T(50), T(2), T(2) }, 0.0f, -T(2), 1u << 5 },
    { { T(136), T(42), T(1), T(3) }, -T(47), 0.0f, 1u << 1 },
    { { T(92), T(52), T(44), T(2) }, 0.0f, 0.0f, 1u << 1, 1 },
};
static const WalkerEnemyDef g_room10_walker_enemies[] = {
    { { T(18.25f), T(5.10f), T(1.5f), T(0.9f) }, 140.0f, 1, WALKER_ENEMY_M1 },
    { { T(111.25f), T(5.10f), T(1.5f), T(0.9f) }, 100.0f, -1, WALKER_ENEMY_M2 },
    { { T(45.25f), T(21.10f), T(1.5f), T(0.9f) }, 100.0f, 1, WALKER_ENEMY_M2 },
    { { T(46.25f), T(34.10f), T(1.5f), T(0.9f) }, 100.0f, 1, WALKER_ENEMY_M2 },
    { { T(32.25f), T(43.10f), T(1.5f), T(0.9f) }, 140.0f, 1, WALKER_ENEMY_M1 },
    { { T(92.25f), T(51.10f), T(1.5f), T(0.9f) }, 140.0f, -1, WALKER_ENEMY_M1 },
};

extern const RoomDef g_room10 = {
    g_room10_platforms, (int)(sizeof(g_room10_platforms) / sizeof(g_room10_platforms[0])),
    g_room10_type_a_walls, (int)(sizeof(g_room10_type_a_walls) / sizeof(g_room10_type_a_walls[0])),
    g_room10_speakers, (int)(sizeof(g_room10_speakers) / sizeof(g_room10_speakers[0])),
    { T(135), T(44), T(1), T(1) }, T(90), T(44), { T(0), T(0), T(137), T(54) }, T(4), GRAVITY_DOWN,
    kDefaultDeleteState, g_room10_pistons, (int)(sizeof(g_room10_pistons) / sizeof(g_room10_pistons[0])),
    0, 0,
    g_room10_pressure_switches, (int)(sizeof(g_room10_pressure_switches) / sizeof(g_room10_pressure_switches[0])),
    g_room10_pressure_platforms, (int)(sizeof(g_room10_pressure_platforms) / sizeof(g_room10_pressure_platforms[0])),
    1, g_room10_walker_enemies, (int)(sizeof(g_room10_walker_enemies) / sizeof(g_room10_walker_enemies[0])), 3,
    { T(63), T(8), T(1), T(1) },
};
