#pragma once

#include "delete_rules.h"

struct RectF {
    float x;
    float y;
    float w;
    float h;
};

struct SpeakerDevice {
    float x;
    float y;
    float width;
    float height;
};

struct PistonDevice {
    float x;
    float y;
    float width;
    float body_height;
    float shaft_width;
    float plate_height;
    float travel;
    float cycle_seconds;
    float phase;
};

struct RoomDef {
    const RectF* platforms;
    int platform_count;
    const RectF* type_a_walls;
    int type_a_count;
    const SpeakerDevice* speakers;
    int speaker_count;
    RectF exit;
    float player_x;
    float player_y;
    RectF bounds;
    float death_margin;
    GravityDirection initial_gravity;
    DeleteState initial_delete_state;
    const PistonDevice* pistons;
    int piston_count;
};

const RoomDef* GetRoom(int index);
int RoomCount();
int DevelopedRoomCount();
