#include "stage_update.h"

#include "collision.h"
#include "exit_sequence.h"
#include "input.h"
#include "perf.h"
#include "piston.h"
#include "player_movement.h"
#include "settings_ui.h"
#include "stage_cache.h"
#include "tutorial_ui.h"

#include <math.h>

static constexpr float GAME_DEATH_RESPAWN_DELAY = 0.58f;
static constexpr float CHECKPOINT_FLAG_DROP_SECONDS = 0.65f;
static constexpr float SPEAKER_WAVE_RANGE = 1080.0f;
static constexpr float SPEAKER_PUSH_SPEED = 1900.0f;
static constexpr float SPEAKER_BASE_PUSH_STRENGTH = 0.10f;
static constexpr float SPEAKER_CLOSE_PUSH_BOOST = 3.25f;
static constexpr float SPEAKER_VERTICAL_PUSH_SCALE = 0.35f;
static constexpr float SPEAKER_AIR_PUSH_SCALE = 1.35f;
static constexpr float SPEAKER_PUSH_SMOOTH_SPEED = 18.0f;
static constexpr float PLAYER_FLEX_NORMAL_TANGENT = 40.0f;
static constexpr float PLAYER_FLEX_NORMAL_GRAVITY = 40.0f;
static constexpr float PLAYER_FLEX_SOFT_IDLE_TANGENT = 46.4f;
static constexpr float PLAYER_FLEX_SOFT_IDLE_GRAVITY = 30.0f;
static constexpr float PLAYER_FLEX_SOFT_MOVE_TANGENT = 46.4f;
static constexpr float PLAYER_FLEX_SOFT_MOVE_GRAVITY = 30.0f;
static constexpr float PLAYER_FLEX_SOFT_AIR_TANGENT = 39.2f;
static constexpr float PLAYER_FLEX_SOFT_AIR_GRAVITY = 41.6f;
static constexpr float PLAYER_FLEX_SOFT_APPROACH_SPEED = 95.0f;
static constexpr float PLAYER_FLEX_FIRM_GROW_SPEED = 220.0f;
// Every piston can contribute a body, shaft, and plate; the remaining
// dynamic devices each contribute one solid.
static constexpr int GAME_MAX_DYNAMIC_SOLIDS = GAME_MAX_PISTONS * 3 +
                                               GAME_MAX_PRESSURE_PLATFORMS +
                                               GAME_MAX_PRESSURE_SWITCHES +
                                               GAME_MAX_GRAVITY_BOXES;
static constexpr float GRAVITY_BOX_ACCEL = 1850.0f;
static constexpr float GRAVITY_BOX_TANGENT_DAMPING = 24.0f;
static constexpr float GRAVITY_BOX_MAX_GRAVITY_SPEED = 1250.0f;
static constexpr float GRAVITY_BOX_MAX_TANGENT_SPEED = 480.0f;
static constexpr float GRAVITY_BOX_PUSH_SPEED = 120.0f;
static constexpr float GRAVITY_BOX_SPEAKER_MIN_PUSH_SPEED = 200.0f;
static constexpr float PRESSURE_PLATFORM_OPEN_SPEED_PIXELS_PER_SECOND = 560.0f;
static constexpr float PRESSURE_PLATFORM_CLOSE_SPEED_PIXELS_PER_SECOND = 560.0f;
static constexpr float PRESSURE_BARRIER_FADE_SECONDS = 0.25f;
static constexpr float WALKER_ENEMY_APPROACH_RANGE = 200.0f;
static constexpr float WALKER_ENEMY_SPIKE_RETRACT_SECONDS = 0.60f;
static constexpr float WALKER_ENEMY_CROUCH_SECONDS = 0.10f;
static constexpr float WALKER_ENEMY_SPIKE_DEPLOY_SECONDS = 0.08f;
static constexpr float WALKER_ENEMY_EYE_CROUCH_SECONDS = 0.18f;
static constexpr float WALKER_ENEMY_LEAVE_RANGE = 240.0f;
static constexpr float WALKER_ENEMY_TURN_SQUASH_SECONDS = 0.10f;
static constexpr int ROOM09_INDEX = 8;
static unsigned int g_room09_route_seed = 0x9e3779b9u;

struct GameSpeakerPushVelocity {
    float vx;
    float vy;
};

struct GameWalkerTriangle {
    float ax;
    float ay;
    float bx;
    float by;
    float cx;
    float cy;
};

static float GameClampF(float value, float lo, float hi);
static float GameAbsF(float value);
static int GameRangesOverlap(float a0, float a1, float b0, float b1);
static void GameApplyPlayerFlexibility(GameState* state, int value, float move, int airborne, GravityDirection anchor_direction, float dt);
static int GameRoomGravityBoxCount(const RoomDef* room);
static int GameAppendGravityBoxSolids(const GameState* state, RectF* out_solids, int count, int max_solids);
static int GameAppendPressurePlatformSolids(const GameState* state, RectF* out_solids, int count, int max_solids);
static int GameAppendPressureSwitchSolids(const GameState* state, RectF* out_solids, int count, int max_solids);
static GameSpeakerPushVelocity GameComputeSpeakerPushVelocityForRect(const GameState* state, const RectF* rect, int grounded);
static int GameTryClearGravityBoxesForMovingPlayer(GameState* state, const RectF* previous_player, const RectF* current_player, float move_x, float move_y, int ignored_box_index);
static void GamePushWalkerEnemiesByMovingPiston(GameState* state, const PistonDevice* piston, float previous_extension, float current_extension);
static void GameCarryWalkerEnemiesOnPistonPlate(GameState* state, const PistonDevice* piston, float previous_extension, float current_extension);
static void GameStartPlayerDeath(GameState* state);
static int GameWalkerEnemySpikesTouchRect(const GameState* state, int enemy_index, const RectF* rect);
static int GameWalkerEnemySpikesTouchAnotherEnemy(const GameState* state, int enemy_index, int other_index);

int GameFeatureActive(const GameState* state, DeleteFeature feature) {
    return state->delete_state.deleted[feature] == 0;
}

const RoomDef* GameCurrentRoom(const GameState* state) {
    return GetRoom(state->current_room);
}

static GameRoomStartState GameBuildRoomStartState(const RoomDef* room) {
    GameRoomStartState start;
    start.player_x = room->player_x;
    start.player_y = room->player_y;
    start.gravity_direction = room->initial_gravity;
    start.delete_state = room->initial_delete_state;
    return start;
}
static float GameFlexApproachF(float value, float target, float dt, float grow_speed, float shrink_speed) {
    if (dt <= 0.0f) {
        return target;
    }
    float speed = value < target ? grow_speed : shrink_speed;
    float max_delta = speed * dt;
    if (value < target) {
        value += max_delta;
        if (value > target) value = target;
    } else if (value > target) {
        value -= max_delta;
        if (value < target) value = target;
    }
    return value;
}
static GravityDirection GameFlexAnchorDirection(int airborne, GravityDirection grounded_anchor) {
    (void)airborne;
    return grounded_anchor;
}

static void GameSetPlayerVelocityOnGravityAxis(Player* player, GravityDirection direction, float speed) {
    if (direction == GRAVITY_LEFT) {
        player->vx = -speed;
    } else if (direction == GRAVITY_RIGHT) {
        player->vx = speed;
    } else if (direction == GRAVITY_UP) {
        player->vy = -speed;
    } else {
        player->vy = speed;
    }
}

void GameSetGravityDirection(GameState* state, GravityDirection direction) {
    if (direction < GRAVITY_UP || direction > GRAVITY_LEFT || state->gravity_direction == direction) {
        return;
    }
    GravityDirection previous_direction = state->gravity_direction;
    state->gravity_direction = direction;
    GameApplyPlayerFlexibility(state, SettingsUiItemValue(SettingsFlexibilityItemIndex()), 0.0f, !state->player.grounded, GameFlexAnchorDirection(!state->player.grounded, previous_direction), 1.0f / 60.0f);
    state->player.vx = 0.0f;
    state->player.vy = 0.0f;
    state->player.grounded = 0;
    state->player.jump_buffer_timer = 0.0f;
    state->player.coyote_timer = 0.0f;
    int gravity_box_count = GameRoomGravityBoxCount(GameCurrentRoom(state));
    for (int i = 0; i < gravity_box_count; ++i) {
        state->gravity_box_vx[i] = 0.0f;
        state->gravity_box_vy[i] = 0.0f;
        state->gravity_box_grounded[i] = 0;
    }
    state->gravity_setting_feedback_until = PerfNowSeconds() + 0.22;
}



static int GameBuildPistonSolids(const GameState* state, const RoomDef* room, float piston_time_seconds, RectF* out_solids, int max_solids) {
    int count = 0;
    if (!room || !out_solids || max_solids <= 0) {
        return 0;
    }
    for (int i = 0; i < room->piston_count; ++i) {
        const PistonDevice* piston = &room->pistons[i];
        float extension = i < GAME_MAX_PISTONS && state ?
            state->piston_effective_extension[i] :
            PistonPoseAt(piston, piston_time_seconds).extension;
        if (count < max_solids) {
            out_solids[count++] = PistonBodyRect(piston);
        }
        RectF shaft = PistonShaftRectForExtension(piston, extension);
        if (shaft.w > 0.001f && shaft.h > 0.001f && count < max_solids) {
            out_solids[count++] = shaft;
        }
        if (count < max_solids) {
            out_solids[count++] = PistonPlateRectForExtension(piston, extension);
        }
    }
    return count;
}

static int GameRoomGravityBoxCount(const RoomDef* room) {
    if (!room || room->gravity_box_count <= 0) {
        return 0;
    }
    return room->gravity_box_count < GAME_MAX_GRAVITY_BOXES ? room->gravity_box_count : GAME_MAX_GRAVITY_BOXES;
}

static int GameRoomWalkerEnemyCount(const RoomDef* room) {
    if (!room || room->walker_enemy_count <= 0) {
        return 0;
    }
    return room->walker_enemy_count < GAME_MAX_WALKER_ENEMIES ? room->walker_enemy_count : GAME_MAX_WALKER_ENEMIES;
}
static int GameRoomStaticSpikeCount(const RoomDef* room) {
    if (!room || !room->static_spikes || room->static_spike_count <= 0) {
        return 0;
    }
    return room->static_spike_count;
}
static int GameRoomPressureSwitchCount(const RoomDef* room) {
    if (!room || room->pressure_switch_count <= 0) {
        return 0;
    }
    return room->pressure_switch_count < GAME_MAX_PRESSURE_SWITCHES ? room->pressure_switch_count : GAME_MAX_PRESSURE_SWITCHES;
}

static int GameRoomPressurePlatformCount(const RoomDef* room) {
    if (!room || room->pressure_platform_count <= 0) {
        return 0;
    }
    return room->pressure_platform_count < GAME_MAX_PRESSURE_PLATFORMS ? room->pressure_platform_count : GAME_MAX_PRESSURE_PLATFORMS;
}

static int GamePressureSwitchMaskPressed(const GameState* state, unsigned int switch_mask) {
    const RoomDef* room = GameCurrentRoom(state);
    int switch_count = GameRoomPressureSwitchCount(room);
    if (switch_count <= 0) {
        return 0;
    }

    if (switch_mask == 0) {
        switch_mask = (1u << switch_count) - 1u;
    }
    for (int i = 0; i < switch_count; ++i) {
        if ((switch_mask & (1u << i)) != 0 && !state->pressure_switch_pressed[i]) {
            return 0;
        }
    }
    return 1;
}

static int GameRoom09PlatformCanOpen(const GameState* state, const PressurePlatformDevice* platform) {
    (void)state;
    (void)platform;
    return 1;
}
static float GamePressurePlatformAmountPerSecond(const PressurePlatformDevice* platform, float pixels_per_second) {
    if (platform->disappears_when_open) {
        return 1.0f / PRESSURE_BARRIER_FADE_SECONDS;
    }
    float travel = GameAbsF(platform->open_offset_x) + GameAbsF(platform->open_offset_y);
    return travel > 0.001f ? pixels_per_second / travel : 1.0f;
}

static int GamePressurePlatformTargetClear(const GameState* state, int platform_index, float open_amount) {
    const RoomDef* room = GameCurrentRoom(state);
    const PressurePlatformDevice* platform = &room->pressure_platforms[platform_index];
    RectF platform_rect = PressurePlatformRectAt(platform, open_amount);
    if (platform->disappears_when_open) {
        RectF player_rect = GamePlayerRect(state);
        if (RectsOverlap(&player_rect, &platform_rect)) {
            return 0;
        }
        int enemy_count = GameRoomWalkerEnemyCount(room);
        for (int i = 0; i < enemy_count; ++i) {
            if (RectsOverlap(&state->walker_enemies[i], &platform_rect)) {
                return 0;
            }
        }
    }
    int box_count = GameRoomGravityBoxCount(room);
    for (int i = 0; i < box_count; ++i) {
        if (RectsOverlap(&state->gravity_boxes[i], &platform_rect)) {
            return 0;
        }
    }
    return 1;
}

static int GameAppendPressurePlatformSolids(const GameState* state, RectF* out_solids, int count, int max_solids) {
    const RoomDef* room = GameCurrentRoom(state);
    int platform_count = GameRoomPressurePlatformCount(room);
    for (int i = 0; i < platform_count && count < max_solids; ++i) {
        const PressurePlatformDevice* platform = &room->pressure_platforms[i];
        if (platform->disappears_when_open) {
            float alpha = 1.0f - GameClampF(state->pressure_platform_open_amount[i], 0.0f, 1.0f);
            if (alpha < 1.0f) {
                continue;
            }
        }
        out_solids[count++] = PressurePlatformRectAt(platform, state->pressure_platform_open_amount[i]);
    }
    return count;
}

static RectF GamePressureSwitchSolidAt(const GameState* state, int switch_index) {
    const RoomDef* room = GameCurrentRoom(state);
    const PressureSwitchDevice* sw = &room->pressure_switches[switch_index];
    RectF rect = sw->rect;
    float anim = GameClampF(state->pressure_switch_anim[switch_index], 0.0f, 1.0f);
    PressureSwitchMount mount = PressureSwitchMountFor(room, sw);
    if (mount == PRESSURE_SWITCH_MOUNT_DOWN || mount == PRESSURE_SWITCH_MOUNT_UP) {
        float travel = anim * 6.0f;
        float next_h = GameClampF(rect.h - travel, 4.0f, rect.h);
        if (mount == PRESSURE_SWITCH_MOUNT_DOWN) {
            rect.y += rect.h - next_h;
        }
        rect.h = next_h;
    } else {
        float side_travel = GameClampF(rect.w * 0.20f, 4.0f, 10.0f) + 2.0f;
        side_travel = GameClampF(side_travel, 0.0f, rect.w - 4.0f);
        if (mount == PRESSURE_SWITCH_MOUNT_RIGHT) {
            rect.x += side_travel;
        }
        rect.w = GameClampF(rect.w - side_travel, 4.0f, rect.w);
    }
    return rect;
}

static int GameAppendPressureSwitchSolids(const GameState* state, RectF* out_solids, int count, int max_solids) {
    const RoomDef* room = GameCurrentRoom(state);
    int switch_count = GameRoomPressureSwitchCount(room);
    for (int i = 0; i < switch_count && count < max_solids; ++i) {
        out_solids[count++] = GamePressureSwitchSolidAt(state, i);
    }
    return count;
}

static int GameAppendGravityBoxSolids(const GameState* state, RectF* out_solids, int count, int max_solids) {
    const RoomDef* room = GameCurrentRoom(state);
    int box_count = GameRoomGravityBoxCount(room);
    for (int i = 0; i < box_count && count < max_solids; ++i) {
        out_solids[count++] = state->gravity_boxes[i];
    }
    return count;
}
// Walker enemies use the same world-solid set as the player. Speakers apply
// force but deliberately do not appear in this list, so they remain non-solid.
static int GameBuildWalkerEnemyDynamicSolids(const GameState* state, RectF* out_solids, int max_solids) {
    const RoomDef* room = GameCurrentRoom(state);
    int count = GameBuildPistonSolids(state, room, state->piston_time_seconds, out_solids, max_solids);
    count = GameAppendPressurePlatformSolids(state, out_solids, count, max_solids);
    count = GameAppendPressureSwitchSolids(state, out_solids, count, max_solids);
    return GameAppendGravityBoxSolids(state, out_solids, count, max_solids);
}

static int GameRectOverlapsLevelSolids(const GameState* state, const RectF* rect, int include_pistons) {
    const RoomDef* room = GameCurrentRoom(state);
    for (int i = 0; i < room->platform_count; ++i) {
        if (RectsOverlap(rect, &room->platforms[i])) {
            return 1;
        }
    }
    if (GameFeatureActive(state, FEATURE_COLLISION_TYPE_A)) {
        for (int i = 0; i < room->type_a_count; ++i) {
            if (RectsOverlap(rect, &room->type_a_walls[i])) {
                return 1;
            }
        }
    }
    RectF dynamic_solids[GAME_MAX_DYNAMIC_SOLIDS];
    int dynamic_solid_count = 0;
    if (include_pistons) {
        dynamic_solid_count = GameBuildPistonSolids(state, room, state->piston_time_seconds, dynamic_solids, GAME_MAX_DYNAMIC_SOLIDS);
    }
    dynamic_solid_count = GameAppendPressurePlatformSolids(state, dynamic_solids, dynamic_solid_count, GAME_MAX_DYNAMIC_SOLIDS);
    dynamic_solid_count = GameAppendPressureSwitchSolids(state, dynamic_solids, dynamic_solid_count, GAME_MAX_DYNAMIC_SOLIDS);
    for (int i = 0; i < dynamic_solid_count; ++i) {
        if (RectsOverlap(rect, &dynamic_solids[i])) {
            return 1;
        }
    }
    return 0;
}

// A switch carries its rider only into free space. An adjacent closed pressure
// platform is a blocker: otherwise entering a sunk switch from that platform
// embeds the rider, and the next horizontal resolve ejects them from its far side.
static int GameRectOverlapsPressureSwitchCarryBlockers(const GameState* state, const RectF* rect) {
    const RoomDef* room = GameCurrentRoom(state);
    for (int i = 0; i < room->platform_count; ++i) {
        if (RectsOverlap(rect, &room->platforms[i])) return 1;
    }
    int pressure_platform_count = GameRoomPressurePlatformCount(room);
    for (int i = 0; i < pressure_platform_count; ++i) {
        const PressurePlatformDevice* platform = &room->pressure_platforms[i];
        // X is stationary. It blocks like a fixed platform only while fully
        // closed; any fade-out amount is already non-solid everywhere else.
        if (platform->disappears_when_open &&
            GameClampF(state->pressure_platform_open_amount[i], 0.0f, 1.0f) > 0.0f) {
            continue;
        }
        RectF pressure_platform = PressurePlatformRectAt(platform, state->pressure_platform_open_amount[i]);
        if (RectsOverlap(rect, &pressure_platform)) return 1;
    }
    if (GameFeatureActive(state, FEATURE_COLLISION_TYPE_A)) {
        for (int i = 0; i < room->type_a_count; ++i) {
            if (RectsOverlap(rect, &room->type_a_walls[i])) return 1;
        }
    }
    RectF pistons[GAME_MAX_DYNAMIC_SOLIDS];
    int piston_count = GameBuildPistonSolids(state, room, state->piston_time_seconds, pistons, GAME_MAX_DYNAMIC_SOLIDS);
    for (int i = 0; i < piston_count; ++i) {
        if (RectsOverlap(rect, &pistons[i])) return 1;
    }
    int switch_count = GameRoomPressureSwitchCount(room);
    for (int i = 0; i < switch_count; ++i) {
        RectF sw = GamePressureSwitchSolidAt(state, i);
        if (RectsOverlap(rect, &sw)) return 1;
    }
    return 0;
}

static int GameRectOverlapsLevelSolidsForPistonBox(const GameState* state, const RectF* rect, int ignored_piston_index) {
    if (GameRectOverlapsLevelSolids(state, rect, 0)) {
        return 1;
    }

    const RoomDef* room = GameCurrentRoom(state);
    int piston_count = room->piston_count < GAME_MAX_PISTONS ? room->piston_count : GAME_MAX_PISTONS;
    for (int i = 0; i < piston_count; ++i) {
        const PistonDevice* piston = &room->pistons[i];
        RectF body = PistonBodyRect(piston);
        if (RectsOverlap(rect, &body)) {
            return 1;
        }
        if (i == ignored_piston_index) {
            continue;
        }
        float extension = state->piston_effective_extension[i];
        RectF shaft = PistonShaftRectForExtension(piston, extension);
        if (shaft.w > 0.001f && shaft.h > 0.001f && RectsOverlap(rect, &shaft)) {
            return 1;
        }
        RectF plate = PistonPlateRectForExtension(piston, extension);
        if (RectsOverlap(rect, &plate)) {
            return 1;
        }
    }
    return 0;
}
static int GameRectOverlapsSolids(const GameState* state, const RectF* rect) {
    if (GameRectOverlapsLevelSolids(state, rect, 1)) {
        return 1;
    }
    const RoomDef* room = GameCurrentRoom(state);
    int box_count = GameRoomGravityBoxCount(room);
    for (int i = 0; i < box_count; ++i) {
        if (RectsOverlap(rect, &state->gravity_boxes[i])) {
            return 1;
        }
    }
    return 0;
}
static RectF GamePlayerFlexRectWithSize(const GameState* state, float tangent_size, float gravity_size, GravityDirection anchor_direction) {
    RectF old = GamePlayerRect(state);
    float width = tangent_size;
    float height = gravity_size;
    if (state->gravity_direction == GRAVITY_LEFT || state->gravity_direction == GRAVITY_RIGHT) {
        width = gravity_size;
        height = tangent_size;
    }

    RectF candidate;
    candidate.w = width;
    candidate.h = height;
    candidate.x = old.x + old.w * 0.5f - width * 0.5f;
    candidate.y = old.y + old.h * 0.5f - height * 0.5f;

    if (anchor_direction == GRAVITY_LEFT) {
        candidate.x = old.x;
    } else if (anchor_direction == GRAVITY_RIGHT) {
        candidate.x = old.x + old.w - width;
    }

    if (anchor_direction == GRAVITY_UP) {
        candidate.y = old.y;
    } else if (anchor_direction == GRAVITY_DOWN) {
        candidate.y = old.y + old.h - height;
    }
    return candidate;
}
static int GamePlayerFlexSizeFits(const GameState* state, float tangent_size, float gravity_size, GravityDirection anchor_direction) {
    RectF candidate = GamePlayerFlexRectWithSize(state, tangent_size, gravity_size, anchor_direction);
    return !GameRectOverlapsSolids(state, &candidate);
}

static RectF GamePlayerVisualRectForScale(const GameState* state, float sx, float sy) {
    RectF pr = GamePlayerRect(state);
    float tangent_size = 40.0f * sx;
    float gravity_size = 40.0f * sy;
    RectF visual = pr;
    if (state->gravity_direction == GRAVITY_DOWN) {
        float center_x = pr.x + pr.w * 0.5f;
        visual.x = center_x - tangent_size * 0.5f;
        visual.y = pr.y + pr.h - gravity_size;
        visual.w = tangent_size;
        visual.h = gravity_size;
    } else if (state->gravity_direction == GRAVITY_UP) {
        float center_x = pr.x + pr.w * 0.5f;
        visual.x = center_x - tangent_size * 0.5f;
        visual.y = pr.y;
        visual.w = tangent_size;
        visual.h = gravity_size;
    } else if (state->gravity_direction == GRAVITY_RIGHT) {
        float center_y = pr.y + pr.h * 0.5f;
        visual.x = pr.x + pr.w - gravity_size;
        visual.y = center_y - tangent_size * 0.5f;
        visual.w = gravity_size;
        visual.h = tangent_size;
    } else {
        float center_y = pr.y + pr.h * 0.5f;
        visual.x = pr.x;
        visual.y = center_y - tangent_size * 0.5f;
        visual.w = gravity_size;
        visual.h = tangent_size;
    }
    return visual;
}

static int GamePlayerStretchBlocked(const GameState* state) {
    RectF stretch = GamePlayerVisualRectForScale(state, 0.92f, 1.12f);
    return GameRectOverlapsSolids(state, &stretch);
}

static void GameApplyPlayerFlexibility(GameState* state, int value, float move, int airborne, GravityDirection anchor_direction, float dt) {
    float tangent_size = PLAYER_FLEX_NORMAL_TANGENT;
    float gravity_size = PLAYER_FLEX_NORMAL_GRAVITY;
    if (value == SETTINGS_FLEXIBILITY_SOFT) {
        int moving = (move < 0.0f ? -move : move) > 0.01f;
        if (airborne) {
            tangent_size = PLAYER_FLEX_SOFT_AIR_TANGENT;
            gravity_size = PLAYER_FLEX_SOFT_AIR_GRAVITY;
        } else {
            tangent_size = moving ? PLAYER_FLEX_SOFT_MOVE_TANGENT : PLAYER_FLEX_SOFT_IDLE_TANGENT;
            gravity_size = moving ? PLAYER_FLEX_SOFT_MOVE_GRAVITY : PLAYER_FLEX_SOFT_IDLE_GRAVITY;
        }


    }
    RectF pr = GamePlayerRect(state);
    float current_tangent_size = (state->gravity_direction == GRAVITY_LEFT || state->gravity_direction == GRAVITY_RIGHT) ? pr.h : pr.w;
    float current_gravity_size = (state->gravity_direction == GRAVITY_LEFT || state->gravity_direction == GRAVITY_RIGHT) ? pr.w : pr.h;
    float grow_speed = value == SETTINGS_FLEXIBILITY_SOFT ? PLAYER_FLEX_SOFT_APPROACH_SPEED : PLAYER_FLEX_FIRM_GROW_SPEED;
    tangent_size = GameFlexApproachF(current_tangent_size, tangent_size, dt, grow_speed, PLAYER_FLEX_SOFT_APPROACH_SPEED);
    gravity_size = GameFlexApproachF(current_gravity_size, gravity_size, dt, grow_speed, PLAYER_FLEX_SOFT_APPROACH_SPEED);

    if (value == SETTINGS_FLEXIBILITY_SOFT && airborne) {
        RectF full_air = GamePlayerFlexRectWithSize(state,
                                                    PLAYER_FLEX_SOFT_AIR_TANGENT,
                                                    PLAYER_FLEX_SOFT_AIR_GRAVITY,
                                                    anchor_direction);
        if (GameRectOverlapsSolids(state, &full_air)) {
            tangent_size = current_tangent_size;
            gravity_size = current_gravity_size;
        }
    }

    int tangent_growing = tangent_size > current_tangent_size + 0.01f;
    int gravity_growing = gravity_size > current_gravity_size + 0.01f;
    if ((tangent_growing || gravity_growing) &&
        !GamePlayerFlexSizeFits(state, tangent_size, gravity_size, anchor_direction)) {
        tangent_size = current_tangent_size;
        gravity_size = current_gravity_size;
    }
    PlayerSetCollisionSizeAnchored(&state->player, tangent_size, gravity_size, state->gravity_direction, anchor_direction);
}

void GameSetPlayerFlexibility(GameState* state, int value) {
    GameApplyPlayerFlexibility(state, value, 0.0f, !state->player.grounded, GameFlexAnchorDirection(!state->player.grounded, state->gravity_direction), 1.0f / 60.0f);
}

void GameSetFeatureActive(GameState* state, DeleteFeature feature, int active) {
    if (feature < 0 || feature >= FEATURE_COUNT || GameFeatureActive(state, feature) == active) {
        return;
    }
    state->delete_state.deleted[feature] = active ? 0 : 1;
    SettingsUiInvalidateCache();
    if (feature == FEATURE_GRAVITY) {
        state->gravity_setting_feedback_until = PerfNowSeconds() + 0.22;
    }
    if (feature == FEATURE_COLLISION_TYPE_A) {
        StageCacheInvalidate();
        SettingsUiMarkFullDirty();
        state->type_a_setting_feedback_until = 0.0;
        if (!active) {
            TutorialUiCompleteTypeA();
            state->type_a_bump_until = 0.0;
        }
    }
    if (feature == FEATURE_GRAVITY && !active) {
        GameSetPlayerVelocityOnGravityAxis(&state->player, state->gravity_direction, 0.0f);
    }
}

void GameToggleFeature(GameState* state, DeleteFeature feature) {
    GameSetFeatureActive(state, feature, !GameFeatureActive(state, feature));
}

RectF GamePlayerRect(const GameState* state) {
    return PlayerCollisionRect(&state->player);
}

static int GameRoomCheckpointCount(const RoomDef* room) {
    return RoomCheckpointCount(room);
}

static int GameRoomHasCheckpoint(const RoomDef* room) {
    return GameRoomCheckpointCount(room) > 0;
}

void GameClearCheckpoint(GameState* state) {
    if (!state) {
        return;
    }
    state->checkpoint_room = -1;
    state->checkpoint_index = -1;
    state->checkpoint_active = 0;
    state->checkpoint_flag_drop = 0.0f;
}

static void GameRerollRoom09Route(GameState* state) {
    unsigned int clock_bits = (unsigned int)(PerfNowSeconds() * 1000000.0);
    g_room09_route_seed ^= clock_bits + 0x9e3779b9u + (g_room09_route_seed << 6) + (g_room09_route_seed >> 2);
    g_room09_route_seed = g_room09_route_seed * 1664525u + 1013904223u;
    state->room09_route3_safe = (int)((g_room09_route_seed >> 31) & 1u);
    state->room09_route_initialized = 1;
}

void GamePrepareRoomEntry(GameState* state, int room_index) {
    if (state && room_index == ROOM09_INDEX) {
        state->room09_route_initialized = 0;
    }
}

void GameRequestStageRestart(GameState* state) {
    if (state && state->current_room == ROOM09_INDEX) {
        state->room09_route_initialized = 0;
    }
    GameResetStage(state);
}

void GameResetStage(GameState* state) {
    if (state->current_room != ROOM09_INDEX) {
        state->room09_route_initialized = 0;
    } else if (!state->room09_route_initialized) {
        GameRerollRoom09Route(state);
    }
    DeleteState saved_delete_state = state->delete_state;
    const RoomDef* room = GameCurrentRoom(state);
    if (!GameRoomHasCheckpoint(room) || state->checkpoint_room != state->current_room ||
        state->checkpoint_index < 0 || state->checkpoint_index >= GameRoomCheckpointCount(room)) {
        GameClearCheckpoint(state);
    }
    const RectF* checkpoint = state->checkpoint_active && state->checkpoint_room == state->current_room ?
        RoomCheckpointAt(room, state->checkpoint_index) : 0;
    int checkpoint_respawn = checkpoint != 0;
    state->room_start_state = GameBuildRoomStartState(room);
    state->player.x = checkpoint_respawn ? checkpoint->x : state->room_start_state.player_x;
    state->player.y = checkpoint_respawn ? checkpoint->y : state->room_start_state.player_y;
    state->checkpoint_flag_drop = checkpoint_respawn ? 1.0f : 0.0f;
    state->player.collision_w = PLAYER_FLEX_NORMAL_TANGENT;
    state->player.collision_h = PLAYER_FLEX_NORMAL_GRAVITY;
    state->player.vx = 0.0f;
    state->player.vy = 0.0f;
    state->player.grounded = 0;
    state->player.jump_buffer_timer = 0.0f;
    state->player.coyote_timer = 0.0f;
    ResetPlayerPresentation(&state->player, state->player_particles, PLAYER_PARTICLE_COUNT);
    state->delete_state = state->room_start_state.delete_state;
    state->gravity_direction = state->room_start_state.gravity_direction;
    for (int i = 0; i < GAME_MAX_GRAVITY_BOXES; ++i) {
        state->gravity_boxes[i] = { 0.0f, 0.0f, 0.0f, 0.0f };
        state->gravity_box_vx[i] = 0.0f;
        state->gravity_box_vy[i] = 0.0f;
        state->gravity_box_grounded[i] = 0;
        state->gravity_box_piston_driven[i] = 0;
    }
    int box_count = GameRoomGravityBoxCount(room);
    for (int i = 0; i < box_count; ++i) {
        state->gravity_boxes[i] = room->gravity_boxes[i].start;
    }
    for (int i = 0; i < GAME_MAX_WALKER_ENEMIES; ++i) {
        state->walker_enemies[i] = { 0.0f, 0.0f, 0.0f, 0.0f };
        state->walker_enemy_gravity_speed[i] = 0.0f;
        state->walker_enemy_direction[i] = 1;
        state->walker_enemy_grounded[i] = 0;
        state->walker_enemy_spike_amount[i] = 0.0f;
        state->walker_enemy_spike_delay[i] = 0.0f;
        state->walker_enemy_squash_amount[i] = 0.0f;
        state->walker_enemy_eye_crouch_amount[i] = 0.0f;
        state->walker_enemy_turn_squash[i] = 0.0f;
        state->walker_enemy_player_near[i] = 0;
    }
    int walker_enemy_count = GameRoomWalkerEnemyCount(room);
    for (int i = 0; i < walker_enemy_count; ++i) {
        state->walker_enemies[i] = room->walker_enemies[i].start;
        if (room->walker_enemies[i].spawn_code == WALKER_ENEMY_M2) {
            state->walker_enemy_spike_amount[i] = 1.0f;
        }
        state->walker_enemy_direction[i] = room->walker_enemies[i].initial_direction < 0 ? -1 : 1;
    }
    for (int i = 0; i < GAME_MAX_PRESSURE_SWITCHES; ++i) {
        state->pressure_switch_pressed[i] = 0;
        state->pressure_switch_anim[i] = 0.0f;
    }
    for (int i = 0; i < GAME_MAX_PRESSURE_PLATFORMS; ++i) {
        state->pressure_platform_open_amount[i] = 0.0f;
        state->pressure_platform_open_cycle_pending[i] = 0;
    }
    state->room_exit_unlocked = (room->pressure_switch_count > 0 || room->exit_requires_pressure_switches) ? 0 : 1;
    state->cleared_room_this_frame = -1;
    state->player_dead = 0;
    state->death_respawn_timer = 0.0f;
    SettingsUiReset();
    for (int feature = 0; feature < FEATURE_COUNT; ++feature) {
        GameSetFeatureActive(state, (DeleteFeature)feature, saved_delete_state.deleted[feature] == 0);
    }
    int gravity_item_index = SettingsGravityDirectionItemIndex();
    if (gravity_item_index >= 0) {
        GameSetGravityDirection(state, (GravityDirection)SettingsUiItemValue(gravity_item_index));
    }
    GameSetPlayerFlexibility(state, SettingsUiItemValue(SettingsFlexibilityItemIndex()));
    RectF pr = GamePlayerRect(state);
    CameraResetToRoom(&state->camera, room, &pr);
    ExitSequenceResetStageState();
    state->type_a_contacted = 0;
    TutorialUiResetStageState();
    state->type_a_bump_until = 0.0;
    state->type_a_setting_feedback_until = 0.0;
    state->gravity_setting_feedback_until = 0.0;
    state->room_started_at_seconds = PerfNowSeconds();
    state->speaker_time_seconds = 0.0;
    state->speaker_push_vx = 0.0f;
    state->speaker_push_vy = 0.0f;
    state->piston_time_seconds = 0.0f;
    state->player_on_piston_support = 0;
    int reset_piston_count = room->piston_count < GAME_MAX_PISTONS ? room->piston_count : GAME_MAX_PISTONS;
    for (int i = 0; i < GAME_MAX_PISTONS; ++i) {
        state->piston_effective_extension[i] = i < reset_piston_count ? PistonPoseAt(&room->pistons[i], state->piston_time_seconds).extension : 0.0f;
    }
    StageCacheInvalidate();
}

static void GamePistonDirectionVector(const PistonDevice* piston, int* x, int* y) {
    *x = 0;
    *y = 1;
    if (!piston) {
        return;
    }
    if (piston->direction == PISTON_UP) {
        *y = -1;
    } else if (piston->direction == PISTON_RIGHT) {
        *x = 1;
        *y = 0;
    } else if (piston->direction == PISTON_LEFT) {
        *x = -1;
        *y = 0;
    }
}

static int GamePistonCrossAxisOverlapsRect(const RectF* plate, const RectF* rect, int axis_x, int axis_y) {
    if (axis_x != 0) {
        return plate->y < rect->y + rect->h && plate->y + plate->h > rect->y;
    }
    if (axis_y != 0) {
        return plate->x < rect->x + rect->w && plate->x + plate->w > rect->x;
    }
    return 0;
}

static int GameMovingRectsCrossAxisOverlap(const RectF* previous_solid, const RectF* current_solid, const RectF* rect, float move_x, float move_y) {
    if (move_x != 0.0f) {
        float swept_y0 = previous_solid->y < current_solid->y ? previous_solid->y : current_solid->y;
        float previous_y1 = previous_solid->y + previous_solid->h;
        float current_y1 = current_solid->y + current_solid->h;
        float swept_y1 = previous_y1 > current_y1 ? previous_y1 : current_y1;
        return GameRangesOverlap(swept_y0, swept_y1, rect->y, rect->y + rect->h);
    }
    if (move_y != 0.0f) {
        float swept_x0 = previous_solid->x < current_solid->x ? previous_solid->x : current_solid->x;
        float previous_x1 = previous_solid->x + previous_solid->w;
        float current_x1 = current_solid->x + current_solid->w;
        float swept_x1 = previous_x1 > current_x1 ? previous_x1 : current_x1;
        return GameRangesOverlap(swept_x0, swept_x1, rect->x, rect->x + rect->w);
    }
    return 0;
}

static int GameMovingRectsTouchOrOverlap(const RectF* previous_solid, const RectF* current_solid, const RectF* rect, int swept) {
    return swept || RectsOverlap(previous_solid, rect) || RectsOverlap(current_solid, rect);
}

static int GameMovingPistonPushCandidate(const PistonDevice* piston,
                                         float previous_extension,
                                         float current_extension,
                                         const RectF* rect,
                                         RectF* candidate,
                                         float* move_x,
                                         float* move_y) {
    float delta = current_extension - previous_extension;
    if (delta == 0.0f) {
        return 0;
    }

    int axis_x;
    int axis_y;
    GamePistonDirectionVector(piston, &axis_x, &axis_y);
    float motion_x = (float)axis_x * delta;
    float motion_y = (float)axis_y * delta;
    if (motion_x == 0.0f && motion_y == 0.0f) {
        return 0;
    }

    RectF previous_plate = PistonPlateRectForExtension(piston, previous_extension);
    RectF current_plate = PistonPlateRectForExtension(piston, current_extension);
    if (!GamePistonCrossAxisOverlapsRect(&current_plate, rect, axis_x, axis_y)) {
        return 0;
    }

    *candidate = *rect;
    *move_x = motion_x;
    *move_y = motion_y;
    if (motion_x > 0.0f) {
        float previous_face = previous_plate.x + previous_plate.w;
        float current_face = current_plate.x + current_plate.w;
        int swept = previous_face <= rect->x && current_face > rect->x;
        if (!GameMovingRectsTouchOrOverlap(&previous_plate, &current_plate, rect, swept)) {
            return 0;
        }
        candidate->x = current_face;
    } else if (motion_x < 0.0f) {
        float rect_right = rect->x + rect->w;
        int swept = previous_plate.x >= rect_right && current_plate.x < rect_right;
        if (!GameMovingRectsTouchOrOverlap(&previous_plate, &current_plate, rect, swept)) {
            return 0;
        }
        candidate->x = current_plate.x - rect->w;
    } else if (motion_y > 0.0f) {
        float previous_face = previous_plate.y + previous_plate.h;
        float current_face = current_plate.y + current_plate.h;
        int swept = previous_face <= rect->y && current_face > rect->y;
        if (!GameMovingRectsTouchOrOverlap(&previous_plate, &current_plate, rect, swept)) {
            return 0;
        }
        candidate->y = current_face;
    } else if (motion_y < 0.0f) {
        float rect_bottom = rect->y + rect->h;
        int swept = previous_plate.y >= rect_bottom && current_plate.y < rect_bottom;
        if (!GameMovingRectsTouchOrOverlap(&previous_plate, &current_plate, rect, swept)) {
            return 0;
        }
        candidate->y = current_plate.y - rect->h;
    }
    return 1;
}

static float GamePistonPlayerPushVelocityScale() {
    float speed_scale = SettingsUiGameSpeedScale();
    if (speed_scale < 0.75f) {
        return 0.85f;
    }
    if (speed_scale > 1.15f) {
        return 1.95f;
    }
    return 1.35f;
}

static void GameApplyPistonPushToPlayer(GameState* state, const RectF* candidate, float move_x, float move_y, float frame_dt) {
    state->player.x = candidate->x;
    state->player.y = candidate->y;
    if (frame_dt <= 0.0001f) {
        return;
    }

    float velocity_scale = GamePistonPlayerPushVelocityScale();
    if (move_x > 0.0f) {
        float push_vx = move_x / frame_dt * velocity_scale;
        if (state->player.vx < push_vx) state->player.vx = push_vx;
    } else if (move_x < 0.0f) {
        float push_vx = move_x / frame_dt * velocity_scale;
        if (state->player.vx > push_vx) state->player.vx = push_vx;
    }
    if (move_y > 0.0f) {
        float push_vy = move_y / frame_dt * velocity_scale;
        if (state->player.vy < push_vy) state->player.vy = push_vy;
    } else if (move_y < 0.0f) {
        float push_vy = move_y / frame_dt * velocity_scale;
        if (state->player.vy > push_vy) state->player.vy = push_vy;
    }
}

static void GamePistonGravityVector(GravityDirection direction, int* x, int* y) {
    *x = 0;
    *y = 1;
    if (direction == GRAVITY_UP) {
        *y = -1;
    } else if (direction == GRAVITY_RIGHT) {
        *x = 1;
        *y = 0;
    } else if (direction == GRAVITY_LEFT) {
        *x = -1;
        *y = 0;
    }
}

static int GameRectSupportedBySolidAlongGravity(const RectF* rect, const RectF* solid, int gravity_x, int gravity_y) {
    const float epsilon = 2.5f;
    if (gravity_y > 0) {
        return GameRangesOverlap(rect->x, rect->x + rect->w, solid->x, solid->x + solid->w) &&
               GameAbsF((rect->y + rect->h) - solid->y) <= epsilon;
    }
    if (gravity_y < 0) {
        return GameRangesOverlap(rect->x, rect->x + rect->w, solid->x, solid->x + solid->w) &&
               GameAbsF(rect->y - (solid->y + solid->h)) <= epsilon;
    }
    if (gravity_x > 0) {
        return GameRangesOverlap(rect->y, rect->y + rect->h, solid->y, solid->y + solid->h) &&
               GameAbsF((rect->x + rect->w) - solid->x) <= epsilon;
    }
    if (gravity_x < 0) {
        return GameRangesOverlap(rect->y, rect->y + rect->h, solid->y, solid->y + solid->h) &&
               GameAbsF(rect->x - (solid->x + solid->w)) <= epsilon;
    }
    return 0;
}


static void GameCarryPlayerOnPistonPlate(GameState* state, const PistonDevice* piston, float previous_extension, float current_extension, float frame_dt) {
    RectF previous_plate = PistonPlateRectForExtension(piston, previous_extension);
    RectF current_plate = PistonPlateRectForExtension(piston, current_extension);
    float move_x = current_plate.x - previous_plate.x;
    float move_y = current_plate.y - previous_plate.y;
    if (move_x == 0.0f && move_y == 0.0f) {
        return;
    }

    int gravity_x;
    int gravity_y;
    GamePistonGravityVector(state->gravity_direction, &gravity_x, &gravity_y);
    float move_with_gravity = move_x * (float)gravity_x + move_y * (float)gravity_y;
    if (move_with_gravity <= 0.0f) {
        return;
    }

    RectF pr = GamePlayerRect(state);
    if (!GameRectSupportedBySolidAlongGravity(&pr, &previous_plate, gravity_x, gravity_y)) {
        return;
    }

    RectF candidate = pr;
    candidate.x += move_x;
    candidate.y += move_y;
    if (GameRectOverlapsLevelSolids(state, &candidate, 0)) {
        return;
    }
    if (!GameTryClearGravityBoxesForMovingPlayer(state, &pr, &candidate, move_x, move_y, -1)) {
        return;
    }

    state->player.x = candidate.x;
    state->player.y = candidate.y;
    state->player.grounded = 1;
    state->player_on_piston_support = 1;
}

static void GameCarryPlayerOnMovingSolid(GameState* state, const RectF* previous_platform, const RectF* current_platform) {
    float move_x = current_platform->x - previous_platform->x;
    float move_y = current_platform->y - previous_platform->y;
    if (move_x == 0.0f && move_y == 0.0f) {
        return;
    }

    int gravity_x;
    int gravity_y;
    GamePistonGravityVector(state->gravity_direction, &gravity_x, &gravity_y);
    RectF player_rect = GamePlayerRect(state);
    if (!GameRectSupportedBySolidAlongGravity(&player_rect, previous_platform, gravity_x, gravity_y)) {
        return;
    }

    RectF candidate = player_rect;
    candidate.x += move_x;
    candidate.y += move_y;
    if (GameRectOverlapsLevelSolids(state, &candidate, 0)) {
        return;
    }
    if (!GameTryClearGravityBoxesForMovingPlayer(state, &player_rect, &candidate, move_x, move_y, -1)) {
        return;
    }

    state->player.x = candidate.x;
    state->player.y = candidate.y;
    state->player.grounded = 1;
}

static int GameCarryPlayerOnPressureSwitch(GameState* state,
                                           const RectF* previous_switch,
                                           const RectF* current_switch) {
    float move_x = current_switch->x - previous_switch->x;
    float move_y = current_switch->y - previous_switch->y;
    if (move_x == 0.0f && move_y == 0.0f) return 0;

    int gravity_x;
    int gravity_y;
    GamePistonGravityVector(state->gravity_direction, &gravity_x, &gravity_y);
    RectF player = GamePlayerRect(state);
    if (!GameRectSupportedBySolidAlongGravity(&player, previous_switch, gravity_x, gravity_y)) {
        return 0;
    }
    RectF candidate = player;
    candidate.x += move_x;
    candidate.y += move_y;
    if (GameRectOverlapsPressureSwitchCarryBlockers(state, &candidate)) {
        return 0;
    }
    if (!GameTryClearGravityBoxesForMovingPlayer(state, &player, &candidate, move_x, move_y, -1)) {
        return 0;
    }
    state->player.x = candidate.x;
    state->player.y = candidate.y;
    state->player.grounded = 1;
    return 1;
}

static int GameMovingSolidPushCandidate(const RectF* previous_solid,
                                        const RectF* current_solid,
                                        const RectF* rect,
                                        float move_x,
                                        float move_y,
                                        RectF* candidate) {
    if (move_x == 0.0f && move_y == 0.0f) {
        return 0;
    }

    *candidate = *rect;
    if (move_x > 0.0f) {
        if (!GameMovingRectsCrossAxisOverlap(previous_solid, current_solid, rect, move_x, move_y)) {
            return 0;
        }
        float previous_face = previous_solid->x + previous_solid->w;
        float current_face = current_solid->x + current_solid->w;
        int swept = previous_face <= rect->x && current_face > rect->x;
        if (!GameMovingRectsTouchOrOverlap(previous_solid, current_solid, rect, swept)) {
            return 0;
        }
        candidate->x = current_face;
    } else if (move_x < 0.0f) {
        if (!GameMovingRectsCrossAxisOverlap(previous_solid, current_solid, rect, move_x, move_y)) {
            return 0;
        }
        float rect_right = rect->x + rect->w;
        int swept = previous_solid->x >= rect_right && current_solid->x < rect_right;
        if (!GameMovingRectsTouchOrOverlap(previous_solid, current_solid, rect, swept)) {
            return 0;
        }
        candidate->x = current_solid->x - rect->w;
    } else if (move_y > 0.0f) {
        if (!GameMovingRectsCrossAxisOverlap(previous_solid, current_solid, rect, move_x, move_y)) {
            return 0;
        }
        float previous_face = previous_solid->y + previous_solid->h;
        float current_face = current_solid->y + current_solid->h;
        int swept = previous_face <= rect->y && current_face > rect->y;
        if (!GameMovingRectsTouchOrOverlap(previous_solid, current_solid, rect, swept)) {
            return 0;
        }
        candidate->y = current_face;
    } else if (move_y < 0.0f) {
        if (!GameMovingRectsCrossAxisOverlap(previous_solid, current_solid, rect, move_x, move_y)) {
            return 0;
        }
        float rect_bottom = rect->y + rect->h;
        int swept = previous_solid->y >= rect_bottom && current_solid->y < rect_bottom;
        if (!GameMovingRectsTouchOrOverlap(previous_solid, current_solid, rect, swept)) {
            return 0;
        }
        candidate->y = current_solid->y - rect->h;
    }
    return 1;
}

static int GameTryMoveGravityBoxByMovingSolid(GameState* state, int box_index, const RectF* previous_solid, const RectF* current_solid, float move_x, float move_y, int depth, int ignored_piston_index) {
    const RoomDef* room = GameCurrentRoom(state);
    int box_count = GameRoomGravityBoxCount(room);
    if (box_index < 0 || box_index >= box_count || depth > box_count) {
        return 0;
    }

    RectF before = state->gravity_boxes[box_index];
    RectF candidate = before;
    if (!GameMovingSolidPushCandidate(previous_solid, current_solid, &before, move_x, move_y, &candidate)) {
        return 1;
    }

    if ((ignored_piston_index >= 0 ? GameRectOverlapsLevelSolidsForPistonBox(state, &candidate, ignored_piston_index) : GameRectOverlapsLevelSolids(state, &candidate, 1))) {
        return 0;
    }

    state->gravity_boxes[box_index] = candidate;
    state->gravity_box_piston_driven[box_index] = 1;
    state->gravity_box_vx[box_index] = 0.0f;
    state->gravity_box_vy[box_index] = 0.0f;

    for (int i = 0; i < box_count; ++i) {
        if (i == box_index) {
            continue;
        }
        if (RectsOverlap(&candidate, &state->gravity_boxes[i])) {
            if (!GameTryMoveGravityBoxByMovingSolid(state, i, &before, &candidate, move_x, move_y, depth + 1, ignored_piston_index)) {
                return 0;
            }
        }
    }
    return 1;
}

static void GameSaveGravityBoxState(const GameState* state, RectF* boxes, float* vx, float* vy, int* piston_driven) {
    const RoomDef* room = GameCurrentRoom(state);
    int box_count = GameRoomGravityBoxCount(room);
    for (int i = 0; i < box_count; ++i) {
        boxes[i] = state->gravity_boxes[i];
        vx[i] = state->gravity_box_vx[i];
        vy[i] = state->gravity_box_vy[i];
        piston_driven[i] = state->gravity_box_piston_driven[i];
    }
}

static void GameRestoreGravityBoxState(GameState* state, const RectF* boxes, const float* vx, const float* vy, const int* piston_driven) {
    const RoomDef* room = GameCurrentRoom(state);
    int box_count = GameRoomGravityBoxCount(room);
    for (int i = 0; i < box_count; ++i) {
        state->gravity_boxes[i] = boxes[i];
        state->gravity_box_vx[i] = vx[i];
        state->gravity_box_vy[i] = vy[i];
        state->gravity_box_piston_driven[i] = piston_driven[i];
    }
}

static int GameTryClearGravityBoxesForMovingPlayer(GameState* state, const RectF* previous_player, const RectF* current_player, float move_x, float move_y, int ignored_box_index) {
    RectF saved_boxes[GAME_MAX_GRAVITY_BOXES];
    float saved_vx[GAME_MAX_GRAVITY_BOXES];
    float saved_vy[GAME_MAX_GRAVITY_BOXES];
    int saved_piston_driven[GAME_MAX_GRAVITY_BOXES];
    GameSaveGravityBoxState(state, saved_boxes, saved_vx, saved_vy, saved_piston_driven);

    const RoomDef* room = GameCurrentRoom(state);
    int box_count = GameRoomGravityBoxCount(room);
    for (int i = 0; i < box_count; ++i) {
        if (i == ignored_box_index) {
            continue;
        }
        RectF ignored = state->gravity_boxes[i];
        if (GameMovingSolidPushCandidate(previous_player, current_player, &state->gravity_boxes[i], move_x, move_y, &ignored)) {
            if (!GameTryMoveGravityBoxByMovingSolid(state, i, previous_player, current_player, move_x, move_y, 0, -1)) {
                GameRestoreGravityBoxState(state, saved_boxes, saved_vx, saved_vy, saved_piston_driven);
                return 0;
            }
        }
    }
    return 1;
}

static int GameTryPushPlayerByMovingSolid(GameState* state, const RectF* previous_solid, const RectF* current_solid, float move_x, float move_y, int ignored_box_index, float frame_dt) {
    RectF pr = GamePlayerRect(state);
    RectF candidate = pr;
    if (!GameMovingSolidPushCandidate(previous_solid, current_solid, &pr, move_x, move_y, &candidate)) {
        return 1;
    }

    if (GameRectOverlapsLevelSolids(state, &candidate, 0)) {
        return 0;
    }
    if (!GameTryClearGravityBoxesForMovingPlayer(state, &pr, &candidate, move_x, move_y, ignored_box_index)) {
        return 0;
    }

    GameApplyPistonPushToPlayer(state, &candidate, move_x, move_y, frame_dt);
    return 2;
}

// A pressed switch lowers its support surface only a few pixels. When a
// horizontal pressure platform pushes the player out of that shallow recess,
// restore the player to the closed switch surface before deciding it is a
// lethal pinch against the surrounding floor lip.
static int GameLiftPlayerOutOfPressureSwitchDepression(const GameState* state,
                                                       const RectF* player,
                                                       RectF* candidate) {
    const RoomDef* room = GameCurrentRoom(state);
    int gravity_x;
    int gravity_y;
    GamePistonGravityVector(state->gravity_direction, &gravity_x, &gravity_y);

    int switch_count = GameRoomPressureSwitchCount(room);
    for (int i = 0; i < switch_count; ++i) {
        if (state->pressure_switch_anim[i] <= 0.001f) continue;

        RectF current_switch = GamePressureSwitchSolidAt(state, i);
        if (!GameRectSupportedBySolidAlongGravity(player, &current_switch, gravity_x, gravity_y)) continue;

        const RectF* closed_switch = &room->pressure_switches[i].rect;
        const float escape_clearance = 12.0f;
        if (gravity_y > 0) {
            candidate->y = closed_switch->y - candidate->h - escape_clearance;
        } else if (gravity_y < 0) {
            candidate->y = closed_switch->y + closed_switch->h + escape_clearance;
        } else if (gravity_x > 0) {
            candidate->x = closed_switch->x - candidate->w - escape_clearance;
        } else {
            candidate->x = closed_switch->x + closed_switch->w + escape_clearance;
        }
        return 1;
    }
    return 0;
}

// A platform that reaches a player resting in a depressed switch must wait
// for the switch-surface correction. Relying on the one-frame carry result
// misses an already-depressed switch, letting the platform displace that
// player as if they had entered the solid from its far side.
static int GamePlayerSupportedByDepressedPressureSwitch(const GameState* state,
                                                        const RectF* player) {
    const RoomDef* room = GameCurrentRoom(state);
    int gravity_x;
    int gravity_y;
    GamePistonGravityVector(state->gravity_direction, &gravity_x, &gravity_y);
    int switch_count = GameRoomPressureSwitchCount(room);
    for (int i = 0; i < switch_count; ++i) {
        if (state->pressure_switch_anim[i] <= 0.001f) continue;
        RectF sw = GamePressureSwitchSolidAt(state, i);
        if (GameRectSupportedBySolidAlongGravity(player, &sw, gravity_x, gravity_y)) {
            return 1;
        }
    }
    return 0;
}

// Pressure platforms move after the normal player step. Resolve a swept
// side or ceiling contact here so the next frame never starts embedded.
static int GameTryPushPlayerByPressurePlatform(GameState* state,
                                               const RectF* previous_platform,
                                               const RectF* current_platform) {
    float move_x = current_platform->x - previous_platform->x;
    float move_y = current_platform->y - previous_platform->y;
    RectF player = GamePlayerRect(state);
    RectF candidate = player;
    if (!GameMovingSolidPushCandidate(previous_platform, current_platform, &player, move_x, move_y, &candidate)) {
        return 1;
    }
    float candidate_move_x = candidate.x - player.x;
    float candidate_move_y = candidate.y - player.y;
    int candidate_is_safe = !GameRectOverlapsLevelSolids(state, &candidate, 0) &&
        GameTryClearGravityBoxesForMovingPlayer(state, &player, &candidate, candidate_move_x, candidate_move_y, -1);
    if (!candidate_is_safe) {
        if (move_x == 0.0f || !GameLiftPlayerOutOfPressureSwitchDepression(state, &player, &candidate)) {
            return 0;
        }
        candidate_move_x = candidate.x - player.x;
        candidate_move_y = candidate.y - player.y;
        if (GameRectOverlapsLevelSolids(state, &candidate, 0) ||
            !GameTryClearGravityBoxesForMovingPlayer(state, &player, &candidate, candidate_move_x, candidate_move_y, -1)) {
            return 0;
        }
    }

    state->player.x = candidate.x;
    state->player.y = candidate.y;
    if (move_x != 0.0f) state->player.vx = 0.0f;
    if (move_y != 0.0f) state->player.vy = 0.0f;
    return 2;
}

// Enemies on a platform's side or ceiling are crushed. Enemies standing on
// its gravity-facing surface remain carried with the platform.
static void GameCrushWalkerEnemiesByPressurePlatform(GameState* state,
                                                      const RectF* previous_platform,
                                                      const RectF* current_platform) {
    float move_x = current_platform->x - previous_platform->x;
    float move_y = current_platform->y - previous_platform->y;
    if (move_x == 0.0f && move_y == 0.0f) return;

    int gravity_x;
    int gravity_y;
    GamePistonGravityVector(state->gravity_direction, &gravity_x, &gravity_y);
    const RoomDef* room = GameCurrentRoom(state);
    int enemy_count = GameRoomWalkerEnemyCount(room);
    for (int i = 0; i < enemy_count; ++i) {
        RectF* enemy = &state->walker_enemies[i];
        if (enemy->w <= 0.0f || enemy->h <= 0.0f) continue;
        if (GameRectSupportedBySolidAlongGravity(enemy, previous_platform, gravity_x, gravity_y)) {
            continue;
        }
        RectF candidate = *enemy;
        if (GameMovingSolidPushCandidate(previous_platform, current_platform, enemy, move_x, move_y, &candidate)) {
            SpawnWalkerEnemyCrushParticles(state->player_particles,
                                           PLAYER_PARTICLE_COUNT,
                                           enemy->x + enemy->w * 0.5f,
                                           enemy->y + enemy->h * 0.5f,
                                           state->gravity_direction);
            *enemy = { room->bounds.x - 10000.0f, room->bounds.y - 10000.0f, 0.0f, 0.0f };
            state->walker_enemy_gravity_speed[i] = 0.0f;
            state->walker_enemy_direction[i] = 1;
            state->walker_enemy_grounded[i] = 0;
            state->walker_enemy_spike_amount[i] = 0.0f;
            state->walker_enemy_spike_delay[i] = 0.0f;
            state->walker_enemy_squash_amount[i] = 0.0f;
            state->walker_enemy_eye_crouch_amount[i] = 0.0f;
            state->walker_enemy_turn_squash[i] = 0.0f;
            state->walker_enemy_player_near[i] = 0;
        }
    }
}

static int GameTryPushPlayerByMovingPiston(GameState* state, const PistonDevice* piston, float previous_extension, float current_extension, float frame_dt) {
    RectF pr = GamePlayerRect(state);
    RectF candidate = pr;
    float move_x = 0.0f;
    float move_y = 0.0f;
    if (!GameMovingPistonPushCandidate(piston, previous_extension, current_extension, &pr, &candidate, &move_x, &move_y)) {
        return 1;
    }

    if (GameRectOverlapsLevelSolids(state, &candidate, 0)) {
        return 0;
    }
    if (!GameTryClearGravityBoxesForMovingPlayer(state, &pr, &candidate, move_x, move_y, -1)) {
        return 0;
    }

    GameApplyPistonPushToPlayer(state, &candidate, move_x, move_y, frame_dt);
    return 1;
}

static int GameTryPushGravityBoxesByMovingPiston(GameState* state, int piston_index, const PistonDevice* piston, float previous_extension, float current_extension, float frame_dt) {
    int pushed_any_box = 0;
    int pushed_player_by_box = 0;
    const RoomDef* room = GameCurrentRoom(state);
    int box_count = GameRoomGravityBoxCount(room);

    RectF saved_boxes[GAME_MAX_GRAVITY_BOXES];
    float saved_vx[GAME_MAX_GRAVITY_BOXES];
    float saved_vy[GAME_MAX_GRAVITY_BOXES];
    int saved_piston_driven[GAME_MAX_GRAVITY_BOXES];
    GameSaveGravityBoxState(state, saved_boxes, saved_vx, saved_vy, saved_piston_driven);

    for (int i = 0; i < box_count; ++i) {
        RectF before = state->gravity_boxes[i];
        RectF candidate = before;
        float move_x = 0.0f;
        float move_y = 0.0f;
        if (!GameMovingPistonPushCandidate(piston, previous_extension, current_extension, &before, &candidate, &move_x, &move_y)) {
            continue;
        }

        if (GameRectOverlapsLevelSolidsForPistonBox(state, &candidate, piston_index)) {
            GameRestoreGravityBoxState(state, saved_boxes, saved_vx, saved_vy, saved_piston_driven);
            return 0;
        }

        pushed_any_box = 1;
        state->gravity_boxes[i] = candidate;
        state->gravity_box_piston_driven[i] = 1;
        state->gravity_box_vx[i] = 0.0f;
        state->gravity_box_vy[i] = 0.0f;
        for (int j = 0; j < box_count; ++j) {
            if (j == i) {
                continue;
            }
            if (RectsOverlap(&candidate, &state->gravity_boxes[j])) {
                if (!GameTryMoveGravityBoxByMovingSolid(state, j, &before, &candidate, move_x, move_y, 0, piston_index)) {
                    GameRestoreGravityBoxState(state, saved_boxes, saved_vx, saved_vy, saved_piston_driven);
                    return 0;
                }
            }
        }
        int player_push = GameTryPushPlayerByMovingSolid(state, &before, &candidate, move_x, move_y, i, frame_dt);
        if (player_push == 0) {
            GameRestoreGravityBoxState(state, saved_boxes, saved_vx, saved_vy, saved_piston_driven);
            return -1;
        }
        if (player_push == 2) {
            pushed_player_by_box = 1;
        }
    }
    if (pushed_player_by_box) {
        return 3;
    }
    return pushed_any_box ? 2 : 1;
}

static int GamePistonSolidOverlapsRectForExtension(const PistonDevice* piston, float extension, const RectF* rect) {
    RectF body = PistonBodyRect(piston);
    if (RectsOverlap(&body, rect)) {
        return 1;
    }
    RectF shaft = PistonShaftRectForExtension(piston, extension);
    if (shaft.w > 0.001f && shaft.h > 0.001f && RectsOverlap(&shaft, rect)) {
        return 1;
    }
    RectF plate = PistonPlateRectForExtension(piston, extension);
    return RectsOverlap(&plate, rect);
}

// Static world solids block the plate in either travel direction. BRICK is
// excluded only while BRICK collision is disabled.
static void GameLimitPistonTravelAtSolid(const RectF* start_plate,
                                         const RectF* target_plate,
                                         float start_extension,
                                         float target_extension,
                                         const RectF* solid,
                                         float* blocked_extension) {
    if (RectsOverlap(start_plate, solid)) {
        *blocked_extension = start_extension;
        return;
    }

    float move_x = target_plate->x - start_plate->x;
    float move_y = target_plate->y - start_plate->y;
    if (!GameMovingRectsCrossAxisOverlap(start_plate, target_plate, solid, move_x, move_y)) {
        return;
    }

    float contact_extension = target_extension;
    int crosses_solid = 0;
    if (move_x > 0.0f) {
        float start_face = start_plate->x + start_plate->w;
        float target_face = target_plate->x + target_plate->w;
        crosses_solid = start_face <= solid->x && target_face > solid->x;
        contact_extension = start_extension + (target_extension - start_extension) * (solid->x - start_face) / (target_face - start_face);
    } else if (move_x < 0.0f) {
        float solid_face = solid->x + solid->w;
        float start_face = start_plate->x;
        float target_face = target_plate->x;
        crosses_solid = start_face >= solid_face && target_face < solid_face;
        contact_extension = start_extension + (target_extension - start_extension) * (start_face - solid_face) / (start_face - target_face);
    } else if (move_y > 0.0f) {
        float start_face = start_plate->y + start_plate->h;
        float target_face = target_plate->y + target_plate->h;
        crosses_solid = start_face <= solid->y && target_face > solid->y;
        contact_extension = start_extension + (target_extension - start_extension) * (solid->y - start_face) / (target_face - start_face);
    } else if (move_y < 0.0f) {
        float solid_face = solid->y + solid->h;
        float start_face = start_plate->y;
        float target_face = target_plate->y;
        crosses_solid = start_face >= solid_face && target_face < solid_face;
        contact_extension = start_extension + (target_extension - start_extension) * (start_face - solid_face) / (start_face - target_face);
    }

    int extending = target_extension > start_extension;
    if (crosses_solid && (extending ? contact_extension < *blocked_extension : contact_extension > *blocked_extension)) {
        *blocked_extension = contact_extension;
    }
}

static float GamePistonExtensionBeforeStaticSolid(const GameState* state,
                                                  const PistonDevice* piston,
                                                  float start_extension,
                                                  float target_extension) {
    if (target_extension == start_extension) {
        return target_extension;
    }

    const RoomDef* room = GameCurrentRoom(state);
    RectF start_plate = PistonPlateRectForExtension(piston, start_extension);
    RectF target_plate = PistonPlateRectForExtension(piston, target_extension);
    float blocked_extension = target_extension;
    for (int i = 0; i < room->platform_count; ++i) {
        GameLimitPistonTravelAtSolid(&start_plate, &target_plate, start_extension, target_extension, &room->platforms[i], &blocked_extension);
    }
    if (GameFeatureActive(state, FEATURE_COLLISION_TYPE_A)) {
        for (int i = 0; i < room->type_a_count; ++i) {
            GameLimitPistonTravelAtSolid(&start_plate, &target_plate, start_extension, target_extension, &room->type_a_walls[i], &blocked_extension);
        }
    }

    RectF device_solids[GAME_MAX_DYNAMIC_SOLIDS];
    int device_solid_count = 0;
    device_solid_count = GameAppendPressurePlatformSolids(state, device_solids, device_solid_count, GAME_MAX_DYNAMIC_SOLIDS);
    device_solid_count = GameAppendPressureSwitchSolids(state, device_solids, device_solid_count, GAME_MAX_DYNAMIC_SOLIDS);
    for (int i = 0; i < device_solid_count; ++i) {
        GameLimitPistonTravelAtSolid(&start_plate, &target_plate, start_extension, target_extension, &device_solids[i], &blocked_extension);
    }

    float minimum_extension = start_extension < target_extension ? start_extension : target_extension;
    float maximum_extension = start_extension > target_extension ? start_extension : target_extension;
    return GameClampF(blocked_extension, minimum_extension, maximum_extension);
}
static int GameApplyPistonExtensionStep(GameState* state, int piston_index, const PistonDevice* piston, float previous_extension, float current_extension, float frame_dt) {
    state->piston_effective_extension[piston_index] = current_extension;

    int box_push = GameTryPushGravityBoxesByMovingPiston(state, piston_index, piston, previous_extension, current_extension, frame_dt);
    if (box_push < 0) {
        GameStartPlayerDeath(state);
        return -1;
    }
    if (box_push == 0) {
        state->piston_effective_extension[piston_index] = previous_extension;
        return 0;
    }

    if (box_push != 3 && !GameTryPushPlayerByMovingPiston(state, piston, previous_extension, current_extension, frame_dt)) {
        GameStartPlayerDeath(state);
        return -1;
    }
    GameCarryPlayerOnPistonPlate(state, piston, previous_extension, current_extension, frame_dt);
    GamePushWalkerEnemiesByMovingPiston(state, piston, previous_extension, current_extension);
    GameCarryWalkerEnemiesOnPistonPlate(state, piston, previous_extension, current_extension);

    RectF pr = GamePlayerRect(state);
    if (GameAbsF(current_extension - previous_extension) > 0.001f && GamePistonSolidOverlapsRectForExtension(piston, current_extension, &pr)) {
        GameStartPlayerDeath(state);
        return -1;
    }
    return 1;
}

static float GamePistonTargetAtAllowedSpeed(const PistonDevice* piston,
                                            float start_extension,
                                            float piston_time_seconds,
                                            float frame_time_seconds) {
    float pose_target = PistonPoseAt(piston, piston_time_seconds).extension;
    float remaining_distance = pose_target - start_extension;
    float movement_speed = remaining_distance > 0.0f ? PISTON_EXTENSION_SPEED : PISTON_RETRACTION_SPEED;
    float allowed_distance = movement_speed * frame_time_seconds;
    if (GameAbsF(remaining_distance) <= allowed_distance) {
        return pose_target;
    }
    return start_extension + (remaining_distance > 0.0f ? allowed_distance : -allowed_distance);
}
static void GameUpdateRoomPistons(GameState* state, float dt) {
    state->player_on_piston_support = 0;
    float piston_time_delta = dt * SettingsUiGameSpeedScale();
    state->piston_time_seconds += piston_time_delta;
    if (state->piston_time_seconds > 600.0f) {
        state->piston_time_seconds -= 600.0f;
    }
    const RoomDef* room = GameCurrentRoom(state);
    int piston_count = room->piston_count < GAME_MAX_PISTONS ? room->piston_count : GAME_MAX_PISTONS;
    for (int i = 0; i < piston_count; ++i) {
        const PistonDevice* piston = &room->pistons[i];
        float start_extension = state->piston_effective_extension[i];
        float target_extension = GamePistonTargetAtAllowedSpeed(piston, start_extension, state->piston_time_seconds, piston_time_delta);
        target_extension = GamePistonExtensionBeforeStaticSolid(state, piston, start_extension, target_extension);
        float delta = target_extension - start_extension;
        int step_count = (int)(GameAbsF(delta) / 8.0f) + 1;
        if (step_count > 192) {
            step_count = 192;
        }

        float previous_extension = start_extension;
        for (int step = 1; step <= step_count; ++step) {
            float t = (float)step / (float)step_count;
            float current_extension = start_extension + delta * t;
            int result = GameApplyPistonExtensionStep(state, i, piston, previous_extension, current_extension, dt);
            if (result < 0) {
                return;
            }
            if (result == 0) {
                break;
            }
            previous_extension = current_extension;
        }
    }
    for (int i = piston_count; i < GAME_MAX_PISTONS; ++i) {
        state->piston_effective_extension[i] = 0.0f;
    }
}
static int GamePlayerOutsideRoomBounds(const GameState* state) {
    const RoomDef* room = GameCurrentRoom(state);
    RectF pr = GamePlayerRect(state);
    float margin = room->death_margin;
    if (pr.x + pr.w < room->bounds.x - margin) return 1;
    if (pr.x > room->bounds.x + room->bounds.w + margin) return 1;
    if (pr.y + pr.h < room->bounds.y - margin) return 1;
    if (pr.y > room->bounds.y + room->bounds.h + margin) return 1;
    return 0;
}

static float GameClampF(float value, float lo, float hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static float GameAbsF(float value) {
    return value < 0.0f ? -value : value;
}

static float GameApproxLength(float x, float y) {
    float ax = GameAbsF(x);
    float ay = GameAbsF(y);
    float hi = ax > ay ? ax : ay;
    float lo = ax > ay ? ay : ax;
    return hi + lo * 0.375f;
}

static void GameGravityVector(GravityDirection direction, int* x, int* y) {
    *x = 0;
    *y = 1;
    if (direction == GRAVITY_UP) {
        *y = -1;
    } else if (direction == GRAVITY_RIGHT) {
        *x = 1;
        *y = 0;
    } else if (direction == GRAVITY_LEFT) {
        *x = -1;
        *y = 0;
    }
}


static int GameWalkerEnemySolidCount(const GameState* state, const RoomDef* room, int dynamic_solid_count) {
    return room->platform_count +
           (GameFeatureActive(state, FEATURE_COLLISION_TYPE_A) ? room->type_a_count : 0) +
           dynamic_solid_count;
}

static const RectF* GameWalkerEnemySolidAt(const GameState* state,
                                           const RoomDef* room,
                                           const RectF* dynamic_solids,
                                           int dynamic_solid_count,
                                           int index) {
    if (index < room->platform_count) {
        return &room->platforms[index];
    }
    index -= room->platform_count;
    int type_a_count = GameFeatureActive(state, FEATURE_COLLISION_TYPE_A) ? room->type_a_count : 0;
    if (index < type_a_count) {
        return &room->type_a_walls[index];
    }
    index -= type_a_count;
    return index < dynamic_solid_count ? &dynamic_solids[index] : 0;
}

static int GameWalkerEnemyTouchesSolid(const GameState* state,
                                       int enemy_index,
                                       const RectF* solid,
                                       int include_spikes) {
    if (!solid || RectsOverlap(&state->walker_enemies[enemy_index], solid)) {
        return solid != 0;
    }
    if (!include_spikes) {
        return 0;
    }

    // A side wedge shares the floor contact line with its supporting platform.
    // Ignore that line so horizontal patrol is blocked only by a solid's side,
    // not by the floor directly underneath the enemy.
    static constexpr float horizontal_contact_inset = 0.01f;
    RectF horizontal_probe = *solid;
    if (horizontal_probe.h > horizontal_contact_inset) {
        horizontal_probe.y += horizontal_contact_inset;
        horizontal_probe.h -= horizontal_contact_inset;
    }
    return GameWalkerEnemySpikesTouchRect(state, enemy_index, &horizontal_probe);
}

static int GameWalkerEnemyTouchesAnotherEnemy(const GameState* state,
                                              int enemy_index,
                                              int other_index,
                                              int include_spikes) {
    const RectF* enemy = &state->walker_enemies[enemy_index];
    const RectF* other = &state->walker_enemies[other_index];
    return RectsOverlap(enemy, other) ||
           (include_spikes && GameWalkerEnemySpikesTouchAnotherEnemy(state, enemy_index, other_index));
}

static void GameSetWalkerEnemyAxisPosition(RectF* enemy, const RectF* before, int axis_x, float delta, float amount) {
    *enemy = *before;
    if (axis_x) {
        enemy->x += delta * amount;
    } else {
        enemy->y += delta * amount;
    }
}

static void GameResolveWalkerEnemySolidOverlap(GameState* state,
                                                int enemy_index,
                                                const RectF* before,
                                                int axis_x,
                                                float delta,
                                                const RectF* solid,
                                                int include_spikes) {
    RectF* enemy = &state->walker_enemies[enemy_index];
    float clear_amount = 0.0f;
    float blocked_amount = 1.0f;
    for (int step = 0; step < 16; ++step) {
        float amount = (clear_amount + blocked_amount) * 0.5f;
        GameSetWalkerEnemyAxisPosition(enemy, before, axis_x, delta, amount);
        if (GameWalkerEnemyTouchesSolid(state, enemy_index, solid, include_spikes)) {
            blocked_amount = amount;
        } else {
            clear_amount = amount;
        }
    }
    GameSetWalkerEnemyAxisPosition(enemy, before, axis_x, delta, clear_amount);
}

static void GameResolveWalkerEnemyOverlap(GameState* state,
                                           int enemy_index,
                                           const RectF* before,
                                           int axis_x,
                                           float delta,
                                           int other_index,
                                           int include_spikes) {
    RectF* enemy = &state->walker_enemies[enemy_index];
    float clear_amount = 0.0f;
    float blocked_amount = 1.0f;
    for (int step = 0; step < 16; ++step) {
        float amount = (clear_amount + blocked_amount) * 0.5f;
        GameSetWalkerEnemyAxisPosition(enemy, before, axis_x, delta, amount);
        if (GameWalkerEnemyTouchesAnotherEnemy(state, enemy_index, other_index, include_spikes)) {
            blocked_amount = amount;
        } else {
            clear_amount = amount;
        }
    }
    GameSetWalkerEnemyAxisPosition(enemy, before, axis_x, delta, clear_amount);
}

static int GameMoveWalkerEnemyAxis(GameState* state,
                                   int enemy_index,
                                   int axis_x,
                                   float delta,
                                   int* collided_enemy_index) {
    if (collided_enemy_index) {
        *collided_enemy_index = -1;
    }
    if (delta == 0.0f) {
        return 0;
    }
    RectF* enemy = &state->walker_enemies[enemy_index];
    RectF before = *enemy;
    if (axis_x) {
        enemy->x += delta;
    } else {
        enemy->y += delta;
    }
    const RoomDef* room = GameCurrentRoom(state);
    RectF dynamic_solids[GAME_MAX_DYNAMIC_SOLIDS];
    int dynamic_solid_count = GameBuildWalkerEnemyDynamicSolids(state, dynamic_solids, GAME_MAX_DYNAMIC_SOLIDS);
    int collided = 0;
    int solid_count = GameWalkerEnemySolidCount(state, room, dynamic_solid_count);
    for (int solid_index = 0; solid_index < solid_count; ++solid_index) {
        const RectF* solid = GameWalkerEnemySolidAt(state, room, dynamic_solids, dynamic_solid_count, solid_index);
        if (!GameWalkerEnemyTouchesSolid(state, enemy_index, solid, axis_x)) {
            continue;
        }
        GameResolveWalkerEnemySolidOverlap(state, enemy_index, &before, axis_x, delta, solid, axis_x);
        collided = 1;
    }
    // Static spikes are hazards for the player, but act as horizontal walls
    // for walker enemies. Keep them out of gravity-axis support resolution.
    if (axis_x) {
        int static_spike_count = GameRoomStaticSpikeCount(room);
        for (int spike_index = 0; spike_index < static_spike_count; ++spike_index) {
            const RectF* solid = &room->static_spikes[spike_index].bounds;
            if (!GameWalkerEnemyTouchesSolid(state, enemy_index, solid, 1)) {
                continue;
            }
            GameResolveWalkerEnemySolidOverlap(state, enemy_index, &before, axis_x, delta, solid, 1);
            collided = 1;
        }
    }
    int enemy_count = GameRoomWalkerEnemyCount(room);
    for (int other_index = 0; other_index < enemy_count; ++other_index) {
        if (other_index == enemy_index) {
            continue;
        }
        if (!GameWalkerEnemyTouchesAnotherEnemy(state, enemy_index, other_index, axis_x)) {
            continue;
        }
        GameResolveWalkerEnemyOverlap(state, enemy_index, &before, axis_x, delta, other_index, axis_x);
        if (collided_enemy_index) {
            *collided_enemy_index = other_index;
        }
        collided = 1;
    }
    return collided;
}

static void GameMoveWalkerEnemyWithSolid(GameState* state, int enemy_index, float move_x, float move_y) {
    if (move_x != 0.0f) {
        GameMoveWalkerEnemyAxis(state, enemy_index, 1, move_x, 0);
    }
    if (move_y != 0.0f) {
        GameMoveWalkerEnemyAxis(state, enemy_index, 0, move_y, 0);
    }
}

static void GamePushWalkerEnemiesByMovingPiston(GameState* state,
                                                const PistonDevice* piston,
                                                float previous_extension,
                                                float current_extension) {
    int enemy_count = GameRoomWalkerEnemyCount(GameCurrentRoom(state));
    for (int i = 0; i < enemy_count; ++i) {
        RectF candidate = state->walker_enemies[i];
        float move_x = 0.0f;
        float move_y = 0.0f;
        if (GameMovingPistonPushCandidate(piston,
                                          previous_extension,
                                          current_extension,
                                          &state->walker_enemies[i],
                                          &candidate,
                                          &move_x,
                                          &move_y)) {
            GameMoveWalkerEnemyWithSolid(state, i, move_x, move_y);
        }
    }
}

static void GameCarryWalkerEnemiesOnPistonPlate(GameState* state,
                                                const PistonDevice* piston,
                                                float previous_extension,
                                                float current_extension) {
    RectF previous_plate = PistonPlateRectForExtension(piston, previous_extension);
    RectF current_plate = PistonPlateRectForExtension(piston, current_extension);
    float move_x = current_plate.x - previous_plate.x;
    float move_y = current_plate.y - previous_plate.y;
    if (move_x == 0.0f && move_y == 0.0f) {
        return;
    }

    int gravity_x;
    int gravity_y;
    GamePistonGravityVector(state->gravity_direction, &gravity_x, &gravity_y);
    if (move_x * (float)gravity_x + move_y * (float)gravity_y <= 0.0f) {
        return;
    }

    int enemy_count = GameRoomWalkerEnemyCount(GameCurrentRoom(state));
    for (int i = 0; i < enemy_count; ++i) {
        if (GameRectSupportedBySolidAlongGravity(&state->walker_enemies[i], &previous_plate, gravity_x, gravity_y)) {
            GameMoveWalkerEnemyWithSolid(state, i, move_x, move_y);
        }
    }
}

static void GameCarryWalkerEnemiesOnPressurePlatform(GameState* state,
                                                      const RectF* previous_platform,
                                                      const RectF* current_platform) {
    float move_x = current_platform->x - previous_platform->x;
    float move_y = current_platform->y - previous_platform->y;
    if (move_x == 0.0f && move_y == 0.0f) {
        return;
    }

    int gravity_x;
    int gravity_y;
    GamePistonGravityVector(state->gravity_direction, &gravity_x, &gravity_y);
    int enemy_count = GameRoomWalkerEnemyCount(GameCurrentRoom(state));
    for (int i = 0; i < enemy_count; ++i) {
        if (GameRectSupportedBySolidAlongGravity(&state->walker_enemies[i], previous_platform, gravity_x, gravity_y)) {
            GameMoveWalkerEnemyWithSolid(state, i, move_x, move_y);
        }
    }
}
static int GameWalkerEnemyPlayerWithinRange(const GameState* state,
                                            const RectF* enemy,
                                            float range) {
    RectF player = GamePlayerRect(state);
    float dx = (player.x + player.w * 0.5f) - (enemy->x + enemy->w * 0.5f);
    float dy = (player.y + player.h * 0.5f) - (enemy->y + enemy->h * 0.5f);
    return dx * dx + dy * dy <= range * range;
}

static float GameWalkerTriangleEdge(float ax, float ay, float bx, float by, float px, float py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static int GameWalkerPointInRect(const RectF* rect, float x, float y) {
    return x >= rect->x && x <= rect->x + rect->w && y >= rect->y && y <= rect->y + rect->h;
}

static int GameWalkerPointInTriangle(float ax, float ay,
                                     float bx, float by,
                                     float cx, float cy,
                                     float px, float py) {
    float edge0 = GameWalkerTriangleEdge(ax, ay, bx, by, px, py);
    float edge1 = GameWalkerTriangleEdge(bx, by, cx, cy, px, py);
    float edge2 = GameWalkerTriangleEdge(cx, cy, ax, ay, px, py);
    return (edge0 >= 0.0f && edge1 >= 0.0f && edge2 >= 0.0f) ||
           (edge0 <= 0.0f && edge1 <= 0.0f && edge2 <= 0.0f);
}

static int GameWalkerPointOnSegment(float ax, float ay, float bx, float by, float px, float py) {
    static constexpr float epsilon = 0.001f;
    return px >= (ax < bx ? ax : bx) - epsilon &&
           px <= (ax > bx ? ax : bx) + epsilon &&
           py >= (ay < by ? ay : by) - epsilon &&
           py <= (ay > by ? ay : by) + epsilon;
}

static int GameWalkerSegmentsTouch(float ax, float ay, float bx, float by,
                                   float cx, float cy, float dx, float dy) {
    static constexpr float epsilon = 0.001f;
    float ab_c = GameWalkerTriangleEdge(ax, ay, bx, by, cx, cy);
    float ab_d = GameWalkerTriangleEdge(ax, ay, bx, by, dx, dy);
    float cd_a = GameWalkerTriangleEdge(cx, cy, dx, dy, ax, ay);
    float cd_b = GameWalkerTriangleEdge(cx, cy, dx, dy, bx, by);
    if (((ab_c > epsilon && ab_d < -epsilon) || (ab_c < -epsilon && ab_d > epsilon)) &&
        ((cd_a > epsilon && cd_b < -epsilon) || (cd_a < -epsilon && cd_b > epsilon))) {
        return 1;
    }
    if (ab_c >= -epsilon && ab_c <= epsilon && GameWalkerPointOnSegment(ax, ay, bx, by, cx, cy)) return 1;
    if (ab_d >= -epsilon && ab_d <= epsilon && GameWalkerPointOnSegment(ax, ay, bx, by, dx, dy)) return 1;
    if (cd_a >= -epsilon && cd_a <= epsilon && GameWalkerPointOnSegment(cx, cy, dx, dy, ax, ay)) return 1;
    if (cd_b >= -epsilon && cd_b <= epsilon && GameWalkerPointOnSegment(cx, cy, dx, dy, bx, by)) return 1;
    return 0;
}

static int GameWalkerTriangleTouchesRect(const RectF* rect,
                                         float ax, float ay,
                                         float bx, float by,
                                         float cx, float cy) {
    if (GameWalkerPointInRect(rect, ax, ay) ||
        GameWalkerPointInRect(rect, bx, by) ||
        GameWalkerPointInRect(rect, cx, cy)) {
        return 1;
    }
    float rect_left = rect->x;
    float rect_right = rect->x + rect->w;
    float rect_top = rect->y;
    float rect_bottom = rect->y + rect->h;
    if (GameWalkerPointInTriangle(ax, ay, bx, by, cx, cy, rect_left, rect_top) ||
        GameWalkerPointInTriangle(ax, ay, bx, by, cx, cy, rect_right, rect_top) ||
        GameWalkerPointInTriangle(ax, ay, bx, by, cx, cy, rect_left, rect_bottom) ||
        GameWalkerPointInTriangle(ax, ay, bx, by, cx, cy, rect_right, rect_bottom)) {
        return 1;
    }
    const float rect_edges[4][4] = {
        { rect_left, rect_top, rect_right, rect_top },
        { rect_right, rect_top, rect_right, rect_bottom },
        { rect_right, rect_bottom, rect_left, rect_bottom },
        { rect_left, rect_bottom, rect_left, rect_top },
    };
    for (int i = 0; i < 4; ++i) {
        if (GameWalkerSegmentsTouch(ax, ay, bx, by,
                                    rect_edges[i][0], rect_edges[i][1], rect_edges[i][2], rect_edges[i][3]) ||
            GameWalkerSegmentsTouch(bx, by, cx, cy,
                                    rect_edges[i][0], rect_edges[i][1], rect_edges[i][2], rect_edges[i][3]) ||
            GameWalkerSegmentsTouch(cx, cy, ax, ay,
                                    rect_edges[i][0], rect_edges[i][1], rect_edges[i][2], rect_edges[i][3])) {
            return 1;
        }
    }
    return 0;
}

static int GameWalkerTrianglesTouch(const GameWalkerTriangle* a, const GameWalkerTriangle* b) {
    if (GameWalkerPointInTriangle(a->ax, a->ay, a->bx, a->by, a->cx, a->cy, b->ax, b->ay) ||
        GameWalkerPointInTriangle(a->ax, a->ay, a->bx, a->by, a->cx, a->cy, b->bx, b->by) ||
        GameWalkerPointInTriangle(a->ax, a->ay, a->bx, a->by, a->cx, a->cy, b->cx, b->cy) ||
        GameWalkerPointInTriangle(b->ax, b->ay, b->bx, b->by, b->cx, b->cy, a->ax, a->ay) ||
        GameWalkerPointInTriangle(b->ax, b->ay, b->bx, b->by, b->cx, b->cy, a->bx, a->by) ||
        GameWalkerPointInTriangle(b->ax, b->ay, b->bx, b->by, b->cx, b->cy, a->cx, a->cy)) {
        return 1;
    }
    const float a_edges[3][4] = {
        { a->ax, a->ay, a->bx, a->by },
        { a->bx, a->by, a->cx, a->cy },
        { a->cx, a->cy, a->ax, a->ay },
    };
    const float b_edges[3][4] = {
        { b->ax, b->ay, b->bx, b->by },
        { b->bx, b->by, b->cx, b->cy },
        { b->cx, b->cy, b->ax, b->ay },
    };
    for (int a_edge = 0; a_edge < 3; ++a_edge) {
        for (int b_edge = 0; b_edge < 3; ++b_edge) {
            if (GameWalkerSegmentsTouch(a_edges[a_edge][0], a_edges[a_edge][1], a_edges[a_edge][2], a_edges[a_edge][3],
                                        b_edges[b_edge][0], b_edges[b_edge][1], b_edges[b_edge][2], b_edges[b_edge][3])) {
                return 1;
            }
        }
    }
    return 0;
}

static int GameBuildWalkerEnemySpikeTriangles(const GameState* state,
                                              int enemy_index,
                                              GameWalkerTriangle* out_triangles,
                                              int max_triangles) {
    if (!out_triangles || max_triangles <= 0 || state->walker_enemy_spike_amount[enemy_index] <= 0.01f) {
        return 0;
    }
    const RectF* enemy = &state->walker_enemies[enemy_index];
    float spike_amount = state->walker_enemy_spike_amount[enemy_index];
    float crouch = state->walker_enemy_squash_amount[enemy_index];
    float body_w = enemy->w * (1.0f - crouch * 0.08f);
    float body_x = enemy->x + enemy->w * 0.5f - body_w * 0.5f;
    float squash_px = enemy->h * 0.12f * crouch;
    float radius_x = body_w * 0.5f;
    if (radius_x < 4.0f) radius_x = 4.0f;
    float dome_height = enemy->h - 4.0f;
    if (dome_height > radius_x) dome_height = radius_x;
    float radius_y = dome_height - squash_px;
    if (radius_y < 4.0f) radius_y = 4.0f;
    float center_x = body_x + body_w * 0.5f;
    float ellipse_center_y = enemy->y + squash_px + radius_y;
    float half_width = body_w / 6.0f;
    if (half_width < 10.0f) half_width = 10.0f;
    static const float direction_x[7] = { -1.0f, -0.8660254f, -0.5f, 0.0f, 0.5f, 0.8660254f, 1.0f };
    static const float direction_y[7] = { 0.0f, -0.5f, -0.8660254f, -1.0f, -0.8660254f, -0.5f, 0.0f };
    const RoomDef* room = GameCurrentRoom(state);
    float spike_length_scale = room && enemy_index < GameRoomWalkerEnemyCount(room) &&
                               room->walker_enemies[enemy_index].spawn_code == WALKER_ENEMY_M2 ? 0.80f : 1.0f;
    static constexpr float visible_spike_length = 20.0f;
    static constexpr float root_embed_depth = 2.0f;
    float floor_y = enemy->y + enemy->h;
    int count = 0;
    for (int i = 0; i < 7; ++i) {
        float outward_x = direction_x[i];
        float outward_y = direction_y[i];
        float ellipse_scale = sqrtf((outward_x * outward_x) / (radius_x * radius_x) +
                                    (outward_y * outward_y) / (radius_y * radius_y));
        float outline_x = center_x + outward_x / ellipse_scale;
        float outline_y = ellipse_center_y + outward_y / ellipse_scale;
        float base_x = outline_x - outward_x * root_embed_depth;
        float base_y = outline_y - outward_y * root_embed_depth;
        float tip_x = outline_x + outward_x * visible_spike_length * spike_length_scale * spike_amount;
        float tip_y = outline_y + outward_y * visible_spike_length * spike_length_scale * spike_amount;
        if (i == 0 || i == 6) {
            if (count + 2 > max_triangles) break;
            float wedge_height = half_width * spike_amount;
            float cut_x = outline_x + outward_x * visible_spike_length * spike_length_scale * 0.65f * spike_amount;
            float wedge_tip_y = floor_y - wedge_height * 0.60f;
            out_triangles[count++] = { base_x, floor_y - wedge_height, base_x, floor_y, tip_x, wedge_tip_y };
            out_triangles[count++] = { base_x, floor_y, cut_x, floor_y, tip_x, wedge_tip_y };
        } else {
            if (count >= max_triangles) break;
            float tangent_x = -outward_y;
            float tangent_y = outward_x;
            out_triangles[count++] = { base_x - tangent_x * half_width, base_y - tangent_y * half_width,
                                       base_x + tangent_x * half_width, base_y + tangent_y * half_width,
                                       tip_x, tip_y };
        }
    }
    return count;
}

static int GameWalkerEnemySpikesTouchRect(const GameState* state, int enemy_index, const RectF* player) {
    float spike_amount = state->walker_enemy_spike_amount[enemy_index];
    if (spike_amount <= 0.01f) {
        return 0;
    }

    const RectF* enemy = &state->walker_enemies[enemy_index];
    float crouch = state->walker_enemy_squash_amount[enemy_index];
    float body_w = enemy->w * (1.0f - crouch * 0.08f);
    float body_x = enemy->x + enemy->w * 0.5f - body_w * 0.5f;
    float squash_px = enemy->h * 0.12f * crouch;
    float radius_x = body_w * 0.5f;
    if (radius_x < 4.0f) radius_x = 4.0f;
    float dome_height = enemy->h - 4.0f;
    if (dome_height > radius_x) dome_height = radius_x;
    float radius_y = dome_height - squash_px;
    if (radius_y < 4.0f) radius_y = 4.0f;
    float center_x = body_x + body_w * 0.5f;
    float ellipse_center_y = enemy->y + squash_px + radius_y;
    float half_width = body_w / 6.0f;
    if (half_width < 10.0f) half_width = 10.0f;

    static const float direction_x[7] = { -1.0f, -0.8660254f, -0.5f, 0.0f, 0.5f, 0.8660254f, 1.0f };
    static const float direction_y[7] = { 0.0f, -0.5f, -0.8660254f, -1.0f, -0.8660254f, -0.5f, 0.0f };
    static constexpr float visible_spike_length = 20.0f;
    const RoomDef* room = GameCurrentRoom(state);
    float spike_length_scale = room && enemy_index < GameRoomWalkerEnemyCount(room) &&
                               room->walker_enemies[enemy_index].spawn_code == WALKER_ENEMY_M2 ? 0.80f : 1.0f;
    static constexpr float root_embed_depth = 2.0f;
    float floor_y = enemy->y + enemy->h;

    for (int i = 0; i < 7; ++i) {
        float outward_x = direction_x[i];
        float outward_y = direction_y[i];
        float ellipse_scale = sqrtf((outward_x * outward_x) / (radius_x * radius_x) +
                                    (outward_y * outward_y) / (radius_y * radius_y));
        float outline_x = center_x + outward_x / ellipse_scale;
        float outline_y = ellipse_center_y + outward_y / ellipse_scale;
        float base_x = outline_x - outward_x * root_embed_depth;
        float base_y = outline_y - outward_y * root_embed_depth;
        float tip_x = outline_x + outward_x * visible_spike_length * spike_length_scale * spike_amount;
        float tip_y = outline_y + outward_y * visible_spike_length * spike_length_scale * spike_amount;

        if (i == 0 || i == 6) {
            float wedge_height = half_width * spike_amount;
            float cut_x = outline_x + outward_x * visible_spike_length * spike_length_scale * 0.65f * spike_amount;
            float wedge_tip_y = floor_y - wedge_height * 0.60f;
            if (GameWalkerTriangleTouchesRect(player,
                                              base_x, floor_y - wedge_height,
                                              base_x, floor_y,
                                              tip_x, wedge_tip_y) ||
                GameWalkerTriangleTouchesRect(player,
                                              base_x, floor_y,
                                              cut_x, floor_y,
                                              tip_x, wedge_tip_y)) {
                return 1;
            }
        } else {
            float tangent_x = -outward_y;
            float tangent_y = outward_x;
            if (GameWalkerTriangleTouchesRect(player,
                                              base_x - tangent_x * half_width,
                                              base_y - tangent_y * half_width,
                                              base_x + tangent_x * half_width,
                                              base_y + tangent_y * half_width,
                                              tip_x,
                                              tip_y)) {
                return 1;
            }
        }
    }
    return 0;
}

static int GameWalkerEnemySpikesTouchAnotherEnemy(const GameState* state, int enemy_index, int other_index) {
    const RectF* enemy = &state->walker_enemies[enemy_index];
    const RectF* other = &state->walker_enemies[other_index];
    GameWalkerTriangle triangles[9];
    GameWalkerTriangle other_triangles[9];
    int count = GameBuildWalkerEnemySpikeTriangles(state, enemy_index, triangles, 9);
    int other_count = GameBuildWalkerEnemySpikeTriangles(state, other_index, other_triangles, 9);
    for (int i = 0; i < count; ++i) {
        if (GameWalkerTriangleTouchesRect(other,
                                          triangles[i].ax, triangles[i].ay,
                                          triangles[i].bx, triangles[i].by,
                                          triangles[i].cx, triangles[i].cy)) {
            return 1;
        }
    }
    for (int i = 0; i < other_count; ++i) {
        if (GameWalkerTriangleTouchesRect(enemy,
                                          other_triangles[i].ax, other_triangles[i].ay,
                                          other_triangles[i].bx, other_triangles[i].by,
                                          other_triangles[i].cx, other_triangles[i].cy)) {
            return 1;
        }
    }
    for (int i = 0; i < count; ++i) {
        for (int other_triangle = 0; other_triangle < other_count; ++other_triangle) {
            if (GameWalkerTrianglesTouch(&triangles[i], &other_triangles[other_triangle])) {
                return 1;
            }
        }
    }
    return 0;
}

static void GameUpdateWalkerEnemies(GameState* state, float dt) {
    const RoomDef* room = GameCurrentRoom(state);
    int enemy_count = GameRoomWalkerEnemyCount(room);
    if (enemy_count <= 0) {
        return;
    }
    int gravity_x;
    int gravity_y;
    GameGravityVector(state->gravity_direction, &gravity_x, &gravity_y);
    int tangent_x = gravity_y != 0 ? 1 : 0;
    const float gravity = 1550.0f;
    const float max_gravity_speed = 1100.0f;
    for (int i = 0; i < enemy_count; ++i) {
        const WalkerEnemyDef* def = &room->walker_enemies[i];
        if (state->walker_enemies[i].w <= 0.0f || state->walker_enemies[i].h <= 0.0f) continue;
        int always_spiked = def->spawn_code == WALKER_ENEMY_M2;
        int reacts_to_player = def->spawn_code == WALKER_ENEMY_M1;
        int was_near = reacts_to_player ? state->walker_enemy_player_near[i] : 0;
        float detection_range = was_near ? WALKER_ENEMY_LEAVE_RANGE : WALKER_ENEMY_APPROACH_RANGE;
        int player_near = reacts_to_player &&
                          GameWalkerEnemyPlayerWithinRange(state,
                                                             &state->walker_enemies[i],
                                                             detection_range);
        float spike = state->walker_enemy_spike_amount[i];
        float delay = state->walker_enemy_spike_delay[i];
        float crouch = state->walker_enemy_squash_amount[i];
        float eye_crouch = state->walker_enemy_eye_crouch_amount[i];

        if (player_near) {
            // Alert is a locked stop state: crouch persists while the player remains inside leave range.
            float crouch_step = WALKER_ENEMY_CROUCH_SECONDS > 0.0f ?
                                dt / WALKER_ENEMY_CROUCH_SECONDS : 1.0f;
            crouch = GameClampF(crouch + crouch_step, 0.0f, 1.0f);
            if (!was_near) {
                delay = spike <= 0.001f ? WALKER_ENEMY_CROUCH_SECONDS : 0.0f;
            }
            if (delay > 0.0f) {
                delay -= dt;
                if (delay <= 0.0f) {
                    delay = 0.0f;
                }
            } else {
                float deploy_step = WALKER_ENEMY_SPIKE_DEPLOY_SECONDS > 0.0f ?
                                    dt / WALKER_ENEMY_SPIKE_DEPLOY_SECONDS : 1.0f;
                spike = GameClampF(spike + deploy_step, 0.0f, 1.0f);
            }
        } else {
            delay = 0.0f;
            float recovery_step = WALKER_ENEMY_SPIKE_RETRACT_SECONDS > 0.0f ? dt / WALKER_ENEMY_SPIKE_RETRACT_SECONDS : 1.0f;
            if (always_spiked) {
                spike = 1.0f;
            } else {
                spike = GameClampF(spike - recovery_step, 0.0f, 1.0f);
            }
            crouch = GameClampF(crouch - recovery_step, 0.0f, 1.0f);
        }

        // Spike collision applies only to screen X-axis movement. Gravity-axis
        // support remains body-only, so floor contact cannot reverse patrol.
        state->walker_enemy_squash_amount[i] = crouch;
        state->walker_enemy_spike_amount[i] = spike;

        float eye_step = WALKER_ENEMY_EYE_CROUCH_SECONDS > 0.0f ?
                         dt / WALKER_ENEMY_EYE_CROUCH_SECONDS : 1.0f;
        if (player_near) {
            eye_crouch = GameClampF(eye_crouch + eye_step, 0.0f, 1.0f);
        } else {
            eye_crouch = GameClampF(eye_crouch - eye_step, 0.0f, 1.0f);
        }

        // M2 enemies retain their fully deployed spikes while patrolling.
        int patrol_active = player_near == 0 && (always_spiked || spike <= 0.0f) && crouch <= 0.0f;
        if (patrol_active) {
            int direction = state->walker_enemy_direction[i] < 0 ? -1 : 1;
            float tangent_delta = (float)direction * def->move_speed * dt;
            int collided_enemy_index = -1;
            if (GameMoveWalkerEnemyAxis(state, i, tangent_x, tangent_delta, &collided_enemy_index)) {
                state->walker_enemy_direction[i] = -direction;
                state->walker_enemy_turn_squash[i] = 1.0f;
                if (collided_enemy_index >= 0) {
                    int other_direction = state->walker_enemy_direction[collided_enemy_index] < 0 ? -1 : 1;
                    state->walker_enemy_direction[collided_enemy_index] = -other_direction;
                    state->walker_enemy_turn_squash[collided_enemy_index] = 1.0f;
                }
            }
        }

        GameSpeakerPushVelocity speaker_push = GameComputeSpeakerPushVelocityForRect(
            state,
            &state->walker_enemies[i],
            state->walker_enemy_grounded[i]);
        if (GameAbsF(speaker_push.vx) > 0.001f) {
            GameMoveWalkerEnemyAxis(state, i, 1, speaker_push.vx * dt, 0);
        }
        if (GameAbsF(speaker_push.vy) > 0.001f) {
            GameMoveWalkerEnemyAxis(state, i, 0, speaker_push.vy * dt, 0);
        }
        float turn_squash = state->walker_enemy_turn_squash[i];
        float turn_step = WALKER_ENEMY_TURN_SQUASH_SECONDS > 0.0f ? dt / WALKER_ENEMY_TURN_SQUASH_SECONDS : 1.0f;
        state->walker_enemy_turn_squash[i] = GameClampF(turn_squash - turn_step, 0.0f, 1.0f);

        state->walker_enemy_grounded[i] = 0;
        if (!GameFeatureActive(state, FEATURE_GRAVITY)) {
            state->walker_enemy_gravity_speed[i] = 0.0f;
        } else {
            float gravity_speed = state->walker_enemy_gravity_speed[i] + gravity * dt;
            if (gravity_speed > max_gravity_speed) {
                gravity_speed = max_gravity_speed;
            }
            float gravity_delta = gravity_speed * dt * (float)(gravity_x != 0 ? gravity_x : gravity_y);
            if (GameMoveWalkerEnemyAxis(state, i, gravity_x != 0, gravity_delta, 0)) {
                gravity_speed = 0.0f;
                state->walker_enemy_grounded[i] = 1;
            }
            state->walker_enemy_gravity_speed[i] = gravity_speed;
        }

        state->walker_enemy_player_near[i] = player_near;
        state->walker_enemy_spike_amount[i] = spike;
        state->walker_enemy_spike_delay[i] = delay;
        state->walker_enemy_squash_amount[i] = crouch;
        state->walker_enemy_eye_crouch_amount[i] = eye_crouch;
    }
}static int GamePlayerTouchesWalkerEnemy(const GameState* state) {
    RectF player = GamePlayerRect(state);
    int enemy_count = GameRoomWalkerEnemyCount(GameCurrentRoom(state));
    for (int i = 0; i < enemy_count; ++i) {
        if (state->walker_enemies[i].w <= 0.0f || state->walker_enemies[i].h <= 0.0f) continue;
        if (RectsOverlap(&player, &state->walker_enemies[i]) ||
            GameWalkerEnemySpikesTouchRect(state, i, &player)) {
            return 1;
        }
    }
    return 0;
}
static int GameStaticSpikeTouchesRect(const StaticSpikeDef* spike, const RectF* rect) {
    if (!spike || !rect || spike->bounds.w <= 0.0f || spike->bounds.h <= 0.0f) {
        return 0;
    }
    const RectF* bounds = &spike->bounds;
    float left = bounds->x;
    float right = bounds->x + bounds->w;
    float top = bounds->y;
    float bottom = bounds->y + bounds->h;
    if (spike->rotation == STATIC_SPIKE_ROTATION_90_DEGREES) {
        return GameWalkerTriangleTouchesRect(rect, left, top, left, bottom, right, (top + bottom) * 0.5f);
    }
    if (spike->rotation == STATIC_SPIKE_ROTATION_180_DEGREES) {
        return GameWalkerTriangleTouchesRect(rect, left, top, right, top, (left + right) * 0.5f, bottom);
    }
    if (spike->rotation == STATIC_SPIKE_ROTATION_270_DEGREES) {
        return GameWalkerTriangleTouchesRect(rect, right, top, right, bottom, left, (top + bottom) * 0.5f);
    }
    return GameWalkerTriangleTouchesRect(rect, left, bottom, right, bottom, (left + right) * 0.5f, top);
}
static int GamePlayerTouchesStaticSpike(const GameState* state) {
    const RoomDef* room = GameCurrentRoom(state);
    RectF player = GamePlayerRect(state);
    int count = GameRoomStaticSpikeCount(room);
    for (int i = 0; i < count; ++i) {
        if (GameStaticSpikeTouchesRect(&room->static_spikes[i], &player)) {
            return 1;
        }
    }
    return 0;
}
static int GameBoxSolidCount(const GameState* state, const RectF* dynamic_solids, int dynamic_solid_count, const RectF* player_rect) {
    const RoomDef* room = GameCurrentRoom(state);
    return room->platform_count +
           (GameFeatureActive(state, FEATURE_COLLISION_TYPE_A) ? room->type_a_count : 0) +
           dynamic_solid_count +
           (player_rect ? 1 : 0);
}

static const RectF* GameBoxSolidAt(const GameState* state, const RectF* dynamic_solids, int dynamic_solid_count, const RectF* player_rect, int index) {
    const RoomDef* room = GameCurrentRoom(state);
    if (index < room->platform_count) {
        return &room->platforms[index];
    }
    index -= room->platform_count;
    int type_a_count = GameFeatureActive(state, FEATURE_COLLISION_TYPE_A) ? room->type_a_count : 0;
    if (index < type_a_count) {
        return &room->type_a_walls[index];
    }
    index -= type_a_count;
    if (index < dynamic_solid_count) {
        return &dynamic_solids[index];
    }
    return player_rect;
}

static void GameResolveGravityBoxAgainstSolidAxis(GameState* state, int box_index, const RectF* solid, int axis_x, int axis_y, int gravity_x, int gravity_y) {
    RectF* box = &state->gravity_boxes[box_index];
    if (!RectsOverlap(box, solid)) {
        return;
    }
    if (axis_x != 0) {
        if (state->gravity_box_vx[box_index] > 0.0f) {
            box->x = solid->x - box->w;
            if (gravity_x > 0) state->gravity_box_grounded[box_index] = 1;
        } else if (state->gravity_box_vx[box_index] < 0.0f) {
            box->x = solid->x + solid->w;
            if (gravity_x < 0) state->gravity_box_grounded[box_index] = 1;
        }
        state->gravity_box_vx[box_index] = 0.0f;
    } else if (axis_y != 0) {
        if (state->gravity_box_vy[box_index] > 0.0f) {
            box->y = solid->y - box->h;
            if (gravity_y > 0) state->gravity_box_grounded[box_index] = 1;
        } else if (state->gravity_box_vy[box_index] < 0.0f) {
            box->y = solid->y + solid->h;
            if (gravity_y < 0) state->gravity_box_grounded[box_index] = 1;
        }
        state->gravity_box_vy[box_index] = 0.0f;
    }
}

static void GameResolveGravityBoxAxisBaseSolids(GameState* state, int box_index, const RectF* dynamic_solids, int dynamic_solid_count, const RectF* player_rect, int axis_x, int axis_y, int gravity_x, int gravity_y) {
    int total = GameBoxSolidCount(state, dynamic_solids, dynamic_solid_count, player_rect);
    for (int i = 0; i < total; ++i) {
        const RectF* solid = GameBoxSolidAt(state, dynamic_solids, dynamic_solid_count, player_rect, i);
        GameResolveGravityBoxAgainstSolidAxis(state, box_index, solid, axis_x, axis_y, gravity_x, gravity_y);
    }
}

static void GameResolveGravityBoxAxis(GameState* state, int box_index, const RectF* dynamic_solids, int dynamic_solid_count, const RectF* player_rect, int axis_x, int axis_y, int gravity_x, int gravity_y) {
    GameResolveGravityBoxAxisBaseSolids(state, box_index, dynamic_solids, dynamic_solid_count, player_rect, axis_x, axis_y, gravity_x, gravity_y);

    const RoomDef* room = GameCurrentRoom(state);
    int box_count = GameRoomGravityBoxCount(room);
    for (int i = 0; i < box_count; ++i) {
        if (i == box_index) {
            continue;
        }
        GameResolveGravityBoxAgainstSolidAxis(state, box_index, &state->gravity_boxes[i], axis_x, axis_y, gravity_x, gravity_y);
    }
}
static void GameUpdateRoomGravityBoxes(GameState* state, float dt) {
    const RoomDef* room = GameCurrentRoom(state);
    int box_count = GameRoomGravityBoxCount(room);
    if (box_count <= 0) {
        return;
    }

    RectF dynamic_solids[GAME_MAX_DYNAMIC_SOLIDS];
    int dynamic_solid_count = GameBuildPistonSolids(state, room, state->piston_time_seconds, dynamic_solids, GAME_MAX_DYNAMIC_SOLIDS);
    dynamic_solid_count = GameAppendPressurePlatformSolids(state, dynamic_solids, dynamic_solid_count, GAME_MAX_DYNAMIC_SOLIDS);
    dynamic_solid_count = GameAppendPressureSwitchSolids(state, dynamic_solids, dynamic_solid_count, GAME_MAX_DYNAMIC_SOLIDS);
    RectF player_rect = GamePlayerRect(state);
    int gravity_x;
    int gravity_y;
    GameGravityVector(state->gravity_direction, &gravity_x, &gravity_y);
    float sim_dt = dt * SettingsUiGameSpeedScale();
    for (int i = 0; i < box_count; ++i) {
        if (state->gravity_box_piston_driven[i]) {
            continue;
        }
        float ax = 0.0f;
        float ay = 0.0f;
        if (GameFeatureActive(state, FEATURE_GRAVITY)) {
            ax = (float)gravity_x * GRAVITY_BOX_ACCEL;
            ay = (float)gravity_y * GRAVITY_BOX_ACCEL;
        }

        int tangent_x = gravity_y != 0 ? 1 : 0;
        int tangent_y = gravity_y != 0 ? 0 : 1;
        float gravity_speed = state->gravity_box_vx[i] * (float)gravity_x + state->gravity_box_vy[i] * (float)gravity_y;
        float tangent_speed = state->gravity_box_vx[i] * (float)tangent_x + state->gravity_box_vy[i] * (float)tangent_y;
        GameSpeakerPushVelocity speaker_push = GameComputeSpeakerPushVelocityForRect(state, &state->gravity_boxes[i], state->gravity_box_grounded[i]);
        float speaker_tangent_speed = speaker_push.vx * (float)tangent_x + speaker_push.vy * (float)tangent_y;
        float speaker_gravity_speed = speaker_push.vx * (float)gravity_x + speaker_push.vy * (float)gravity_y;
        gravity_speed += ax * (float)gravity_x * sim_dt + ay * (float)gravity_y * sim_dt;
        if (GameAbsF(speaker_tangent_speed) >= GRAVITY_BOX_SPEAKER_MIN_PUSH_SPEED ||
            GameAbsF(speaker_gravity_speed) >= GRAVITY_BOX_SPEAKER_MIN_PUSH_SPEED) {
            tangent_speed += speaker_tangent_speed;
            gravity_speed += speaker_gravity_speed;
        }
        float tangent_damping = GameClampF(1.0f - GRAVITY_BOX_TANGENT_DAMPING * sim_dt, 0.0f, 1.0f);
        tangent_speed *= tangent_damping;
        gravity_speed = GameClampF(gravity_speed, -GRAVITY_BOX_MAX_GRAVITY_SPEED, GRAVITY_BOX_MAX_GRAVITY_SPEED);
        tangent_speed = GameClampF(tangent_speed, -GRAVITY_BOX_MAX_TANGENT_SPEED, GRAVITY_BOX_MAX_TANGENT_SPEED);
        state->gravity_box_vx[i] = (float)tangent_x * tangent_speed + (float)gravity_x * gravity_speed;
        state->gravity_box_vy[i] = (float)tangent_y * tangent_speed + (float)gravity_y * gravity_speed;

        state->gravity_box_grounded[i] = 0;
        state->gravity_boxes[i].x += state->gravity_box_vx[i] * sim_dt;
        GameResolveGravityBoxAxis(state, i, dynamic_solids, dynamic_solid_count, &player_rect, 1, 0, gravity_x, gravity_y);
        state->gravity_boxes[i].y += state->gravity_box_vy[i] * sim_dt;
        GameResolveGravityBoxAxis(state, i, dynamic_solids, dynamic_solid_count, &player_rect, 0, 1, gravity_x, gravity_y);
    }
}


static int GameRangesOverlap(float a0, float a1, float b0, float b1) {
    return a0 < b1 && a1 > b0;
}

static int GamePlayerCanReachBoxOnAxis(const RectF* player_rect, const RectF* box, int axis_x, int axis_y, float amount) {
    float reach = GameAbsF(amount) + 2.0f;
    if (axis_x != 0) {
        if (!GameRangesOverlap(player_rect->y + 2.0f, player_rect->y + player_rect->h - 2.0f, box->y + 1.0f, box->y + box->h - 1.0f)) {
            return 0;
        }
        if (amount > 0.0f) {
            return player_rect->x + player_rect->w <= box->x + 0.5f && box->x - (player_rect->x + player_rect->w) <= reach;
        }
        return box->x + box->w <= player_rect->x + 0.5f && player_rect->x - (box->x + box->w) <= reach;
    }
    if (!GameRangesOverlap(player_rect->x + 2.0f, player_rect->x + player_rect->w - 2.0f, box->x + 1.0f, box->x + box->w - 1.0f)) {
        return 0;
    }
    if (amount > 0.0f) {
        return player_rect->y + player_rect->h <= box->y + 0.5f && box->y - (player_rect->y + player_rect->h) <= reach;
    }
    return box->y + box->h <= player_rect->y + 0.5f && player_rect->y - (box->y + box->h) <= reach;
}

static void GamePushGravityBoxByPlayer(GameState* state, int box_index, const RectF* dynamic_solids, int dynamic_solid_count, int axis_x, int axis_y, float amount, float dt) {
    if (amount == 0.0f || dt <= 0.0f) {
        return;
    }

    RectF saved_boxes[GAME_MAX_GRAVITY_BOXES];
    float saved_vx[GAME_MAX_GRAVITY_BOXES];
    float saved_vy[GAME_MAX_GRAVITY_BOXES];
    int saved_piston_driven[GAME_MAX_GRAVITY_BOXES];
    GameSaveGravityBoxState(state, saved_boxes, saved_vx, saved_vy, saved_piston_driven);

    RectF before = state->gravity_boxes[box_index];
    if (axis_x != 0) {
        state->gravity_boxes[box_index].x += amount;
        state->gravity_box_vx[box_index] = amount;
    } else if (axis_y != 0) {
        state->gravity_boxes[box_index].y += amount;
        state->gravity_box_vy[box_index] = amount;
    }

    int gravity_x;
    int gravity_y;
    GameGravityVector(state->gravity_direction, &gravity_x, &gravity_y);
    GameResolveGravityBoxAxisBaseSolids(state, box_index, dynamic_solids, dynamic_solid_count, 0, axis_x, axis_y, gravity_x, gravity_y);

    if (axis_x != 0) state->gravity_box_vx[box_index] = 0.0f;
    if (axis_y != 0) state->gravity_box_vy[box_index] = 0.0f;

    RectF* box = &state->gravity_boxes[box_index];
    float moved = axis_x != 0 ? box->x - before.x : box->y - before.y;
    if ((amount > 0.0f && moved <= 0.0f) || (amount < 0.0f && moved >= 0.0f)) {
        GameRestoreGravityBoxState(state, saved_boxes, saved_vx, saved_vy, saved_piston_driven);
        return;
    }

    float move_x = axis_x != 0 ? moved : 0.0f;
    float move_y = axis_y != 0 ? moved : 0.0f;
    const RoomDef* room = GameCurrentRoom(state);
    int box_count = GameRoomGravityBoxCount(room);
    for (int i = 0; i < box_count; ++i) {
        if (i == box_index) {
            continue;
        }
        if (RectsOverlap(box, &state->gravity_boxes[i])) {
            if (!GameTryMoveGravityBoxByMovingSolid(state, i, &before, box, move_x, move_y, 0, -1)) {
                GameRestoreGravityBoxState(state, saved_boxes, saved_vx, saved_vy, saved_piston_driven);
                return;
            }
        }
    }
}

static void GamePushGravityBoxesByPlayerInput(GameState* state, float move, float dt) {
    const RoomDef* room = GameCurrentRoom(state);
    int box_count = GameRoomGravityBoxCount(room);
    if (box_count <= 0 || move > -0.01f && move < 0.01f) {
        return;
    }

    int gravity_x;
    int gravity_y;
    GameGravityVector(state->gravity_direction, &gravity_x, &gravity_y);
    int tangent_x = gravity_y != 0 ? 1 : 0;
    int tangent_y = gravity_y != 0 ? 0 : 1;
    float direction = move > 0.0f ? 1.0f : -1.0f;
    float amount = GRAVITY_BOX_PUSH_SPEED * direction * dt;

    RectF dynamic_solids[GAME_MAX_DYNAMIC_SOLIDS];
    int dynamic_solid_count = GameBuildPistonSolids(state, room, state->piston_time_seconds, dynamic_solids, GAME_MAX_DYNAMIC_SOLIDS);
    dynamic_solid_count = GameAppendPressurePlatformSolids(state, dynamic_solids, dynamic_solid_count, GAME_MAX_DYNAMIC_SOLIDS);
    dynamic_solid_count = GameAppendPressureSwitchSolids(state, dynamic_solids, dynamic_solid_count, GAME_MAX_DYNAMIC_SOLIDS);
    RectF player_rect = GamePlayerRect(state);
    for (int i = 0; i < box_count; ++i) {
        if (state->gravity_box_piston_driven[i]) {
            continue;
        }
        if (GamePlayerCanReachBoxOnAxis(&player_rect, &state->gravity_boxes[i], tangent_x, tangent_y, amount)) {
            GamePushGravityBoxByPlayer(state, i, dynamic_solids, dynamic_solid_count, tangent_x, tangent_y, amount, dt);
            break;
        }
    }
}

static RectF GamePressureSwitchContactRect(const GameState* state, const PressureSwitchDevice* sw) {
    const RoomDef* room = GameCurrentRoom(state);
    const float contact_margin = 3.0f;
    RectF probe = sw->rect;
    PressureSwitchMount mount = PressureSwitchMountFor(room, sw);
    if (mount == PRESSURE_SWITCH_MOUNT_DOWN) {
        probe.y = sw->rect.y - contact_margin;
        probe.h = contact_margin * 2.0f;
    } else if (mount == PRESSURE_SWITCH_MOUNT_UP) {
        probe.y = sw->rect.y + sw->rect.h - contact_margin;
        probe.h = contact_margin * 2.0f;
    } else {
        float side_travel = GameClampF(sw->rect.w * 0.20f, 4.0f, 10.0f) + 2.0f;
        side_travel = GameClampF(side_travel, 0.0f, sw->rect.w - 4.0f);
        if (mount == PRESSURE_SWITCH_MOUNT_RIGHT) {
            probe.x = sw->rect.x + side_travel - contact_margin;
        } else {
            probe.x = sw->rect.x + sw->rect.w - side_travel - contact_margin;
        }
        probe.w = contact_margin * 2.0f;
    }
    return probe;
}

static int GamePressureSwitchTouchedByRect(const GameState* state, const PressureSwitchDevice* sw, const RectF* rect) {
    const RoomDef* room = GameCurrentRoom(state);
    const float contact_margin = 3.0f;
    RectF probe = sw->rect;
    PressureSwitchMount mount = PressureSwitchMountFor(room, sw);
    if (mount == PRESSURE_SWITCH_MOUNT_DOWN) {
        probe.y = sw->rect.y - contact_margin;
        probe.h = contact_margin * 2.0f;
    } else if (mount == PRESSURE_SWITCH_MOUNT_UP) {
        probe.y = sw->rect.y + sw->rect.h - contact_margin;
        probe.h = contact_margin * 2.0f;
    } else {
        float side_travel = GameClampF(sw->rect.w * 0.20f, 4.0f, 10.0f) + 2.0f;
        side_travel = GameClampF(side_travel, 0.0f, sw->rect.w - 4.0f);
        if (mount == PRESSURE_SWITCH_MOUNT_RIGHT) {
            probe.x = sw->rect.x + side_travel - contact_margin;
        } else {
            probe.x = sw->rect.x + sw->rect.w - side_travel - contact_margin;
        }
        probe.w = contact_margin * 2.0f;
    }
    if (probe.w <= 0.0f || probe.h <= 0.0f) {
        return 0;
    }
    return RectsOverlap(rect, &probe);
}

static int GamePressureSwitchTouchedByWalkerEnemy(const GameState* state, const PressureSwitchDevice* sw, int enemy_index) {
    const RectF* enemy = &state->walker_enemies[enemy_index];
    if (enemy->w <= 0.0f || enemy->h <= 0.0f) return 0;
    if (GamePressureSwitchTouchedByRect(state, sw, enemy)) {
        return 1;
    }
    RectF probe = GamePressureSwitchContactRect(state, sw);
    return probe.w > 0.0f && probe.h > 0.0f &&
           GameWalkerEnemySpikesTouchRect(state, enemy_index, &probe);
}

static void GameUpdateRoomPressureSwitches(GameState* state, float dt) {
    const RoomDef* room = GameCurrentRoom(state);
    int switch_count = GameRoomPressureSwitchCount(room);
    RectF pr = GamePlayerRect(state);
    int box_count = GameRoomGravityBoxCount(room);

    for (int i = 0; i < switch_count; ++i) {
        const PressureSwitchDevice* sw = &room->pressure_switches[i];
        RectF previous_switch = GamePressureSwitchSolidAt(state, i);
        int pressed = GamePressureSwitchTouchedByRect(state, sw, &pr);
        if (!pressed) {
            for (int box_index = 0; box_index < box_count; ++box_index) {
                if (GamePressureSwitchTouchedByRect(state, sw, &state->gravity_boxes[box_index])) {
                    pressed = 1;
                    break;
                }
            }
        }
        if (!pressed) {
            int piston_count = room->piston_count < GAME_MAX_PISTONS ? room->piston_count : GAME_MAX_PISTONS;
            for (int piston_index = 0; piston_index < piston_count; ++piston_index) {
                RectF plate = PistonPlateRectForExtension(&room->pistons[piston_index], state->piston_effective_extension[piston_index]);
                if (GamePressureSwitchTouchedByRect(state, sw, &plate)) {
                    pressed = 1;
                    break;
                }
            }
        }
        if (!pressed) {
            int enemy_count = GameRoomWalkerEnemyCount(room);
            for (int enemy_index = 0; enemy_index < enemy_count; ++enemy_index) {
                if (GamePressureSwitchTouchedByWalkerEnemy(state, sw, enemy_index)) {
                    pressed = 1;
                    break;
                }
            }
        }
        if (pressed != state->pressure_switch_pressed[i]) {
            state->audio_events |= GAME_AUDIO_SWITCH;
        }
        state->pressure_switch_pressed[i] = pressed;
        float previous_anim = state->pressure_switch_anim[i];
        float next_anim = GameFlexApproachF(previous_anim, pressed ? 1.0f : 0.0f, dt, 18.0f, 18.0f);
        state->pressure_switch_anim[i] = next_anim;
        if (next_anim != previous_anim) {
            RectF current_switch = GamePressureSwitchSolidAt(state, i);
            GameCarryPlayerOnPressureSwitch(state, &previous_switch, &current_switch);
        }
    }

    int unlocked = switch_count > 0 && GamePressureSwitchMaskPressed(state, 0);
    if (switch_count <= 0) {
        unlocked = !room->exit_requires_pressure_switches;
    }
    state->room_exit_unlocked = unlocked;

    int platform_count = GameRoomPressurePlatformCount(room);
    float sim_dt = dt * SettingsUiGameSpeedScale();
    for (int i = 0; i < platform_count; ++i) {
        float current = state->pressure_platform_open_amount[i];
        int platform_unlocked = GamePressureSwitchMaskPressed(state, room->pressure_platforms[i].required_switch_mask) &&
                                GameRoom09PlatformCanOpen(state, &room->pressure_platforms[i]);
        if (platform_unlocked) {
            state->pressure_platform_open_cycle_pending[i] = 1;
        }
        int must_finish_open_cycle = state->pressure_platform_open_cycle_pending[i] && current < 1.0f;
        float target = platform_unlocked || must_finish_open_cycle ? 1.0f : 0.0f;
        float pixels_per_second = target > current ?
            PRESSURE_PLATFORM_OPEN_SPEED_PIXELS_PER_SECOND :
            PRESSURE_PLATFORM_CLOSE_SPEED_PIXELS_PER_SECOND;
        float amount_per_second = GamePressurePlatformAmountPerSecond(&room->pressure_platforms[i], pixels_per_second);
        float next = GameFlexApproachF(current, target, sim_dt, amount_per_second, amount_per_second);
        if (next < current && !GamePressurePlatformTargetClear(state, i, next)) {
            next = current;
        }
        if (next != current) {
            RectF previous_platform = PressurePlatformRectAt(&room->pressure_platforms[i], current);
            RectF current_platform = PressurePlatformRectAt(&room->pressure_platforms[i], next);
            state->pressure_platform_open_amount[i] = next;
            RectF player = GamePlayerRect(state);
            RectF player_candidate = player;
            float move_x = current_platform.x - previous_platform.x;
            float move_y = current_platform.y - previous_platform.y;
            int platform_hits_switch_rider = GamePlayerSupportedByDepressedPressureSwitch(state, &player) &&
                GameMovingSolidPushCandidate(&previous_platform, &current_platform, &player, move_x, move_y, &player_candidate);
            if (platform_hits_switch_rider) {
                state->pressure_platform_open_amount[i] = current;
                next = current;
            } else {
                GameCrushWalkerEnemiesByPressurePlatform(state, &previous_platform, &current_platform);
                if (!GameTryPushPlayerByPressurePlatform(state, &previous_platform, &current_platform)) {
                    GameStartPlayerDeath(state);
                    return;
                } else {
                    GameCarryPlayerOnMovingSolid(state, &previous_platform, &current_platform);
                    GameCarryWalkerEnemiesOnPressurePlatform(state, &previous_platform, &current_platform);
                }
            }
        } else {
            state->pressure_platform_open_amount[i] = next;
        }
        if (next >= 1.0f) {
            state->pressure_platform_open_cycle_pending[i] = 0;
        }
    }
}

static void GameUpdateRoomCheckpoint(GameState* state, float dt) {
    const RoomDef* room = GameCurrentRoom(state);
    int checkpoint_count = GameRoomCheckpointCount(room);
    if (checkpoint_count <= 0) {
        return;
    }

    RectF player = GamePlayerRect(state);
    for (int index = 0; index < checkpoint_count; ++index) {
        const RectF* checkpoint = RoomCheckpointAt(room, index);
        if (checkpoint && RectsOverlap(&player, checkpoint) &&
            (!state->checkpoint_active || state->checkpoint_room != state->current_room || state->checkpoint_index != index)) {
            state->checkpoint_room = state->current_room;
            state->checkpoint_index = index;
            state->checkpoint_active = 1;
            state->checkpoint_flag_drop = 0.0f;
            break;
        }
    }
    if (state->checkpoint_active && state->checkpoint_room == state->current_room) {
        float step = CHECKPOINT_FLAG_DROP_SECONDS > 0.0f ? dt / CHECKPOINT_FLAG_DROP_SECONDS : 1.0f;
        state->checkpoint_flag_drop = GameClampF(state->checkpoint_flag_drop + step, 0.0f, 1.0f);
    }
}

static void GameStartPlayerDeath(GameState* state) {
    if (state->player_dead) {
        return;
    }

    const RoomDef* room = GameCurrentRoom(state);
    RectF pr = GamePlayerRect(state);
    float burst_x = pr.x + pr.w * 0.5f;
    float burst_y = pr.y + pr.h * 0.5f;
    burst_x = GameClampF(burst_x, room->bounds.x + 18.0f, room->bounds.x + room->bounds.w - 18.0f);
    burst_y = GameClampF(burst_y, room->bounds.y + 18.0f, room->bounds.y + room->bounds.h - 18.0f);

    state->player_dead = 1;
    state->audio_events |= GAME_AUDIO_DEATH;
    state->death_respawn_timer = GAME_DEATH_RESPAWN_DELAY;
    state->player.vx = 0.0f;
    state->player.vy = 0.0f;
    state->player.grounded = 0;
    state->player.jump_buffer_timer = 0.0f;
    state->player.coyote_timer = 0.0f;
    SpawnPlayerDeathParticles(state->player_particles,
                              PLAYER_PARTICLE_COUNT,
                              burst_x,
                              burst_y,
                              state->gravity_direction);
}

static GameSpeakerPushVelocity GameSmoothSpeakerPush(GameState* state, GameSpeakerPushVelocity target, float dt) {
    if ((target.vx > -0.001f && target.vx < 0.001f) &&
        (target.vy > -0.001f && target.vy < 0.001f)) {
        state->speaker_push_vx = 0.0f;
        state->speaker_push_vy = 0.0f;
    } else {
        float t = GameClampF(dt * SPEAKER_PUSH_SMOOTH_SPEED, 0.0f, 1.0f);
        state->speaker_push_vx += (target.vx - state->speaker_push_vx) * t;
        state->speaker_push_vy += (target.vy - state->speaker_push_vy) * t;
    }

    GameSpeakerPushVelocity result;
    result.vx = state->speaker_push_vx;
    result.vy = state->speaker_push_vy;
    return result;
}

static GameSpeakerPushVelocity GameComputeSpeakerPushVelocity(const GameState* state) {
    RectF pr = GamePlayerRect(state);
    return GameComputeSpeakerPushVelocityForRect(state, &pr, state->player.grounded);
}

static GameSpeakerPushVelocity GameComputeSpeakerPushVelocityForRect(const GameState* state, const RectF* rect, int grounded) {
    GameSpeakerPushVelocity result = {};
    const RoomDef* room = GameCurrentRoom(state);
    if (room->speaker_count <= 0) {
        return result;
    }
    float volume = SettingsUiSfxVolume();
    if (volume <= 0.001f) {
        return result;
    }

    float target_cx = rect->x + rect->w * 0.5f;
    float target_cy = rect->y + rect->h * 0.5f;
    float best_speed = 0.0f;
    float best_dir_x = -1.0f;
    float best_dir_y = 0.0f;
    for (int speaker_index = 0; speaker_index < room->speaker_count; ++speaker_index) {
        const SpeakerDevice* speaker = &room->speakers[speaker_index];
        float wave_range = SPEAKER_WAVE_RANGE * volume * speaker->wave_range_scale;
        if (wave_range <= 0.001f) {
            continue;
        }
        float source_x = speaker->x + speaker->width * 0.45f;
        float source_y = speaker->y + speaker->height * 0.66f;
        float dx = target_cx - source_x;
        float dy = target_cy - source_y;
        float dist_sq = dx * dx + dy * dy;
        if (dist_sq > wave_range * wave_range) {
            continue;
        }

        float dist = GameApproxLength(dx, dy);
        float falloff = 1.0f - dist / wave_range;
        float close_strength = falloff * falloff * falloff;
        float strength = GameClampF(SPEAKER_BASE_PUSH_STRENGTH + close_strength * SPEAKER_CLOSE_PUSH_BOOST, 0.0f, 3.35f);
        float speed = SPEAKER_PUSH_SPEED * volume * strength * speaker->push_strength_scale;
        if (speed > best_speed) {
            if (dist > 0.001f) {
                best_dir_x = dx / dist;
                best_dir_y = dy / dist;
            } else {
                best_dir_x = -1.0f;
                best_dir_y = 0.0f;
            }
            best_speed = speed;
        }
    }

    if (best_speed <= 0.0f) {
        return result;
    }

    best_dir_y *= SPEAKER_VERTICAL_PUSH_SCALE;
    float speaker_dir_len = GameApproxLength(best_dir_x, best_dir_y);
    if (speaker_dir_len <= 0.001f) {
        return result;
    }
    best_dir_x /= speaker_dir_len;
    best_dir_y /= speaker_dir_len;

    if (grounded) {
        int gravity_x;
        int gravity_y;
        GameGravityVector(state->gravity_direction, &gravity_x, &gravity_y);
        float into_ground = best_dir_x * (float)gravity_x + best_dir_y * (float)gravity_y;
        if (into_ground > 0.0f) {
            best_dir_x -= (float)gravity_x * into_ground;
            best_dir_y -= (float)gravity_y * into_ground;
            float dir_len = GameApproxLength(best_dir_x, best_dir_y);
            if (dir_len <= 0.001f) {
                return result;
            }
            best_dir_x /= dir_len;
            best_dir_y /= dir_len;
        }
    }

    float air_scale = grounded ? 1.0f : SPEAKER_AIR_PUSH_SCALE;
    best_speed *= air_scale;
    result.vx = best_dir_x * best_speed;
    result.vy = best_dir_y * best_speed;
    return result;
}

struct GameControlInput {
    float move;
    int jump_pressed;
};

static void GameApplyResolvedPlayerRect(GameState* state, const RectF* rect, int axis_x, int axis_y) {
    state->player.x = rect->x;
    state->player.y = rect->y;
    if (axis_x != 0) {
        state->player.vx = 0.0f;
    }
    if (axis_y != 0) {
        state->player.vy = 0.0f;
    }
}

static int GameResolvePlayerAgainstStaticSolidSweep(GameState* state, const RectF* previous_player, const RectF* solid, int axis_x, int axis_y) {
    RectF current = GamePlayerRect(state);
    RectF candidate = current;
    float move_x = current.x - previous_player->x;
    float move_y = current.y - previous_player->y;

    if (axis_x != 0) {
        if (!GameMovingRectsCrossAxisOverlap(previous_player, &current, solid, move_x, 0.0f) && !RectsOverlap(&current, solid)) {
            return 1;
        }
        if (move_x > 0.0f) {
            int swept = previous_player->x + previous_player->w <= solid->x && current.x + current.w > solid->x;
            if (!GameMovingRectsTouchOrOverlap(previous_player, &current, solid, swept)) {
                return 1;
            }
            candidate.x = solid->x - current.w;
        } else if (move_x < 0.0f) {
            float solid_right = solid->x + solid->w;
            int swept = previous_player->x >= solid_right && current.x < solid_right;
            if (!GameMovingRectsTouchOrOverlap(previous_player, &current, solid, swept)) {
                return 1;
            }
            candidate.x = solid_right;
        } else if (RectsOverlap(&current, solid)) {
            float push_left = current.x + current.w - solid->x;
            float push_right = solid->x + solid->w - current.x;
            candidate.x += push_left < push_right ? -push_left : push_right;
        } else {
            return 1;
        }
    } else if (axis_y != 0) {
        if (!GameMovingRectsCrossAxisOverlap(previous_player, &current, solid, 0.0f, move_y) && !RectsOverlap(&current, solid)) {
            return 1;
        }
        if (move_y > 0.0f) {
            int swept = previous_player->y + previous_player->h <= solid->y && current.y + current.h > solid->y;
            if (!GameMovingRectsTouchOrOverlap(previous_player, &current, solid, swept)) {
                return 1;
            }
            candidate.y = solid->y - current.h;
        } else if (move_y < 0.0f) {
            float solid_bottom = solid->y + solid->h;
            int swept = previous_player->y >= solid_bottom && current.y < solid_bottom;
            if (!GameMovingRectsTouchOrOverlap(previous_player, &current, solid, swept)) {
                return 1;
            }
            candidate.y = solid_bottom;
        } else if (RectsOverlap(&current, solid)) {
            float push_up = current.y + current.h - solid->y;
            float push_down = solid->y + solid->h - current.y;
            candidate.y += push_up < push_down ? -push_up : push_down;
        } else {
            return 1;
        }
    }

    GameApplyResolvedPlayerRect(state, &candidate, axis_x, axis_y);
    if (GameRectOverlapsSolids(state, &candidate)) {
        return 0;
    }
    return 1;
}

static int GameResolvePlayerAgainstPistonsAfterMovement(GameState* state, const RectF* previous_player) {
    const RoomDef* room = GameCurrentRoom(state);
    RectF piston_solids[GAME_MAX_DYNAMIC_SOLIDS];
    int piston_solid_count = GameBuildPistonSolids(state, room, state->piston_time_seconds, piston_solids, GAME_MAX_DYNAMIC_SOLIDS);
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < piston_solid_count; ++i) {
            RectF current = GamePlayerRect(state);
            float move_x = current.x - previous_player->x;
            float move_y = current.y - previous_player->y;
            if (GameAbsF(move_x) >= GameAbsF(move_y)) {
                if (!GameResolvePlayerAgainstStaticSolidSweep(state, previous_player, &piston_solids[i], 1, 0)) {
                    return 0;
                }
                if (!GameResolvePlayerAgainstStaticSolidSweep(state, previous_player, &piston_solids[i], 0, 1)) {
                    return 0;
                }
            } else {
                if (!GameResolvePlayerAgainstStaticSolidSweep(state, previous_player, &piston_solids[i], 0, 1)) {
                    return 0;
                }
                if (!GameResolvePlayerAgainstStaticSolidSweep(state, previous_player, &piston_solids[i], 1, 0)) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

static int GamePlayerOverlapsAnyPistonSolid(const GameState* state) {
    const RoomDef* room = GameCurrentRoom(state);
    RectF piston_solids[GAME_MAX_DYNAMIC_SOLIDS];
    int piston_solid_count = GameBuildPistonSolids(state, room, state->piston_time_seconds, piston_solids, GAME_MAX_DYNAMIC_SOLIDS);
    RectF pr = GamePlayerRect(state);
    for (int i = 0; i < piston_solid_count; ++i) {
        if (RectsOverlap(&pr, &piston_solids[i])) {
            return 1;
        }
    }
    return 0;
}
static GameControlInput GameReadControlInput(GravityDirection gravity_direction) {
    GameControlInput input;
    input.move = 0.0f;
    input.jump_pressed = 0;

    if (gravity_direction == GRAVITY_LEFT) {
        if (InputIsDown(KEY_UP)) input.move -= 1.0f;
        if (InputIsDown(KEY_DOWN)) input.move += 1.0f;
        input.jump_pressed = InputWasPressed(KEY_RIGHT);
    } else if (gravity_direction == GRAVITY_RIGHT) {
        if (InputIsDown(KEY_UP)) input.move -= 1.0f;
        if (InputIsDown(KEY_DOWN)) input.move += 1.0f;
        input.jump_pressed = InputWasPressed(KEY_LEFT);
    } else if (gravity_direction == GRAVITY_UP) {
        if (InputIsDown(KEY_LEFT)) input.move -= 1.0f;
        if (InputIsDown(KEY_RIGHT)) input.move += 1.0f;
        input.jump_pressed = InputWasPressed(KEY_DOWN);
    } else {
        if (InputIsDown(KEY_LEFT)) input.move -= 1.0f;
        if (InputIsDown(KEY_RIGHT)) input.move += 1.0f;
        input.jump_pressed = InputWasPressed(KEY_UP);
    }

    return input;
}

static void GameResetStageCallback(void* user_data) {
    GameResetStage((GameState*)user_data);
}

void GameUpdateStage(GameState* state, float dt, int use_static_cache) {
    state->audio_events = 0;
    SettingsUiUpdateInput(use_static_cache);
    SettingsUiUpdateFade(dt);
    if (SettingsUiIsOpen()) {
        return;
    }
    state->speaker_time_seconds += dt;
    if (ExitSequenceUpdateTransition(dt, &state->current_room, GameResetStageCallback, state)) {
        return;
    }
    if (state->player_dead) {
        if (InputWasPressed(KEY_R)) {
            GameRequestStageRestart(state);
            return;
        }
        if (SettingsUiIsOpen()) {
            SettingsUiClose();
        }
        UpdatePlayerParticles(state->player_particles,
                              PLAYER_PARTICLE_COUNT,
                              GameCurrentRoom(state),
                              dt,
                              state->gravity_direction);
        state->death_respawn_timer -= dt;
        if (state->death_respawn_timer <= 0.0f) {
            GameResetStage(state);
        }
        return;
    }
    if (InputWasPressed(KEY_R)) {
        GameRequestStageRestart(state);
        return;
    }

    for (int i = 0; i < GAME_MAX_GRAVITY_BOXES; ++i) {
        state->gravity_box_piston_driven[i] = 0;
    }

    GameUpdateRoomPistons(state, dt);
    if (state->player_dead) {
        return;
    }
    GameUpdateRoomGravityBoxes(state, dt);
    if (state->player_dead) {
        return;
    }

    GameUpdateWalkerEnemies(state, dt);
    if (GamePlayerTouchesWalkerEnemy(state) || GamePlayerTouchesStaticSpike(state)) {
        GameStartPlayerDeath(state);
        return;
    }
    GameControlInput control = {};
    if (!SettingsUiIsOpen()) {
        control = GameReadControlInput(state->gravity_direction);
    }

    GamePushGravityBoxesByPlayerInput(state, control.move, dt);

    int flexibility = SettingsUiItemValue(SettingsFlexibilityItemIndex());
    GameApplyPlayerFlexibility(state, flexibility, control.move, !state->player.grounded, GameFlexAnchorDirection(!state->player.grounded, state->gravity_direction), dt);

    int jump_active = GameFeatureActive(state, FEATURE_JUMP);

    GameSpeakerPushVelocity speaker_push = GameSmoothSpeakerPush(state, GameComputeSpeakerPushVelocity(state), dt);

    RectF dynamic_solids[GAME_MAX_DYNAMIC_SOLIDS];
    int dynamic_solid_count = GameBuildPistonSolids(state, GameCurrentRoom(state), state->piston_time_seconds, dynamic_solids, GAME_MAX_DYNAMIC_SOLIDS);
    dynamic_solid_count = GameAppendPressurePlatformSolids(state, dynamic_solids, dynamic_solid_count, GAME_MAX_DYNAMIC_SOLIDS);
    dynamic_solid_count = GameAppendPressureSwitchSolids(state, dynamic_solids, dynamic_solid_count, GAME_MAX_DYNAMIC_SOLIDS);
    dynamic_solid_count = GameAppendGravityBoxSolids(state, dynamic_solids, dynamic_solid_count, GAME_MAX_DYNAMIC_SOLIDS);

    RectF player_before_movement = GamePlayerRect(state);
    PlayerMovementFeedback movement_feedback;
    movement_feedback.type_a_contacted = state->type_a_contacted;
    movement_feedback.type_a_blocked_this_frame = state->type_a_blocked_this_frame;
    movement_feedback.type_a_bump_until = state->type_a_bump_until;
    PlayerMovementResult movement = UpdatePlayerMovement(&state->player,
                                                          GameCurrentRoom(state),
                                                          dt,
                                                          control.move,
                                                          control.jump_pressed,
                                                          jump_active,
                                                          GameFeatureActive(state, FEATURE_GRAVITY),
                                                          state->player_on_piston_support,
                                                          state->gravity_direction,
                                                          GameFeatureActive(state, FEATURE_COLLISION_TYPE_A),
                                                          dynamic_solids,
                                                          dynamic_solid_count,
                                                          speaker_push.vx,
                                                          speaker_push.vy,
                                                          PerfNowSeconds(),
                                                          &movement_feedback);
    state->type_a_contacted = movement_feedback.type_a_contacted;
    state->type_a_blocked_this_frame = movement_feedback.type_a_blocked_this_frame;
    state->type_a_bump_until = movement_feedback.type_a_bump_until;
    GameApplyPlayerFlexibility(state, flexibility, control.move, !state->player.grounded, GameFlexAnchorDirection(!state->player.grounded, state->gravity_direction), dt);
    if (!GameResolvePlayerAgainstPistonsAfterMovement(state, &player_before_movement)) {
        if (GamePlayerOverlapsAnyPistonSolid(state)) {
            GameStartPlayerDeath(state);
            return;
        }
        GameApplyResolvedPlayerRect(state, &player_before_movement, 1, 1);
    }
    if (movement.jump_started) {
        state->audio_events |= GAME_AUDIO_JUMP;
    }
    if (movement.landed) {
        state->audio_events |= GAME_AUDIO_LAND;
    }

    if (GamePlayerTouchesWalkerEnemy(state) || GamePlayerTouchesStaticSpike(state)) {
        GameStartPlayerDeath(state);
        return;
    }
    GameUpdateRoomCheckpoint(state, dt);
    UpdatePlayerPresentation(&state->player,
                             state->player_particles,
                             PLAYER_PARTICLE_COUNT,
                             GameCurrentRoom(state),
                             dt,
                             control.move,
                             movement.jump_started,
                             movement.landed,
                             GamePlayerStretchBlocked(state),
                             state->gravity_direction);
    GameUpdateRoomPressureSwitches(state, dt);
    if (state->player_dead) {
        return;
    }

    RectF pr = GamePlayerRect(state);
    RectF door_player_probe = pr;
    if (GameCurrentRoom(state)->exit_requires_pressure_switches && !state->room_exit_unlocked) {
        door_player_probe.x = GameCurrentRoom(state)->bounds.x - 10000.0f;
        door_player_probe.y = GameCurrentRoom(state)->bounds.y - 10000.0f;
    }
    ExitSequenceUpdateDoor(dt, &door_player_probe, &GameCurrentRoom(state)->exit);
    if ((!GameCurrentRoom(state)->exit_requires_pressure_switches || state->room_exit_unlocked) && RectsOverlap(&pr, &GameCurrentRoom(state)->exit)) {
        state->cleared_room_this_frame = state->current_room;
        state->audio_events |= GAME_AUDIO_CLEAR;
        ExitSequenceSetRoomSolved(1);
        SettingsUiMarkFullDirty();
        return;
    }

    if (GamePlayerOutsideRoomBounds(state)) {
        GameStartPlayerDeath(state);
        return;
    }
    pr = GamePlayerRect(state);
    CameraFollowDeadZone(&state->camera, GameCurrentRoom(state), &pr);
    TutorialUiUpdate(dt,
                     state->current_room,
                     GameFeatureActive(state, FEATURE_COLLISION_TYPE_A),
                     state->type_a_blocked_this_frame);
}

int GameTypeABumpVisible(const GameState* state) {
    return state->current_room == 1 &&
           GameFeatureActive(state, FEATURE_COLLISION_TYPE_A) &&
           PerfNowSeconds() < state->type_a_bump_until;
}

int GameTypeASettingFeedbackVisible(const GameState* state) {
    return 0;
}
