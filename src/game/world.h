#pragma once

#include "delete_rules.h"

struct RectF {
    float x;
    float y;
    float w;
    float h;
};

enum SpeakerMount {
    SPEAKER_MOUNT_AUTO,
    SPEAKER_MOUNT_LEFT,
    SPEAKER_MOUNT_RIGHT,
};

enum SpeakerStyle {
    SPEAKER_STYLE_STANDARD,
    SPEAKER_STYLE_MINI,
};

struct SpeakerDevice {
    float x;
    float y;
    float width;
    float height;
    SpeakerMount mount = SPEAKER_MOUNT_AUTO;
    SpeakerStyle style = SPEAKER_STYLE_STANDARD;
    float wave_range_scale = 1.0f;
    float push_strength_scale = 1.0f;
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

enum WalkerEnemySpawnCode {
    WALKER_ENEMY_M1,
    WALKER_ENEMY_M2,
};

struct WalkerEnemyDef {
    RectF start;
    float move_speed;
    int initial_direction;
    WalkerEnemySpawnCode spawn_code = WALKER_ENEMY_M1;
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
    PressureSwitchMount mount = PRESSURE_SWITCH_MOUNT_AUTO;
};

struct PressurePlatformDevice {
    RectF rect;
    float open_offset_x;
    float open_offset_y;
    // Zero keeps the legacy behavior: every pressure switch in the room is required.
    unsigned int required_switch_mask = 0;
    int disappears_when_open = 0;
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
    int pressure_barrier_count = 0;
    RectF checkpoint = {};
    const RectF* checkpoints = 0;
    int checkpoint_count = 0;
};

const RoomDef* GetRoom(int index);
int RoomCount();
int DevelopedRoomCount();
RectF PressurePlatformRectAt(const PressurePlatformDevice* platform, float open_amount);
PressureSwitchMount PressureSwitchMountFor(const RoomDef* room, const PressureSwitchDevice* sw);
int RoomCheckpointCount(const RoomDef* room);
const RectF* RoomCheckpointAt(const RoomDef* room, int index);
