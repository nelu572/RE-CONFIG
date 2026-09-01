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

static constexpr float PISTON_CYCLE_SECONDS = 1.10f;

struct PistonDevice {
    float x;
    float y;
    float width;
    float body_height;
    float shaft_width;
    float plate_height;
    float travel;
    float phase;
    PistonDirection direction = PISTON_DOWN;
};

struct GravityBoxDef {
    RectF start;
};

struct WalkerEnemyDef {
    RectF start;
    float move_speed;
    int initial_direction;
};

enum PressureSwitchActivator {
    PRESSURE_SWITCH_PLAYER,
    PRESSURE_SWITCH_BOX,
    PRESSURE_SWITCH_ANY,
    PRESSURE_SWITCH_WALKER_ENEMY,
};

enum PressureSwitchMount {
    PRESSURE_SWITCH_MOUNT_AUTO,
    PRESSURE_SWITCH_MOUNT_RIGHT,
    PRESSURE_SWITCH_MOUNT_LEFT,
    PRESSURE_SWITCH_MOUNT_DOWN,
    PRESSURE_SWITCH_MOUNT_UP,
};

struct PressureSwitchDevice {
    RectF rect;
    PressureSwitchActivator activator;
    PressureSwitchMount mount = PRESSURE_SWITCH_MOUNT_AUTO;
};

struct PressurePlatformDevice {
    RectF rect;
    float open_offset_x;
    float open_offset_y;
    // Zero keeps the legacy behavior: every pressure switch in the room is required.
    unsigned int required_switch_mask = 0;
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
    const WalkerEnemyDef* walker_enemies = 0;
    int walker_enemy_count = 0;
};

const RoomDef* GetRoom(int index);
int RoomCount();
int DevelopedRoomCount();
