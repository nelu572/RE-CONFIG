#include "input.h"

#include <windows.h>

struct InputState {
    unsigned char down[KEY_COUNT];
    unsigned char prev[KEY_COUNT];
};

static InputState g_input;

static int KeyDown(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

void InputBeginFrame() {
    for (int i = 0; i < KEY_COUNT; ++i) {
        g_input.prev[i] = g_input.down[i];
    }
}

void InputUpdate() {
    g_input.down[KEY_UP] = KeyDown(VK_UP);
    g_input.down[KEY_DOWN] = KeyDown(VK_DOWN);
    g_input.down[KEY_LEFT] = KeyDown(VK_LEFT);
    g_input.down[KEY_RIGHT] = KeyDown(VK_RIGHT);
    g_input.down[KEY_X] = KeyDown('X');
    g_input.down[KEY_F11] = KeyDown(VK_F11);
    g_input.down[KEY_R] = KeyDown('R');
}

int InputIsDown(KeyCode key) {
    return g_input.down[key] != 0;
}

int InputWasPressed(KeyCode key) {
    return g_input.down[key] && !g_input.prev[key];
}

int InputWasReleased(KeyCode key) {
    return !g_input.down[key] && g_input.prev[key];
}
