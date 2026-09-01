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

RectF PressurePlatformRectAt(const PressurePlatformDevice* platform, float open_amount) {
    RectF rect = platform->rect;
    if (open_amount < 0.0f) open_amount = 0.0f;
    if (open_amount > 1.0f) open_amount = 1.0f;
    rect.x += platform->open_offset_x * open_amount;
    rect.y += platform->open_offset_y * open_amount;
    return rect;
}

static float PressureSwitchEdgeOverlap(float a0, float a1, float b0, float b1) {
    float start = a0 > b0 ? a0 : b0;
    float end = a1 < b1 ? a1 : b1;
    return end > start ? end - start : 0.0f;
}

static void PressureSwitchAddPlatformContacts(const PressureSwitchDevice* sw, const RectF* platform, float* contacts) {
    float overlap;
    float delta;

    delta = platform->y - (sw->rect.y + sw->rect.h);
    if (delta >= -0.01f && delta <= 0.01f) {
        overlap = PressureSwitchEdgeOverlap(sw->rect.x, sw->rect.x + sw->rect.w, platform->x, platform->x + platform->w);
        if (overlap > contacts[0]) contacts[0] = overlap;
    }
    delta = platform->x - (sw->rect.x + sw->rect.w);
    if (delta >= -0.01f && delta <= 0.01f) {
        overlap = PressureSwitchEdgeOverlap(sw->rect.y, sw->rect.y + sw->rect.h, platform->y, platform->y + platform->h);
        if (overlap > contacts[1]) contacts[1] = overlap;
    }
    delta = (platform->x + platform->w) - sw->rect.x;
    if (delta >= -0.01f && delta <= 0.01f) {
        overlap = PressureSwitchEdgeOverlap(sw->rect.y, sw->rect.y + sw->rect.h, platform->y, platform->y + platform->h);
        if (overlap > contacts[2]) contacts[2] = overlap;
    }
    delta = (platform->y + platform->h) - sw->rect.y;
    if (delta >= -0.01f && delta <= 0.01f) {
        overlap = PressureSwitchEdgeOverlap(sw->rect.x, sw->rect.x + sw->rect.w, platform->x, platform->x + platform->w);
        if (overlap > contacts[3]) contacts[3] = overlap;
    }
}

PressureSwitchMount PressureSwitchMountFor(const RoomDef* room, const PressureSwitchDevice* sw) {
    if (sw->mount != PRESSURE_SWITCH_MOUNT_AUTO) {
        return sw->mount;
    }

    float contacts[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    const PressureSwitchMount mounts[4] = {
        PRESSURE_SWITCH_MOUNT_DOWN,
        PRESSURE_SWITCH_MOUNT_RIGHT,
        PRESSURE_SWITCH_MOUNT_LEFT,
        PRESSURE_SWITCH_MOUNT_UP,
    };
    if (room) {
        for (int i = 0; i < room->platform_count; ++i) {
            PressureSwitchAddPlatformContacts(sw, &room->platforms[i], contacts);
        }
        // Moving platforms define the mounting side from their closed map position.
        for (int i = 0; i < room->pressure_platform_count; ++i) {
            RectF platform = PressurePlatformRectAt(&room->pressure_platforms[i], 0.0f);
            PressureSwitchAddPlatformContacts(sw, &platform, contacts);
        }
    }

    int best_index = -1;
    float best_contact = 0.0f;
    for (int i = 0; i < 4; ++i) {
        if (contacts[i] > best_contact) {
            best_contact = contacts[i];
            best_index = i;
        }
    }
    if (best_index >= 0) {
        return mounts[best_index];
    }
    return sw->rect.w >= sw->rect.h ? PRESSURE_SWITCH_MOUNT_DOWN : PRESSURE_SWITCH_MOUNT_RIGHT;
}
