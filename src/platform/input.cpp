#include "input.h"

#include <windows.h>

struct InputState {
    unsigned char down[KEY_COUNT];
    unsigned char prev[KEY_COUNT];
};

static InputState g_input;
static int g_input_active = 1;
static int g_sync_on_activate = 0;

static int KeyDown(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

void InputBeginFrame() {
    for (int i = 0; i < KEY_COUNT; ++i) {
        g_input.prev[i] = g_input.down[i];
    }
}

void InputReset() {
    for (int i = 0; i < KEY_COUNT; ++i) {
        g_input.down[i] = 0;
        g_input.prev[i] = 0;
    }
}

void InputSetActive(int active) {
    active = active != 0;
    if (!active) {
        g_input_active = 0;
        g_sync_on_activate = 0;
        InputReset();
        return;
    }

    if (!g_input_active) {
        g_input_active = 1;
        g_sync_on_activate = 1;
    }
}

void InputUpdate() {
    if (!g_input_active) {
        InputReset();
        return;
    }

    g_input.down[KEY_UP] = KeyDown(VK_UP);
    g_input.down[KEY_DOWN] = KeyDown(VK_DOWN);
    g_input.down[KEY_LEFT] = KeyDown(VK_LEFT);
    g_input.down[KEY_RIGHT] = KeyDown(VK_RIGHT);
    g_input.down[KEY_X] = KeyDown('Z');
    g_input.down[KEY_Z] = KeyDown('X');
    g_input.down[KEY_ESCAPE] = KeyDown(VK_ESCAPE);
    g_input.down[KEY_F11] = KeyDown(VK_F11);
    g_input.down[KEY_R] = KeyDown('R');

    if (g_sync_on_activate) {
        for (int i = 0; i < KEY_COUNT; ++i) {
            g_input.prev[i] = g_input.down[i];
        }
        g_sync_on_activate = 0;
    }
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
