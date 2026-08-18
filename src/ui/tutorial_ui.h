#pragma once

#include "rect_i.h"
#include "render.h"
#include "settings_ui.h"
#include "world.h"

void TutorialUiResetStageState();
void TutorialUiCompleteTypeA();
void TutorialUiUpdate(float dt, int current_room, int type_a_active, int type_a_blocked_this_frame);
int TutorialUiFadeActive(int current_room);
int TutorialUiWorldHintVisible();
RectI TutorialUiWorldHintRect(RenderContext* render, const RoomDef* room);
RectI TutorialUiWorldHintDirtyRect(RenderContext* render, const RoomDef* room);
void TutorialUiDrawWorldHint(RenderContext* render, const RoomDef* room);
SettingsUiTutorialState TutorialUiSettingsState();
