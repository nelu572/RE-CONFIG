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

static constexpr float GAME_DEATH_RESPAWN_DELAY = 0.58f;
static constexpr float SPEAKER_WAVE_RANGE = 1080.0f;
static constexpr float SPEAKER_PUSH_SPEED = 2200.0f;
static constexpr float SPEAKER_BASE_PUSH_STRENGTH = 0.10f;
static constexpr float SPEAKER_CLOSE_PUSH_BOOST = 3.25f;
static constexpr float SPEAKER_VERTICAL_PUSH_SCALE = 0.35f;
static constexpr float SPEAKER_AIR_PUSH_SCALE = 1.35f;
static constexpr float SPEAKER_PUSH_SMOOTH_SPEED = 18.0f;
static constexpr float PLAYER_FLEX_NORMAL_TANGENT = 40.0f;
static constexpr float PLAYER_FLEX_NORMAL_GRAVITY = 40.0f;
static constexpr float PLAYER_FLEX_SOFT_IDLE_TANGENT = 49.0f;
static constexpr float PLAYER_FLEX_SOFT_IDLE_GRAVITY = 30.0f;
static constexpr float PLAYER_FLEX_SOFT_MOVE_TANGENT = 52.0f;
static constexpr float PLAYER_FLEX_SOFT_MOVE_GRAVITY = 29.0f;
static constexpr float PLAYER_FLEX_SOFT_AIR_TANGENT = 28.0f;
static constexpr float PLAYER_FLEX_SOFT_AIR_GRAVITY = 53.6f;
static constexpr float PLAYER_FLEX_SOFT_APPROACH_SPEED = 95.0f;
static constexpr float PLAYER_FLEX_FIRM_GROW_SPEED = 220.0f;
static constexpr int GAME_MAX_DYNAMIC_SOLIDS = 32;
static constexpr float GRAVITY_BOX_ACCEL = 1850.0f;
static constexpr float GRAVITY_BOX_TANGENT_DAMPING = 24.0f;
static constexpr float GRAVITY_BOX_MAX_GRAVITY_SPEED = 1250.0f;
static constexpr float GRAVITY_BOX_MAX_TANGENT_SPEED = 480.0f;
static constexpr float GRAVITY_BOX_PUSH_SPEED = 120.0f;
static constexpr float GRAVITY_BOX_SPEAKER_MIN_PUSH_SPEED = 200.0f;
static constexpr float PRESSURE_PLATFORM_OPEN_SPEED = 8.0f;
static constexpr float PRESSURE_PLATFORM_CLOSE_SPEED = 12.0f;

struct GameSpeakerPushVelocity {
    float vx;
    float vy;
};

static float GameClampF(float value, float lo, float hi);
static void GameApplyPlayerFlexibility(GameState* state, int value, float move, int airborne, GravityDirection anchor_direction, float dt);
static int GameRoomGravityBoxCount(const RoomDef* room);
static int GameAppendGravityBoxSolids(const GameState* state, RectF* out_solids, int count, int max_solids);
static int GameAppendPressurePlatformSolids(const GameState* state, RectF* out_solids, int count, int max_solids);
static int GameAppendPressureSwitchSolids(const GameState* state, RectF* out_solids, int count, int max_solids);
static GameSpeakerPushVelocity GameComputeSpeakerPushVelocityForRect(const GameState* state, const RectF* rect, int grounded);
static void GameStartPlayerDeath(GameState* state);

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
    int gravity_box_count = GameRoomGravityBoxCount(GameCurrentRoom(state));
    for (int i = 0; i < gravity_box_count; ++i) {
        state->gravity_box_vx[i] = 0.0f;
        state->gravity_box_vy[i] = 0.0f;
        state->gravity_box_grounded[i] = 0;
    }
    state->gravity_setting_feedback_until = PerfNowSeconds() + 0.22;
}

static int GameBuildPistonSolids(const RoomDef* room, float piston_time_seconds, RectF* out_solids, int max_solids) {
    int count = 0;
    if (!room || !out_solids || max_solids <= 0) {
        return 0;
    }
    for (int i = 0; i < room->piston_count; ++i) {
        const PistonDevice* piston = &room->pistons[i];
        if (count < max_solids) {
            out_solids[count++] = PistonBodyRect(piston);
        }
        RectF shaft = PistonShaftRectAt(piston, piston_time_seconds);
        if (shaft.h > 0.001f && count < max_solids) {
            out_solids[count++] = shaft;
        }
        if (count < max_solids) {
            out_solids[count++] = PistonPlateRectAt(piston, piston_time_seconds);
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

static RectF GamePressurePlatformRectAt(const PressurePlatformDevice* platform, float open_amount) {
    RectF rect = platform->rect;
    open_amount = open_amount < 0.0f ? 0.0f : (open_amount > 1.0f ? 1.0f : open_amount);
    rect.x += platform->open_offset_x * open_amount;
    rect.y += platform->open_offset_y * open_amount;
    return rect;
}

static int GamePressurePlatformTargetClear(const GameState* state, int platform_index, float open_amount, const RectF* player_rect) {
    const RoomDef* room = GameCurrentRoom(state);
    RectF platform_rect = GamePressurePlatformRectAt(&room->pressure_platforms[platform_index], open_amount);
    if (player_rect && RectsOverlap(player_rect, &platform_rect)) {
        return 0;
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
        out_solids[count++] = GamePressurePlatformRectAt(&room->pressure_platforms[i], state->pressure_platform_open_amount[i]);
    }
    return count;
}

static RectF GamePressureSwitchSolidAt(const GameState* state, int switch_index) {
    const RoomDef* room = GameCurrentRoom(state);
    RectF rect = room->pressure_switches[switch_index].rect;
    float anim = GameClampF(state->pressure_switch_anim[switch_index], 0.0f, 1.0f);
    float travel = anim * 6.0f;
    if (rect.w >= rect.h) {
        rect.h = GameClampF(rect.h - travel, 4.0f, rect.h);
    } else {
        float side_travel = GameClampF(rect.w * 0.20f, 4.0f, 10.0f) + 2.0f;
        side_travel = GameClampF(side_travel, 0.0f, rect.w - 4.0f);
        rect.x += side_travel;
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
static int GameRectOverlapsSolids(const GameState* state, const RectF* rect) {
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
    RectF piston_solids[GAME_MAX_DYNAMIC_SOLIDS];
    int piston_solid_count = GameBuildPistonSolids(room, state->piston_time_seconds, piston_solids, GAME_MAX_DYNAMIC_SOLIDS);
    piston_solid_count = GameAppendPressurePlatformSolids(state, piston_solids, piston_solid_count, GAME_MAX_DYNAMIC_SOLIDS);
    piston_solid_count = GameAppendPressureSwitchSolids(state, piston_solids, piston_solid_count, GAME_MAX_DYNAMIC_SOLIDS);
    for (int i = 0; i < piston_solid_count; ++i) {
        if (RectsOverlap(rect, &piston_solids[i])) {
            return 1;
        }
    }
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

static int GamePlayerFlexSizeClear(const GameState* state, float tangent_size, float gravity_size, GravityDirection anchor_direction) {
    RectF candidate = GamePlayerFlexRectWithSize(state, tangent_size, gravity_size, anchor_direction);
    return !GameRectOverlapsSolids(state, &candidate);
}

static int GamePlayerResizeSolidCount(const RoomDef* room, int type_a_collision_active, int extra_solid_count) {
    return room->platform_count + (type_a_collision_active ? room->type_a_count : 0) + extra_solid_count;
}

static const RectF* GamePlayerResizeSolidAt(const RoomDef* room, int type_a_collision_active, const RectF* extra_solids, int index) {
    if (index < room->platform_count) {
        return &room->platforms[index];
    }
    index -= room->platform_count;
    int type_a_count = type_a_collision_active ? room->type_a_count : 0;
    if (index < type_a_count) {
        return &room->type_a_walls[index];
    }
    return &extra_solids[index - type_a_count];
}

static int GameResolvePlayerResizeRect(const GameState* state, RectF* rect, int allow_x, int allow_y) {
    if (!allow_x && !allow_y) {
        return !GameRectOverlapsSolids(state, rect);
    }
    const RoomDef* room = GameCurrentRoom(state);
    int type_a_collision_active = GameFeatureActive(state, FEATURE_COLLISION_TYPE_A);
    RectF piston_solids[GAME_MAX_DYNAMIC_SOLIDS];
    int piston_solid_count = GameBuildPistonSolids(room, state->piston_time_seconds, piston_solids, GAME_MAX_DYNAMIC_SOLIDS);
    piston_solid_count = GameAppendPressurePlatformSolids(state, piston_solids, piston_solid_count, GAME_MAX_DYNAMIC_SOLIDS);
    piston_solid_count = GameAppendPressureSwitchSolids(state, piston_solids, piston_solid_count, GAME_MAX_DYNAMIC_SOLIDS);
    piston_solid_count = GameAppendGravityBoxSolids(state, piston_solids, piston_solid_count, GAME_MAX_DYNAMIC_SOLIDS);
    int total_count = GamePlayerResizeSolidCount(room, type_a_collision_active, piston_solid_count);
    for (int pass = 0; pass < 8; ++pass) {
        float best_abs = 1000000.0f;
        float best_x = 0.0f;
        float best_y = 0.0f;
        for (int i = 0; i < total_count; ++i) {
            const RectF* solid = GamePlayerResizeSolidAt(room, type_a_collision_active, piston_solids, i);
            if (!RectsOverlap(rect, solid)) {
                continue;
            }
            float push_left = solid->x - (rect->x + rect->w);
            float push_right = (solid->x + solid->w) - rect->x;
            float push_up = solid->y - (rect->y + rect->h);
            float push_down = (solid->y + solid->h) - rect->y;
            float abs_left = push_left < 0.0f ? -push_left : push_left;
            float abs_right = push_right < 0.0f ? -push_right : push_right;
            float abs_up = push_up < 0.0f ? -push_up : push_up;
            float abs_down = push_down < 0.0f ? -push_down : push_down;
            if (allow_x && abs_left < best_abs) {
                best_abs = abs_left;
                best_x = push_left;
                best_y = 0.0f;
            }
            if (allow_x && abs_right < best_abs) {
                best_abs = abs_right;
                best_x = push_right;
                best_y = 0.0f;
            }
            if (allow_y && abs_up < best_abs) {
                best_abs = abs_up;
                best_x = 0.0f;
                best_y = push_up;
            }
            if (allow_y && abs_down < best_abs) {
                best_abs = abs_down;
                best_x = 0.0f;
                best_y = push_down;
            }
        }
        if (best_abs >= 1000000.0f) {
            break;
        }
        rect->x += best_x;
        rect->y += best_y;
    }
    return !GameRectOverlapsSolids(state, rect);
}

static int GamePlayerFlexSizeCanResolve(const GameState* state, float tangent_size, float gravity_size, GravityDirection anchor_direction, int allow_x, int allow_y) {
    RectF candidate = GamePlayerFlexRectWithSize(state, tangent_size, gravity_size, anchor_direction);
    return GameResolvePlayerResizeRect(state, &candidate, allow_x, allow_y);
}

static void GameResolvePlayerResizeOverlap(GameState* state, int allow_x, int allow_y) {
    RectF rect = GamePlayerRect(state);
    GameResolvePlayerResizeRect(state, &rect, allow_x, allow_y);
    state->player.x = rect.x;
    state->player.y = rect.y;
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
    RectF stretch = GamePlayerVisualRectForScale(state, 0.76f, 1.34f);
    return GameRectOverlapsSolids(state, &stretch);
}

static int GamePlayerSoftJumpFits(const GameState* state) {
    return GamePlayerFlexSizeClear(state,
                                   PLAYER_FLEX_SOFT_AIR_TANGENT,
                                   PLAYER_FLEX_SOFT_AIR_GRAVITY,
                                   state->gravity_direction);
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

    float next_w = tangent_size;
    float next_h = gravity_size;
    if (state->gravity_direction == GRAVITY_LEFT || state->gravity_direction == GRAVITY_RIGHT) {
        next_w = gravity_size;
        next_h = tangent_size;
    }
    int allow_x = next_w > pr.w + 0.01f;
    int allow_y = next_h > pr.h + 0.01f;

    int tangent_growing = tangent_size > current_tangent_size + 0.01f;
    int gravity_growing = gravity_size > current_gravity_size + 0.01f;
    if ((tangent_growing || gravity_growing) &&
        !GamePlayerFlexSizeCanResolve(state, tangent_size, gravity_size, anchor_direction, allow_x, allow_y)) {
        tangent_size = current_tangent_size;
        gravity_size = current_gravity_size;
        next_w = pr.w;
        next_h = pr.h;
        allow_x = 0;
        allow_y = 0;
    }
    PlayerSetCollisionSizeAnchored(&state->player, tangent_size, gravity_size, state->gravity_direction, anchor_direction);
    GameResolvePlayerResizeOverlap(state, allow_x, allow_y);
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

void GameResetStage(GameState* state) {
    const RoomDef* room = GameCurrentRoom(state);
    state->room_start_state = GameBuildRoomStartState(room);
    state->player.x = state->room_start_state.player_x;
    state->player.y = state->room_start_state.player_y;
    state->player.collision_w = PLAYER_FLEX_NORMAL_TANGENT;
    state->player.collision_h = PLAYER_FLEX_NORMAL_GRAVITY;
    state->player.vx = 0.0f;
    state->player.vy = 0.0f;
    state->player.grounded = 0;
    ResetPlayerPresentation(&state->player, state->player_particles, PLAYER_PARTICLE_COUNT);
    state->delete_state = state->room_start_state.delete_state;
    state->gravity_direction = state->room_start_state.gravity_direction;
    for (int i = 0; i < GAME_MAX_GRAVITY_BOXES; ++i) {
        state->gravity_boxes[i] = { 0.0f, 0.0f, 0.0f, 0.0f };
        state->gravity_box_vx[i] = 0.0f;
        state->gravity_box_vy[i] = 0.0f;
        state->gravity_box_grounded[i] = 0;
    }
    int box_count = GameRoomGravityBoxCount(room);
    for (int i = 0; i < box_count; ++i) {
        state->gravity_boxes[i] = room->gravity_boxes[i].start;
    }
    for (int i = 0; i < GAME_MAX_PRESSURE_SWITCHES; ++i) {
        state->pressure_switch_pressed[i] = 0;
        state->pressure_switch_anim[i] = 0.0f;
    }
    for (int i = 0; i < GAME_MAX_PRESSURE_PLATFORMS; ++i) {
        state->pressure_platform_open_amount[i] = 0.0f;
    }
    state->room_exit_unlocked = (room->pressure_switch_count > 0 || room->exit_requires_pressure_switches) ? 0 : 1;
    state->cleared_room_this_frame = -1;
    state->player_dead = 0;
    state->death_respawn_timer = 0.0f;
    SettingsUiReset();
    SettingsUiSetItemValue(SettingsGravityDirectionItemIndex(), (int)state->gravity_direction);
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
    state->speaker_push_vx = 0.0f;
    state->speaker_push_vy = 0.0f;
    state->piston_time_seconds = 0.0f;
    StageCacheInvalidate();
}

static int GamePistonHorizontallyOverlapsRect(const RectF* plate, const RectF* rect) {
    return plate->x < rect->x + rect->w && plate->x + plate->w > rect->x;
}

static int GameTryPushPlayerByMovingPiston(GameState* state, const PistonDevice* piston, float previous_piston_time, float current_piston_time) {
    PistonPose previous = PistonPoseAt(piston, previous_piston_time);
    PistonPose current = PistonPoseAt(piston, current_piston_time);
    float delta = current.extension - previous.extension;
    if (delta == 0.0f) {
        return 1;
    }

    RectF pr = GamePlayerRect(state);
    RectF current_plate = PistonPlateRectAt(piston, current_piston_time);
    if (!GamePistonHorizontallyOverlapsRect(&current_plate, &pr)) {
        return 1;
    }

    float previous_plate_top = piston->y + piston->body_height + previous.extension;
    float current_plate_top = piston->y + piston->body_height + current.extension;
    float previous_plate_bottom = previous_plate_top + piston->plate_height;
    float current_plate_bottom = current_plate_top + piston->plate_height;
    RectF candidate = pr;

    if (delta > 0.0f) {
        int swept_player_top = previous_plate_bottom <= pr.y && current_plate_bottom > pr.y;
        if (!swept_player_top && !RectsOverlap(&current_plate, &pr)) {
            return 1;
        }
        candidate.y = current_plate_bottom;
    } else {
        float player_bottom = pr.y + pr.h;
        int swept_player_bottom = previous_plate_top >= player_bottom && current_plate_top < player_bottom;
        if (!swept_player_bottom && !RectsOverlap(&current_plate, &pr)) {
            return 1;
        }
        candidate.y = current_plate_top - pr.h;
    }

    if (GameRectOverlapsSolids(state, &candidate)) {
        return 0;
    }

    state->player.y = candidate.y;
    if (delta > 0.0f && state->player.vy < 0.0f) {
        state->player.vy = 0.0f;
    } else if (delta < 0.0f && state->player.vy > 0.0f) {
        state->player.vy = 0.0f;
    }
    return 1;
}

static int GameTryPushPlayerByPistons(GameState* state, float previous_piston_time, float current_piston_time) {
    const RoomDef* room = GameCurrentRoom(state);
    if (room->piston_count <= 0) {
        return 1;
    }

    for (int i = 0; i < room->piston_count; ++i) {
        if (!GameTryPushPlayerByMovingPiston(state, &room->pistons[i], previous_piston_time, current_piston_time)) {
            return 0;
        }
    }
    return 1;
}

static void GameUpdateRoomPistons(GameState* state, float dt) {
    float previous_time = state->piston_time_seconds;
    state->piston_time_seconds += dt * SettingsUiGameSpeedScale();
    if (state->piston_time_seconds > 600.0f) {
        state->piston_time_seconds -= 600.0f;
        previous_time -= 600.0f;
    }

    if (!GameTryPushPlayerByPistons(state, previous_time, state->piston_time_seconds)) {
        GameStartPlayerDeath(state);
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

static void GameResolveGravityBoxAxis(GameState* state, int box_index, const RectF* dynamic_solids, int dynamic_solid_count, const RectF* player_rect, int axis_x, int axis_y, int gravity_x, int gravity_y) {
    RectF* box = &state->gravity_boxes[box_index];
    int total = GameBoxSolidCount(state, dynamic_solids, dynamic_solid_count, player_rect);
    for (int i = 0; i < total; ++i) {
        const RectF* solid = GameBoxSolidAt(state, dynamic_solids, dynamic_solid_count, player_rect, i);
        if (!RectsOverlap(box, solid)) {
            continue;
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
}

static void GameUpdateRoomGravityBoxes(GameState* state, float dt) {
    const RoomDef* room = GameCurrentRoom(state);
    int box_count = GameRoomGravityBoxCount(room);
    if (box_count <= 0) {
        return;
    }

    RectF dynamic_solids[GAME_MAX_DYNAMIC_SOLIDS];
    int dynamic_solid_count = GameBuildPistonSolids(room, state->piston_time_seconds, dynamic_solids, GAME_MAX_DYNAMIC_SOLIDS);
    dynamic_solid_count = GameAppendPressurePlatformSolids(state, dynamic_solids, dynamic_solid_count, GAME_MAX_DYNAMIC_SOLIDS);
    dynamic_solid_count = GameAppendPressureSwitchSolids(state, dynamic_solids, dynamic_solid_count, GAME_MAX_DYNAMIC_SOLIDS);
    RectF player_rect = GamePlayerRect(state);
    int gravity_x;
    int gravity_y;
    GameGravityVector(state->gravity_direction, &gravity_x, &gravity_y);
    float sim_dt = dt * SettingsUiGameSpeedScale();
    for (int i = 0; i < box_count; ++i) {
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
    GameResolveGravityBoxAxis(state, box_index, dynamic_solids, dynamic_solid_count, 0, axis_x, axis_y, gravity_x, gravity_y);

    if (axis_x != 0) state->gravity_box_vx[box_index] = 0.0f;
    if (axis_y != 0) state->gravity_box_vy[box_index] = 0.0f;

    RectF* box = &state->gravity_boxes[box_index];
    float moved = axis_x != 0 ? box->x - before.x : box->y - before.y;
    if ((amount > 0.0f && moved <= 0.0f) || (amount < 0.0f && moved >= 0.0f)) {
        *box = before;
        if (axis_x != 0) state->gravity_box_vx[box_index] = 0.0f;
        if (axis_y != 0) state->gravity_box_vy[box_index] = 0.0f;
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
    int dynamic_solid_count = GameBuildPistonSolids(room, state->piston_time_seconds, dynamic_solids, GAME_MAX_DYNAMIC_SOLIDS);
    dynamic_solid_count = GameAppendPressurePlatformSolids(state, dynamic_solids, dynamic_solid_count, GAME_MAX_DYNAMIC_SOLIDS);
    dynamic_solid_count = GameAppendPressureSwitchSolids(state, dynamic_solids, dynamic_solid_count, GAME_MAX_DYNAMIC_SOLIDS);
    RectF player_rect = GamePlayerRect(state);
    for (int i = 0; i < box_count; ++i) {
        if (GamePlayerCanReachBoxOnAxis(&player_rect, &state->gravity_boxes[i], tangent_x, tangent_y, amount)) {
            GamePushGravityBoxByPlayer(state, i, dynamic_solids, dynamic_solid_count, tangent_x, tangent_y, amount, dt);
            break;
        }
    }
}

static int GamePressureSwitchTouchedByRect(const PressureSwitchDevice* sw, const RectF* rect) {
    const float contact_margin = 3.0f;
    const float side_inset = 0.0f;
    RectF probe = sw->rect;
    if (sw->rect.w >= sw->rect.h) {
        probe.x += side_inset;
        probe.w -= side_inset * 2.0f;
        probe.y = sw->rect.y + sw->rect.h;
        probe.h = contact_margin;
    } else {
        float side_travel = GameClampF(sw->rect.w * 0.20f, 4.0f, 10.0f) + 2.0f;
        side_travel = GameClampF(side_travel, 0.0f, sw->rect.w - 4.0f);
        probe.x = sw->rect.x + side_travel - contact_margin;
        probe.w = contact_margin;
        probe.y += side_inset;
        probe.h -= side_inset * 2.0f;
    }
    if (probe.w <= 0.0f || probe.h <= 0.0f) {
        return 0;
    }
    return RectsOverlap(rect, &probe);
}

static void GameUpdateRoomPressureSwitches(GameState* state, float dt) {
    const RoomDef* room = GameCurrentRoom(state);
    int switch_count = GameRoomPressureSwitchCount(room);
    RectF pr = GamePlayerRect(state);
    int box_count = GameRoomGravityBoxCount(room);
    for (int i = 0; i < switch_count; ++i) {
        const PressureSwitchDevice* sw = &room->pressure_switches[i];
        int pressed = 0;
        if (sw->activator == PRESSURE_SWITCH_PLAYER || sw->activator == PRESSURE_SWITCH_ANY) {
            pressed = GamePressureSwitchTouchedByRect(sw, &pr);
        }
        if (!pressed && (sw->activator == PRESSURE_SWITCH_BOX || sw->activator == PRESSURE_SWITCH_ANY)) {
            for (int box_index = 0; box_index < box_count; ++box_index) {
                if (GamePressureSwitchTouchedByRect(sw, &state->gravity_boxes[box_index])) {
                    pressed = 1;
                    break;
                }
            }
        }
        if (pressed != state->pressure_switch_pressed[i]) {
            state->audio_events |= GAME_AUDIO_SWITCH;
        }
        state->pressure_switch_pressed[i] = pressed;
        state->pressure_switch_anim[i] = GameFlexApproachF(state->pressure_switch_anim[i], pressed ? 1.0f : 0.0f, dt, 18.0f, 18.0f);
    }

    int unlocked = switch_count <= 0 && !room->exit_requires_pressure_switches;
    if (switch_count > 0) {
        unlocked = 1;
        for (int i = 0; i < switch_count; ++i) {
            if (!state->pressure_switch_pressed[i]) {
                unlocked = 0;
                break;
            }
        }
    }
    state->room_exit_unlocked = unlocked;

    RectF pr_now = GamePlayerRect(state);
    int platform_count = GameRoomPressurePlatformCount(room);
    float sim_dt = dt * SettingsUiGameSpeedScale();
    for (int i = 0; i < platform_count; ++i) {
        float current = state->pressure_platform_open_amount[i];
        float target = unlocked ? 1.0f : 0.0f;
        float speed = unlocked ? PRESSURE_PLATFORM_OPEN_SPEED : PRESSURE_PLATFORM_CLOSE_SPEED;
        float next = GameFlexApproachF(current, target, sim_dt, speed, speed);
        if (next < current && !GamePressurePlatformTargetClear(state, i, next, &pr_now)) {
            next = current;
        }
        state->pressure_platform_open_amount[i] = next;
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
        float source_x = speaker->x + speaker->width * 0.45f;
        float source_y = speaker->y + speaker->height * 0.66f;
        float dx = target_cx - source_x;
        float dy = target_cy - source_y;
        float dist_sq = dx * dx + dy * dy;
        if (dist_sq > SPEAKER_WAVE_RANGE * SPEAKER_WAVE_RANGE) {
            continue;
        }

        float dist = GameApproxLength(dx, dy);
        float falloff = 1.0f - dist / SPEAKER_WAVE_RANGE;
        float close_strength = falloff * falloff * falloff;
        float strength = GameClampF(SPEAKER_BASE_PUSH_STRENGTH + close_strength * SPEAKER_CLOSE_PUSH_BOOST, 0.0f, 3.35f);
        float speed = SPEAKER_PUSH_SPEED * volume * strength;
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
    if (ExitSequenceUpdateTransition(dt, &state->current_room, GameResetStageCallback, state)) {
        return;
    }
    if (state->player_dead) {
        if (InputWasPressed(KEY_R)) {
            GameResetStage(state);
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
        GameResetStage(state);
        return;
    }

    GameUpdateRoomPistons(state, dt);
    GameUpdateRoomGravityBoxes(state, dt);
    if (state->player_dead) {
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
    if (jump_active &&
        control.jump_pressed &&
        state->player.grounded &&
        flexibility == SETTINGS_FLEXIBILITY_SOFT &&
        !GamePlayerSoftJumpFits(state)) {
        jump_active = 0;
    }

    GameSpeakerPushVelocity speaker_push = GameSmoothSpeakerPush(state, GameComputeSpeakerPushVelocity(state), dt);

    RectF dynamic_solids[GAME_MAX_DYNAMIC_SOLIDS];
    int dynamic_solid_count = GameBuildPistonSolids(GameCurrentRoom(state), state->piston_time_seconds, dynamic_solids, GAME_MAX_DYNAMIC_SOLIDS);
    dynamic_solid_count = GameAppendPressurePlatformSolids(state, dynamic_solids, dynamic_solid_count, GAME_MAX_DYNAMIC_SOLIDS);
    dynamic_solid_count = GameAppendPressureSwitchSolids(state, dynamic_solids, dynamic_solid_count, GAME_MAX_DYNAMIC_SOLIDS);
    dynamic_solid_count = GameAppendGravityBoxSolids(state, dynamic_solids, dynamic_solid_count, GAME_MAX_DYNAMIC_SOLIDS);

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
    if (movement.jump_started) {
        state->audio_events |= GAME_AUDIO_JUMP;
    }
    if (movement.landed) {
        state->audio_events |= GAME_AUDIO_LAND;
    }

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
