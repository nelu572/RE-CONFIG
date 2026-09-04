#pragma once

enum KeyCode {
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_X,
    KEY_Z,
    KEY_ESCAPE,
    KEY_F11,
    KEY_R,
    KEY_COUNT
};

void InputBeginFrame();
void InputReset();
void InputSetActive(int active);
void InputUpdate();
int InputIsDown(KeyCode key);
int InputWasPressed(KeyCode key);
int InputWasReleased(KeyCode key);
