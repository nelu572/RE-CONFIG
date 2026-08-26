#include "pause_menu.h"

#include "game_config.h"
#include "input.h"
#include "perf.h"
#include "ui_text.h"

static float PauseClamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float PauseEase01(float value) {
    value = PauseClamp01(value);
    return 1.0f - (1.0f - value) * (1.0f - value);
}

static uint32_t PauseBlendColor(uint32_t a, uint32_t b, float t) {
    t = PauseClamp01(t);
    int ar = (int)((a >> 16) & 255);
    int ag = (int)((a >> 8) & 255);
    int ab = (int)(a & 255);
    int br = (int)((b >> 16) & 255);
    int bg = (int)((b >> 8) & 255);
    int bb = (int)(b & 255);
    int r = ar + (int)((float)(br - ar) * t + 0.5f);
    int g = ag + (int)((float)(bg - ag) * t + 0.5f);
    int bl = ab + (int)((float)(bb - ab) * t + 0.5f);
    return (uint32_t)((r << 16) | (g << 8) | bl);
}

static void PauseBlendRect(RenderContext* render, int x, int y, int w, int h, uint32_t color, float alpha) {
    int a = (int)(PauseClamp01(alpha) * 256.0f + 0.5f);
    if (a <= 0 || w <= 0 || h <= 0) {
        return;
    }
    if (a > 256) a = 256;

    int sx = x * render->scale;
    int sy = y * render->scale;
    int sw = w * render->scale;
    int sh = h * render->scale;
    if (sx < 0) { sw += sx; sx = 0; }
    if (sy < 0) { sh += sy; sy = 0; }
    if (sx + sw > render->width) sw = render->width - sx;
    if (sy + sh > render->height) sh = render->height - sy;
    if (sw <= 0 || sh <= 0) {
        return;
    }

    int sr = (int)((color >> 16) & 255);
    int sg = (int)((color >> 8) & 255);
    int sb = (int)(color & 255);
    int inv = 256 - a;
    for (int yy = sy; yy < sy + sh; ++yy) {
        uint32_t* row = render->pixels + yy * render->width + sx;
        for (int xx = 0; xx < sw; ++xx) {
            uint32_t dst = row[xx];
            int dr = (int)((dst >> 16) & 255);
            int dg = (int)((dst >> 8) & 255);
            int db = (int)(dst & 255);
            row[xx] = (uint32_t)((((sr * a + dr * inv) >> 8) << 16) |
                                 (((sg * a + dg * inv) >> 8) << 8) |
                                 ((sb * a + db * inv) >> 8));
        }
    }
}

void PauseMenuInit(PauseMenuState* menu) {
    menu->open = 0;
    menu->selected_index = 0;
    menu->selection_from_index = 0;
    menu->selection_changed_at = -1000.0;
    menu->opened_at = -1000.0;
    menu->action = PAUSE_MENU_ACTION_NONE;
}

void PauseMenuOpen(PauseMenuState* menu) {
    menu->open = 1;
    menu->selected_index = 0;
    menu->selection_from_index = 0;
    menu->selection_changed_at = PerfNowSeconds();
    menu->opened_at = menu->selection_changed_at;
    menu->action = PAUSE_MENU_ACTION_NONE;
}

void PauseMenuClose(PauseMenuState* menu) {
    menu->open = 0;
    menu->action = PAUSE_MENU_ACTION_NONE;
}

int PauseMenuIsOpen(const PauseMenuState* menu) {
    return menu->open;
}

void PauseMenuUpdate(PauseMenuState* menu) {
    menu->action = PAUSE_MENU_ACTION_NONE;
    if (!menu->open) {
        return;
    }

    if (InputWasPressed(KEY_X) || InputWasPressed(KEY_ESCAPE)) {
        menu->action = PAUSE_MENU_ACTION_RESUME;
        return;
    }
    if (InputWasPressed(KEY_UP)) {
        menu->selection_from_index = menu->selected_index;
        --menu->selected_index;
        if (menu->selected_index < 0) {
            menu->selected_index = 2;
        }
        menu->selection_changed_at = PerfNowSeconds();
    } else if (InputWasPressed(KEY_DOWN)) {
        menu->selection_from_index = menu->selected_index;
        ++menu->selected_index;
        if (menu->selected_index > 2) {
            menu->selected_index = 0;
        }
        menu->selection_changed_at = PerfNowSeconds();
    }

    if (InputWasPressed(KEY_Z)) {
        menu->action = (PauseMenuAction)(PAUSE_MENU_ACTION_RESUME + menu->selected_index);
    }
}

static int PauseTextWidthApprox(const wchar_t* text, int size) {
    int width = 0;
    for (const wchar_t* p = text; *p; ++p) {
        if (*p < 128) {
            width += size * 3 / 5;
        } else {
            width += size;
        }
    }
    return width;
}

static void PauseDrawCenteredText(RenderContext* render,
                                  int center_x,
                                  int y,
                                  const wchar_t* text,
                                  int size,
                                  uint32_t color,
                                  int bold) {
    int x = center_x - PauseTextWidthApprox(text, size) / 2;
    DrawTextUi(x + 1, y + 2, text, size, 0x00070607, bold);
    DrawTextUi(x, y, text, size, color, bold);
}

static void PauseMenuDrawItem(RenderContext* render,
                              int center_x,
                              int center_y,
                              const wchar_t* text,
                              float focus,
                              float appear) {
    int size = 34 + (int)(focus * 6.0f + 0.5f);
    uint32_t base_color = PauseBlendColor(0x00a69c91, 0x0039cfc3, focus);
    uint32_t color = PauseBlendColor(0x0010090b, base_color, appear);
    int y = center_y - size / 2 - 3;
    PauseDrawCenteredText(render, center_x, y, text, size, color, focus > 0.5f);
}

void PauseMenuDraw(RenderContext* render, const PauseMenuState* menu, const PauseMenuColors* colors) {
    (void)colors;
    if (!menu->open) {
        return;
    }

    double now = PerfNowSeconds();
    float appear = PauseEase01((float)((now - menu->opened_at) / 0.16));
    PauseBlendRect(render, 0, 0, FB_W, FB_H, 0x00090507, 0.54f * appear);

    const int menu_x = FB_W / 2 + 290;
    const int menu_y = FB_H / 2 + 36;
    const int row_h = 88;

    const float duration = 0.110f;
    float t = PauseEase01((float)((now - menu->selection_changed_at) / duration));

    const wchar_t* labels[3] = { L"이어하기", L"다시하기", L"스테이지 선택" };
    for (int i = 0; i < 3; ++i) {
        float focus = i == menu->selected_index ? t : (i == menu->selection_from_index ? 1.0f - t : 0.0f);
        PauseMenuDrawItem(render, menu_x, menu_y + (i - 1) * row_h, labels[i], focus, appear);
    }
}