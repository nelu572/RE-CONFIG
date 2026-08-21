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
    state->gravity_direction = direction;
    state->player.vx = 0.0f;
    state->player.vy = 0.0f;
    state->player.grounded = 0;
    state->gravity_setting_feedback_until = PerfNowSeconds() + 0.22;
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
                                                          GameFeatureActive(state, FEATURE_JUMP),
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
    UpdatePlayerPresentation(&state->player,
                             state->player_particles,
                             PLAYER_PARTICLE_COUNT,
                             dt,
                             control.move,
                             movement.jump_started,
                             movement.landed,
                             state->gravity_direction);

    RectF pr = GamePlayerRect(state);
    ExitSequenceUpdateDoor(dt, &pr, &GameCurrentRoom(state)->exit);
    if (RectsOverlap(&pr, &GameCurrentRoom(state)->exit)) {
        if (state->current_room == 0) {
            ExitSequenceStartTransition(1);
            SettingsUiMarkFullDirty();
            return;
        }
        if (state->current_room == 1) {
            ExitSequenceStartTransition(2);
            SettingsUiMarkFullDirty();
            return;
        }
        if (state->current_room == 2) {
            ExitSequenceStartTransition(3);
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
