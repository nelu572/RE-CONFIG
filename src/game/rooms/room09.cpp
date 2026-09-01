#include "room_common.h"
#include "room_defs.h"

// ROOM 09 is intentionally a neutral system-test room. Its final late-game map is not designed yet.
static const RectF g_room09_platforms[] = {
    { T(0), T(0), T(32), T(1) },
    { T(0), T(0), T(1), T(18) },
    { T(31), T(0), T(1), T(18) },
    { T(0), T(16), T(32), T(2) },
};

static const WalkerEnemyDef g_room09_walker_enemies[] = {
    // Temporary runtime test only; remove before ROOM 09's final design.
    { { T(15), T(15.1f), T(1.5f), T(0.9f) }, 120.0f, -1 },
};
extern const RoomDef g_room09 = {
    g_room09_platforms,
    (int)(sizeof(g_room09_platforms) / sizeof(g_room09_platforms[0])),
    0,
    0,
    0,
    0,
    { T(27), T(14), T(2), T(2) },
    T(3),
    T(15),
    { T(0), T(0), T(32), T(18) },
    T(4),
    GRAVITY_DOWN,
    kDefaultDeleteState,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    g_room09_walker_enemies,
    (int)(sizeof(g_room09_walker_enemies) / sizeof(g_room09_walker_enemies[0])),
};
