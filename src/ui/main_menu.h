#pragma once

#include <stdint.h>

#include "render.h"

enum MainMenuAction {
    MAIN_MENU_ACTION_NONE,
    MAIN_MENU_ACTION_START,
    MAIN_MENU_ACTION_EXIT
};

struct MainMenuState {
    int selected_index;
    int selection_from_index;
    double selection_changed_at;
    MainMenuAction action;
};

struct MainMenuColors {
    uint32_t fallback_bg;
    uint32_t title_red;
    uint32_t title_text;
    uint32_t selected;
    uint32_t inactive;
};

void MainMenuInit(MainMenuState* menu);
void MainMenuLoadBackground();
void MainMenuUpdate(MainMenuState* menu);
void MainMenuDraw(RenderContext* render, const MainMenuState* menu, const MainMenuColors* colors);