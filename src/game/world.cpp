#include "world.h"
#include "rooms/room_defs.h"

static const RoomDef* const g_rooms[] = {
    &g_room00,
    &g_room01,
    &g_room02,
    &g_room03,
    &g_room04,
};

const RoomDef* GetRoom(int index) {
    return g_rooms[index];
}

int RoomCount() {
    return (int)(sizeof(g_rooms) / sizeof(g_rooms[0]));
}

int DevelopedRoomCount() {
    return 5;
}