#include "stage_update.h"

#include "collision.h"
#include "exit_sequence.h"
#include "input.h"
#include "perf.h"
#include "player_movement.h"
#include "settings_ui.h"
#include "stage_cache.h"
#include "tutorial_ui.h"

static float GameClampF(float value, float lo, float hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

int GameFeatureActive(const GameState* state, DeleteFeature feature) {
    return state->delete_state.deleted[feature] == 0;
}

const RoomDef* GameCurrentRoom(const GameState* state) {
    return GetRoom(state->current_room);
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
        state->player.vy = 0.0f;
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
    state->player.x = room->player_x;
    state->player.y = room->player_y;
    state->player.vx = 0.0f;
    state->player.vy = 0.0f;
    state->player.grounded = 0;
    ResetPlayerPresentation(&state->player, state->player_particles, PLAYER_PARTICLE_COUNT);
    for (int i = 0; i < FEATURE_COUNT; ++i) {
        state->delete_state.deleted[i] = 0;
    }
    SettingsUiReset();
    ExitSequenceResetStageState();
    state->type_a_contacted = 0;
    TutorialUiResetStageState();
    state->type_a_bump_until = 0.0;
    state->type_a_setting_feedback_until = 0.0;
    state->gravity_setting_feedback_until = 0.0;
    StageCacheInvalidate();
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
    if (SettingsUiOverlayVisible()) {
        if (InputWasPressed(KEY_R)) {
            GameResetStage(state);
            return;
        }
        TutorialUiUpdate(dt,
                         state->current_room,
                         GameFeatureActive(state, FEATURE_COLLISION_TYPE_A),
                         state->type_a_blocked_this_frame);
        return;
    }

    if (InputWasPressed(KEY_R)) {
        GameResetStage(state);
        return;
    }

    float move = 0.0f;
    if (InputIsDown(KEY_LEFT)) move -= 1.0f;
    if (InputIsDown(KEY_RIGHT)) move += 1.0f;

    PlayerMovementFeedback movement_feedback;
    movement_feedback.type_a_contacted = state->type_a_contacted;
    movement_feedback.type_a_blocked_this_frame = state->type_a_blocked_this_frame;
    movement_feedback.type_a_bump_until = state->type_a_bump_until;
    PlayerMovementResult movement = UpdatePlayerMovement(&state->player,
                                                          GameCurrentRoom(state),
                                                          dt,
                                                          move,
                                                          InputWasPressed(KEY_UP),
                                                          GameFeatureActive(state, FEATURE_JUMP),
                                                          GameFeatureActive(state, FEATURE_GRAVITY),
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
                             move,
                             movement.jump_started,
                             movement.landed);

    RectF pr = GamePlayerRect(state);
    ExitSequenceUpdateDoor(dt, &pr, &GameCurrentRoom(state)->exit);
    if (RectsOverlap(&pr, &GameCurrentRoom(state)->exit)) {
        if (state->current_room == 0) {
            ExitSequenceStartTransition(1);
            SettingsUiMarkFullDirty();
            return;
        }
        ExitSequenceSetRoomSolved(1);
    }

    state->player.x = GameClampF(state->player.x, 38.0f, 1828.0f);
    if (state->player.y > 1120.0f) {
        GameResetStage(state);
    }
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
