#pragma once

enum KeyCode {
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_X,
    KEY_F11,
    KEY_R,
    KEY_COUNT
};

void InputBeginFrame();
void InputUpdate();
int InputIsDown(KeyCode key);
int InputWasPressed(KeyCode key);
int InputWasReleased(KeyCode key);
