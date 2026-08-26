#pragma once

#include <stdint.h>

#include "render.h"

enum PauseMenuAction {
    PAUSE_MENU_ACTION_NONE,
    PAUSE_MENU_ACTION_RESUME,
    PAUSE_MENU_ACTION_RESTART,
    PAUSE_MENU_ACTION_STAGE_SELECT
};

struct PauseMenuState {
    int open;
    int selected_index;
    int selection_from_index;
    double selection_changed_at;
    double opened_at;
    PauseMenuAction action;
};

struct PauseMenuColors {
    uint32_t text;
    uint32_t text_dim;
    uint32_t accent;
};

void PauseMenuInit(PauseMenuState* menu);
void PauseMenuOpen(PauseMenuState* menu);
void PauseMenuClose(PauseMenuState* menu);
int PauseMenuIsOpen(const PauseMenuState* menu);
void PauseMenuUpdate(PauseMenuState* menu);
void PauseMenuDraw(RenderContext* render, const PauseMenuState* menu, const PauseMenuColors* colors);
