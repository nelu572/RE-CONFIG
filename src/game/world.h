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

enum PistonDirection {
    PISTON_DOWN,
    PISTON_UP,
    PISTON_RIGHT,
    PISTON_LEFT,
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
    PistonDirection direction = PISTON_DOWN;
};

struct GravityBoxDef {
    RectF start;
};

enum PressureSwitchActivator {
    PRESSURE_SWITCH_PLAYER,
    PRESSURE_SWITCH_BOX,
    PRESSURE_SWITCH_ANY,
};

struct PressureSwitchDevice {
    RectF rect;
    PressureSwitchActivator activator;
};

struct PressurePlatformDevice {
    RectF rect;
    float open_offset_x;
    float open_offset_y;
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
    const GravityBoxDef* gravity_boxes;
    int gravity_box_count;
    const PressureSwitchDevice* pressure_switches;
    int pressure_switch_count;
    const PressurePlatformDevice* pressure_platforms;
    int pressure_platform_count;
    int exit_requires_pressure_switches;
};

const RoomDef* GetRoom(int index);
int RoomCount();
int DevelopedRoomCount();