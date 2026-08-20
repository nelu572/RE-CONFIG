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
