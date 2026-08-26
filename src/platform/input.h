#pragma once

enum KeyCode {
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_X,
    KEY_ESCAPE,
    KEY_F11,
    KEY_R,
    KEY_0,
    KEY_1,
    KEY_2,
    KEY_3,
    KEY_4,
    KEY_COUNT
};

void InputBeginFrame();
void InputUpdate();
int InputIsDown(KeyCode key);
int InputWasPressed(KeyCode key);
int InputWasReleased(KeyCode key);
