#pragma once

#include "delete_rules.h"
#include "game_state.h"
#include "world.h"

int GameFeatureActive(const GameState* state, DeleteFeature feature);
const RoomDef* GameCurrentRoom(const GameState* state);
void GameSetFeatureActive(GameState* state, DeleteFeature feature, int active);
void GameToggleFeature(GameState* state, DeleteFeature feature);
void GameSetGravityDirection(GameState* state, GravityDirection direction);
void GameSetPlayerFlexibility(GameState* state, int value);
RectF GamePlayerRect(const GameState* state);
void GameClearCheckpoint(GameState* state);
void GameResetStage(GameState* state);
void GameUpdateStage(GameState* state, float dt, int use_static_cache);
int GameTypeABumpVisible(const GameState* state);
int GameTypeASettingFeedbackVisible(const GameState* state);
