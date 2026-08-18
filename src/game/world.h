#pragma once

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
};

const RoomDef* GetRoom(int index);
int RoomCount();
