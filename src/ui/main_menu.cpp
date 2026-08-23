#include "main_menu.h"

#include <windows.h>

#include "game_config.h"
#include "input.h"
#include "perf.h"
#include "render.h"
#include "ui_text.h"
#include "ui_text_small.h"

static uint32_t g_main_menu_background[FB_W * FB_H];
static unsigned char g_main_menu_bmp_row[FB_W * 4];
static int g_main_menu_background_loaded;
static int g_main_menu_background_load_attempted;

static uint16_t MainMenuReadU16(const unsigned char* p) {
    return (uint16_t)(((uint16_t)p[0]) | ((uint16_t)p[1] << 8));
}

static uint32_t MainMenuReadU32(const unsigned char* p) {
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int32_t MainMenuReadI32(const unsigned char* p) {
    return (int32_t)MainMenuReadU32(p);
}

static float MainMenuFrac(double value) {
    int whole = (int)value;
    if (value < 0.0 && (double)whole != value) {
        --whole;
    }
    return (float)(value - (double)whole);
}

static float MainMenuTri(double value) {
    float f = MainMenuFrac(value);
    return f < 0.5f ? f * 2.0f : (1.0f - f) * 2.0f;
}

static int MainMenuPulse(double time, double speed, int min_value, int max_value) {
    float t = MainMenuTri(time * speed);
    return min_value + (int)((max_value - min_value) * t + 0.5f);
}

static uint32_t MainMenuBrighten(uint32_t color, int amount) {
    int r = (int)((color >> 16) & 255) + amount;
    int g = (int)((color >> 8) & 255) + amount;
    int b = (int)(color & 255) + amount;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return (uint32_t)((r << 16) | (g << 8) | b);
}

static void MainMenuBlendRawPixel(RenderContext* render, int x, int y, uint32_t color, int alpha) {
    if ((unsigned)x >= (unsigned)render->width || (unsigned)y >= (unsigned)render->height || alpha <= 0) {
        return;
    }
    if (alpha > 255) {
        alpha = 255;
    }
    uint32_t* dst = render->pixels + y * render->width + x;
    uint32_t old = *dst;
    int inv = 255 - alpha;
    int sr = (int)((color >> 16) & 255);
    int sg = (int)((color >> 8) & 255);
    int sb = (int)(color & 255);
    int dr = (int)((old >> 16) & 255);
    int dg = (int)((old >> 8) & 255);
    int db = (int)(old & 255);
    int nr = (sr * alpha + dr * inv) / 255;
    int ng = (sg * alpha + dg * inv) / 255;
    int nb = (sb * alpha + db * inv) / 255;
    *dst = (uint32_t)((nr << 16) | (ng << 8) | nb);
}

static void MainMenuBlendRect(RenderContext* render, int x, int y, int w, int h, uint32_t color, int alpha) {
    int s = render->scale;
    int x0 = x * s;
    int y0 = y * s;
    int x1 = (x + w) * s;
    int y1 = (y + h) * s;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > render->width) x1 = render->width;
    if (y1 > render->height) y1 = render->height;
    for (int yy = y0; yy < y1; ++yy) {
        for (int xx = x0; xx < x1; ++xx) {
            MainMenuBlendRawPixel(render, xx, yy, color, alpha);
        }
    }
}

static int MainMenuFileExists(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static int MainMenuFindBackgroundPath(char* out, int cap) {
    static const char* paths[] = {
        "assets\\ui\\main_menu_background.bmp",
        "..\\assets\\ui\\main_menu_background.bmp",
        "..\\..\\assets\\ui\\main_menu_background.bmp",
    };
    for (int i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); ++i) {
        if (MainMenuFileExists(paths[i])) {
            DWORD len = GetFullPathNameA(paths[i], (DWORD)cap, out, 0);
            return len > 0 && len < (DWORD)cap;
        }
    }
    return 0;
}

static int MainMenuLoadBmp(const char* path) {
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }

    unsigned char header[54];
    DWORD read = 0;
    if (!ReadFile(file, header, sizeof(header), &read, 0) || read != sizeof(header)) {
        CloseHandle(file);
        return 0;
    }

    if (header[0] != 'B' || header[1] != 'M') {
        CloseHandle(file);
        return 0;
    }

    uint32_t pixel_offset = MainMenuReadU32(header + 10);
    uint32_t dib_size = MainMenuReadU32(header + 14);
    int32_t width = MainMenuReadI32(header + 18);
    int32_t signed_height = MainMenuReadI32(header + 22);
    uint16_t planes = MainMenuReadU16(header + 26);
    uint16_t bits_per_pixel = MainMenuReadU16(header + 28);
    uint32_t compression = MainMenuReadU32(header + 30);
    int top_down = signed_height < 0;
    int32_t height = top_down ? -signed_height : signed_height;
    if (dib_size < 40 ||
        width != FB_W ||
        height != FB_H ||
        planes != 1 ||
        (bits_per_pixel != 24 && bits_per_pixel != 32) ||
        compression != 0) {
        CloseHandle(file);
        return 0;
    }

    if (SetFilePointer(file, (LONG)pixel_offset, 0, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        CloseHandle(file);
        return 0;
    }

    int bytes_per_pixel = bits_per_pixel / 8;
    int row_bytes = ((FB_W * bytes_per_pixel + 3) / 4) * 4;
    for (int file_y = 0; file_y < FB_H; ++file_y) {
        if (!ReadFile(file, g_main_menu_bmp_row, (DWORD)row_bytes, &read, 0) || read != (DWORD)row_bytes) {
            CloseHandle(file);
            return 0;
        }

        int y = top_down ? file_y : (FB_H - 1 - file_y);
        uint32_t* dst = g_main_menu_background + y * FB_W;
        for (int x = 0; x < FB_W; ++x) {
            const unsigned char* px = g_main_menu_bmp_row + x * bytes_per_pixel;
            dst[x] = ((uint32_t)px[2] << 16) | ((uint32_t)px[1] << 8) | (uint32_t)px[0];
        }
    }

    CloseHandle(file);
    return 1;
}

void MainMenuInit(MainMenuState* menu) {
    menu->selected_index = 0;
    menu->action = MAIN_MENU_ACTION_NONE;
}

void MainMenuLoadBackground() {
    if (g_main_menu_background_loaded || g_main_menu_background_load_attempted) {
        return;
    }
    g_main_menu_background_load_attempted = 1;

    char path[MAX_PATH];
    if (MainMenuFindBackgroundPath(path, (int)sizeof(path)) && MainMenuLoadBmp(path)) {
        g_main_menu_background_loaded = 1;
    }
}

void MainMenuUpdate(MainMenuState* menu) {
    menu->action = MAIN_MENU_ACTION_NONE;
    if (InputWasPressed(KEY_UP) || InputWasPressed(KEY_DOWN)) {
        menu->selected_index = menu->selected_index == 0 ? 1 : 0;
    }

    if (InputWasPressed(KEY_X)) {
        menu->action = menu->selected_index == 0 ? MAIN_MENU_ACTION_START : MAIN_MENU_ACTION_EXIT;
    }
}

static void MainMenuDrawBackground(RenderContext* render, uint32_t fallback_bg) {
    if (!g_main_menu_background_loaded) {
        RenderClear(render, fallback_bg);
        return;
    }

    int s = render->scale;
    for (int y = 0; y < FB_H; ++y) {
        const uint32_t* src = g_main_menu_background + y * FB_W;
        for (int sy = 0; sy < s; ++sy) {
            uint32_t* dst = render->pixels + (y * s + sy) * render->width;
            for (int x = 0; x < FB_W; ++x) {
                uint32_t color = src[x];
                for (int sx = 0; sx < s; ++sx) {
                    dst[x * s + sx] = color;
                }
            }
        }
    }
}

static void MainMenuDrawTitle(RenderContext* render, double time, const MainMenuColors* colors) {
    const int title_x = 160;
    const int title_y = 388;
    const int title_scale = 9;
    float glitch_phase = MainMenuFrac(time * 0.32);
    int glitch = glitch_phase < 0.045f ? (glitch_phase < 0.022f ? -4 : 4) : 0;
    if (glitch) {
        UiTextSmallDraw(render, title_x - glitch, title_y + 2, "RE:", title_scale, 0x006f3038);
    }
    UiTextSmallDraw(render, title_x + glitch, title_y, "RE:", title_scale, colors->title_red);
    UiTextSmallDraw(render, title_x + 18 * title_scale, title_y, "CONFIG", title_scale, colors->title_text);
}

static void MainMenuDrawSelectionBar(RenderContext* render, int x, int y, uint32_t color, double time) {
    int pulse = MainMenuPulse(time, 0.9, 0, 36);
    uint32_t bright = MainMenuBrighten(color, pulse);
    MainMenuBlendRect(render, x - 5, y - 4, 15, 50, bright, 36);
    MainMenuBlendRect(render, x, y, 6, 42, bright, 205 + pulse);

    int scan_y = y + 4 + (int)(MainMenuFrac(time * 1.35) * 28.0f);
    MainMenuBlendRect(render, x - 1, scan_y, 8, 10, 0x00f7f0e5, 150);
}

void MainMenuDraw(RenderContext* render, const MainMenuState* menu, const MainMenuColors* colors) {
    double time = PerfNowSeconds();
    MainMenuDrawBackground(render, colors->fallback_bg);
    MainMenuDrawTitle(render, time, colors);

    const int menu_x = 160;
    const int text_x = 204;
    const int start_y = 592;
    const int exit_y = 674;
    const int text_size = 36;
    int start_selected = menu->selected_index == 0;
    int bar_y = start_selected ? start_y : exit_y;
    int pulse = MainMenuPulse(time, 0.9, 0, 36);
    uint32_t selected_color = MainMenuBrighten(colors->selected, pulse);

    MainMenuDrawSelectionBar(render, menu_x, bar_y + 1, colors->selected, time);
    DrawTextUi(text_x, start_y - 5, L"시작", text_size, start_selected ? selected_color : colors->inactive, 0);
    DrawTextUi(text_x, exit_y - 5, L"종료", text_size, start_selected ? colors->inactive : selected_color, 0);
}
