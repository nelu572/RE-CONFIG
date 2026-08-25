#pragma once

#include <stdint.h>

#include "rect_i.h"
#include "render.h"
#include "world.h"

typedef void (*ExitSequenceResetStageCallback)(void* user_data);
typedef void (*ExitSequenceDrawTextSmallCallback)(int x, int y, const char* text, int scale, uint32_t color);

void ExitSequenceSetExitColor(uint32_t exit_color);
void ExitSequenceResetStageState();
void ExitSequenceUpdateDoor(float dt, const RectF* player_rect, const RectF* exit_rect);
void ExitSequenceStartTransition(int room);
int ExitSequenceUpdateTransition(float dt, int* current_room, ExitSequenceResetStageCallback reset_stage, void* reset_user_data);
int ExitSequenceTransitionVisible();
void ExitSequenceSetRoomSolved(int solved);
int ExitSequenceRoomSolved();
RectF ExitSequenceVisualRect(const RectF* exit_rect);
RectI ExitSequenceDirtyRect(RenderContext* render, const RectF* exit_rect);
void ExitSequenceDrawExit(RenderContext* render, const RectF* exit_rect);
void ExitSequenceDrawSolvedUi(RenderContext* render, uint32_t text_color, ExitSequenceDrawTextSmallCallback draw_text_small);
void ExitSequenceDrawTransitionAmount(RenderContext* render, float amount);
void ExitSequenceDrawTransition(RenderContext* render);
