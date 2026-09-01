#include "world.h"
#include "rooms/room_defs.h"

static const RoomDef* const g_rooms[] = {
    &g_room00,
    &g_room01,
    &g_room02,
    &g_room03,
    &g_room04,
    &g_room05,
    &g_room06,
    &g_room07,
    &g_room08,
    &g_room09,
};

const RoomDef* GetRoom(int index) {
    return g_rooms[index];
}

int RoomCount() {
    return (int)(sizeof(g_rooms) / sizeof(g_rooms[0]));
}

int DevelopedRoomCount() {
    return 10;
}

