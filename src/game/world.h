#pragma once

#include "delete_rules.h"

struct RectF {
    float x;
    float y;
    float w;
    float h;
};

struct RoomDef {
    const RectF* platforms;
    int platform_count;
    const RectF* type_a_walls;
    int type_a_count;
    RectF exit;
    float player_x;
    float player_y;
    RectF bounds;
    float death_margin;
    GravityDirection initial_gravity;
    DeleteState initial_delete_state;
};

const RoomDef* GetRoom(int index);
int RoomCount();
