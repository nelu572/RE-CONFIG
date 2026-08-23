#include "stage_update.h"

#include "collision.h"
#include "exit_sequence.h"
#include "input.h"
#include "perf.h"
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

static void GameApplyPlayerFlexibility(GameState* state, int value, float move, int airborne, GravityDirection anchor_direction, float dt);

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
    state->gravity_setting_feedback_until = PerfNowSeconds() + 0.22;
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

static int GamePlayerResizeSolidCount(const RoomDef* room, int type_a_collision_active) {
    return room->platform_count + (type_a_collision_active ? room->type_a_count : 0);
}

static const RectF* GamePlayerResizeSolidAt(const RoomDef* room, int index) {
    if (index < room->platform_count) {
        return &room->platforms[index];
    }
    return &room->type_a_walls[index - room->platform_count];
}

static int GameResolvePlayerResizeRect(const GameState* state, RectF* rect, int allow_x, int allow_y) {
    if (!allow_x && !allow_y) {
        return !GameRectOverlapsSolids(state, rect);
    }
    const RoomDef* room = GameCurrentRoom(state);
    int total_count = GamePlayerResizeSolidCount(room, GameFeatureActive(state, FEATURE_COLLISION_TYPE_A));
    for (int pass = 0; pass < 8; ++pass) {
        float best_abs = 1000000.0f;
        float best_x = 0.0f;
        float best_y = 0.0f;
        for (int i = 0; i < total_count; ++i) {
            const RectF* solid = GamePlayerResizeSolidAt(room, i);
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
    StageCacheInvalidate();
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

struct GameSpeakerPushVelocity {
    float vx;
    float vy;
};

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
    GameSpeakerPushVelocity result = {};
    const RoomDef* room = GameCurrentRoom(state);
    if (room->speaker_count <= 0) {
        return result;
    }
    float volume = SettingsUiAudioVolume();
    if (volume <= 0.001f) {
        return result;
    }

    RectF pr = GamePlayerRect(state);
    float player_cx = pr.x + pr.w * 0.5f;
    float player_cy = pr.y + pr.h * 0.5f;
    float best_speed = 0.0f;
    float best_dir_x = -1.0f;
    float best_dir_y = 0.0f;
    for (int speaker_index = 0; speaker_index < room->speaker_count; ++speaker_index) {
        const SpeakerDevice* speaker = &room->speakers[speaker_index];
        float source_x = speaker->x + speaker->width * 0.45f;
        float source_y = speaker->y + speaker->height * 0.66f;
        float dx = player_cx - source_x;
        float dy = player_cy - source_y;
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

    if (state->player.grounded) {
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

    float air_scale = state->player.grounded ? 1.0f : SPEAKER_AIR_PUSH_SCALE;
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

    GameControlInput control = {};
    if (!SettingsUiIsOpen()) {
        control = GameReadControlInput(state->gravity_direction);
    }

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
                                                          speaker_push.vx,
                                                          speaker_push.vy,
                                                          PerfNowSeconds(),
                                                          &movement_feedback);
    state->type_a_contacted = movement_feedback.type_a_contacted;
    state->type_a_blocked_this_frame = movement_feedback.type_a_blocked_this_frame;
    state->type_a_bump_until = movement_feedback.type_a_bump_until;
    GameApplyPlayerFlexibility(state, flexibility, control.move, !state->player.grounded, GameFlexAnchorDirection(!state->player.grounded, state->gravity_direction), dt);
    UpdatePlayerPresentation(&state->player,
                             state->player_particles,
                             PLAYER_PARTICLE_COUNT,
                             dt,
                             control.move,
                             movement.jump_started,
                             movement.landed,
                             GamePlayerStretchBlocked(state),
                             state->gravity_direction);

    RectF pr = GamePlayerRect(state);
    ExitSequenceUpdateDoor(dt, &pr, &GameCurrentRoom(state)->exit);
    if (RectsOverlap(&pr, &GameCurrentRoom(state)->exit)) {
        int next_room = state->current_room + 1;
        if (next_room < RoomCount()) {
            ExitSequenceStartTransition(next_room);
            SettingsUiMarkFullDirty();
            return;
        }
        ExitSequenceSetRoomSolved(1);
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
