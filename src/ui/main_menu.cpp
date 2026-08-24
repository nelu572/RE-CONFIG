#include "main_menu.h"

#include <windows.h>

#include "game_config.h"
#include "input.h"
#include "perf.h"
#include "render.h"
#include "ui_text.h"

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

static int MainMenuClampInt(int value, int min_value, int max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
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
static uint32_t MainMenuBlendColor(uint32_t a, uint32_t b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    int ar = (int)((a >> 16) & 255);
    int ag = (int)((a >> 8) & 255);
    int ab = (int)(a & 255);
    int br = (int)((b >> 16) & 255);
    int bg = (int)((b >> 8) & 255);
    int bb = (int)(b & 255);
    int r = ar + (int)(((float)(br - ar) * t) + 0.5f);
    int g = ag + (int)(((float)(bg - ag) * t) + 0.5f);
    int bl = ab + (int)(((float)(bb - ab) * t) + 0.5f);
    return (uint32_t)((r << 16) | (g << 8) | bl);
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

static void MainMenuBlendRectRaw(RenderContext* render, int x, int y, int w, int h, uint32_t color, int alpha) {
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

static void MainMenuDrawPoly4(RenderContext* render, int x0, int y0, int x1, int y1,
                              int x2, int y2, int x3, int y3, uint32_t color) {
    int xs[4] = { x0, x1, x2, x3 };
    int ys[4] = { y0, y1, y2, y3 };
    int min_y = ys[0];
    int max_y = ys[0];
    for (int i = 1; i < 4; ++i) {
        if (ys[i] < min_y) min_y = ys[i];
        if (ys[i] > max_y) max_y = ys[i];
    }

    min_y = MainMenuClampInt(min_y, 0, FB_H - 1);
    max_y = MainMenuClampInt(max_y, 0, FB_H - 1);
    for (int y = min_y; y <= max_y; ++y) {
        float scan_y = (float)y + 0.5f;
        float hits[4];
        int hit_count = 0;
        for (int i = 0; i < 4; ++i) {
            int j = (i + 1) & 3;
            float ay = (float)ys[i];
            float by = (float)ys[j];
            if ((ay <= scan_y && by > scan_y) || (by <= scan_y && ay > scan_y)) {
                float t = (scan_y - ay) / (by - ay);
                hits[hit_count++] = (float)xs[i] + ((float)xs[j] - (float)xs[i]) * t;
            }
        }

        if (hit_count >= 2) {
            if (hits[0] > hits[1]) {
                float tmp = hits[0];
                hits[0] = hits[1];
                hits[1] = tmp;
            }
            int draw_x0 = MainMenuClampInt((int)(hits[0] + 0.5f), 0, FB_W);
            int draw_x1 = MainMenuClampInt((int)(hits[1] + 0.5f), 0, FB_W);
            if (draw_x1 > draw_x0) {
                DrawRect(render, draw_x0, y, draw_x1 - draw_x0, 1, color);
            }
        }
    }
}

static void MainMenuLogoH(RenderContext* render, int x, int y, int w, int t, int slant, uint32_t color) {
    MainMenuDrawPoly4(render, x + slant, y, x + w, y, x + w - slant, y + t, x, y + t, color);
}

static void MainMenuLogoV(RenderContext* render, int x, int y, int h, int t, int slant, uint32_t color) {
    MainMenuDrawPoly4(render, x, y + slant, x + t, y, x + t, y + h - slant, x, y + h, color);
}

static void MainMenuLogoBackslash(RenderContext* render, int x, int y, int w, int h, int t, uint32_t color) {
    MainMenuDrawPoly4(render, x, y, x + t, y, x + w, y + h, x + w - t, y + h, color);
}

static void MainMenuLogoColon(RenderContext* render, int x, int y, int t, uint32_t color) {
    MainMenuDrawPoly4(render, x + 2, y + 12, x + t + 2, y + 18, x + t, y + 30, x, y + 24, color);
    MainMenuDrawPoly4(render, x + 2, y + 45, x + t + 2, y + 51, x + t, y + 63, x, y + 57, color);
}

static void MainMenuLogoStroke(RenderContext* render, int x, int y, int w, int h, uint32_t color) {
    DrawRect(render, x, y, w, h, color);
}

static void MainMenuLogoTopBar(RenderContext* render, int x, int y, int w, int t,
                               int left_bevel, int right_bevel, uint32_t color) {
    MainMenuDrawPoly4(render,
                      x + left_bevel, y,
                      x + w - right_bevel, y,
                      x + w, y + t,
                      x, y + t,
                      color);
}

static void MainMenuLogoBottomBar(RenderContext* render, int x, int y, int w, int t,
                                  int left_bevel, int right_bevel, uint32_t color) {
    MainMenuDrawPoly4(render,
                      x, y,
                      x + w, y,
                      x + w - right_bevel, y + t,
                      x + left_bevel, y + t,
                      color);
}

static void MainMenuLogoDiagDown(RenderContext* render, int x, int y, int len, int t, uint32_t color) {
    MainMenuDrawPoly4(render,
                      x, y,
                      x + t, y,
                      x + t + len, y + len,
                      x + len, y + len,
                      color);
}

static int MainMenuLogoGlyph(RenderContext* render, int x, int y, char glyph, uint32_t color) {
    const int h = 66;
    const int t = 8;
    const int bevel = t;
    const int mid_y = 31;
    const int bottom_y = h - t;

    switch (glyph) {
    case 'R': {
        const int w = 57;
        MainMenuLogoStroke(render, x, y, t, h, color);
        MainMenuLogoStroke(render, x, y, w, t, color);
        MainMenuLogoStroke(render, x + w - t, y + t, t, mid_y - t, color);
        MainMenuLogoBottomBar(render, x, y + mid_y, w, t, 0, bevel, color);
        MainMenuLogoDiagDown(render, x + 23, y + mid_y + t, 26, t, color);
        return 82;
    }
    case 'E': {
        const int w = 57;
        MainMenuLogoStroke(render, x, y, t, h, color);
        MainMenuLogoStroke(render, x, y, w, t, color);
        MainMenuLogoStroke(render, x, y + mid_y, w - 5, t, color);
        MainMenuLogoStroke(render, x, y + bottom_y, w, t, color);
        return 87;
    }
    case ':':
        MainMenuLogoStroke(render, x, y + 13, 10, 11, color);
        MainMenuLogoStroke(render, x, y + 47, 10, 11, color);
        return 39;
    case 'C': {
        const int w = 57;
        MainMenuLogoTopBar(render, x, y, w, t, bevel, 0, color);
        MainMenuLogoStroke(render, x, y + t, t, bottom_y - t, color);
        MainMenuLogoBottomBar(render, x, y + bottom_y, w, t, bevel, 0, color);
        return 81;
    }
    case 'O': {
        const int w = 60;
        MainMenuLogoTopBar(render, x, y, w, t, bevel, bevel, color);
        MainMenuLogoStroke(render, x, y + t, t, bottom_y - t, color);
        MainMenuLogoStroke(render, x + w - t, y + t, t, bottom_y - t, color);
        MainMenuLogoBottomBar(render, x, y + bottom_y, w, t, bevel, bevel, color);
        return 84;
    }
    case 'N': {
        const int w = 57;
        MainMenuLogoStroke(render, x, y, t, h, color);
        MainMenuLogoStroke(render, x + w - t, y, t, h, color);
        MainMenuDrawPoly4(render,
                          x + t - 2, y,
                          x + t * 2 + 2, y,
                          x + w - t + 2, y + h,
                          x + w - t * 2 - 2, y + h,
                          color);
        return 81;
    }
    case 'F': {
        const int w = 58;
        MainMenuLogoStroke(render, x, y, t, h, color);
        MainMenuLogoStroke(render, x, y, w, t, color);
        MainMenuLogoStroke(render, x, y + mid_y, w - 8, t, color);
        return 83;
    }
    case 'I':
        MainMenuLogoStroke(render, x, y, 40, t, color);
        MainMenuLogoStroke(render, x + 16, y, t, h, color);
        MainMenuLogoStroke(render, x, y + bottom_y, 40, t, color);
        return 64;
    case 'G': {
        const int w = 58;
        MainMenuLogoTopBar(render, x, y, w, t, bevel, bevel, color);
        MainMenuLogoStroke(render, x, y + t, t, bottom_y - t, color);
        MainMenuLogoBottomBar(render, x, y + bottom_y, w, t, bevel, bevel, color);
        MainMenuLogoStroke(render, x + w - t, y + mid_y + t, t, bottom_y - mid_y - t, color);
        MainMenuDrawPoly4(render,
                          x + 37, y + mid_y,
                          x + w, y + mid_y,
                          x + w, y + mid_y + t,
                          x + 29, y + mid_y + t,
                          color);
        return 58;
    }
    default:
        return 24;
    }
}

static void MainMenuDrawLogoText(RenderContext* render, int x, int y, const char* text, uint32_t color) {
    int cursor = x;
    for (const char* p = text; *p; ++p) {
        cursor += MainMenuLogoGlyph(render, cursor, y, *p, color);
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
    menu->selection_from_index = 0;
    menu->selection_changed_at = -1000.0;
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
        menu->selection_from_index = menu->selected_index;
        menu->selected_index = menu->selected_index == 0 ? 1 : 0;
        menu->selection_changed_at = PerfNowSeconds();
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

static void MainMenuDrawTitle(RenderContext* render, const MainMenuColors* colors) {
    const int title_y = 430;
    const int re_x = 164;
    const int config_x = 366;
    const uint32_t shadow = 0x00100b0b;
    MainMenuDrawLogoText(render, re_x + 3, title_y + 4, "RE:", shadow);
    MainMenuDrawLogoText(render, config_x + 3, title_y + 4, "CONFIG", shadow);
    MainMenuDrawLogoText(render, re_x, title_y, "RE:", colors->title_red);
    MainMenuDrawLogoText(render, config_x, title_y, "CONFIG", colors->title_text);
}

static float MainMenuEase01(float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return 1.0f - (1.0f - t) * (1.0f - t);
}

static int MainMenuLerpInt(int a, int b, float t) {
    return a + (int)(((float)(b - a) * t) + (b >= a ? 0.5f : -0.5f));
}

static void MainMenuDrawSelectionBar(RenderContext* render, int x, int center_y, uint32_t color, double time) {
    uint32_t bright = MainMenuBrighten(color, MainMenuPulse(time, 0.82, 0, 14));
    int y = center_y - 21;
    MainMenuBlendRectRaw(render, x - 3, y - 3, 10, 48, bright, 28);
    DrawRect(render, x, y, 5, 42, bright);
}

static void MainMenuDrawMenuText(int x, int center_y, const wchar_t* text, float hover, const MainMenuColors* colors, uint32_t selected_color) {
    int size = 32 + (int)(hover * 10.0f + 0.5f);
    int y = center_y - size / 2 - 5;
    uint32_t color = MainMenuBlendColor(colors->inactive, selected_color, hover);
    DrawTextUi(x + 2, y + 3, text, size, 0x00070506, 1);
    DrawTextUi(x, y, text, size, color, 1);
}

void MainMenuDraw(RenderContext* render, const MainMenuState* menu, const MainMenuColors* colors) {
    double time = PerfNowSeconds();
    MainMenuDrawBackground(render, colors->fallback_bg);
    MainMenuDrawTitle(render, colors);

    const int bar_x = 150;
    const int text_x = 206;
    const int start_center_y = 544;
    const int exit_center_y = 614;
    const float move_duration = 0.075f;
    int start_selected = menu->selected_index == 0;
    int from_y = menu->selection_from_index == 0 ? start_center_y : exit_center_y;
    int to_y = start_selected ? start_center_y : exit_center_y;
    float t = MainMenuEase01((float)((time - menu->selection_changed_at) / move_duration));
    float start_hover = start_selected ? t : (1.0f - t);
    float exit_hover = start_selected ? (1.0f - t) : t;
    int bar_y = MainMenuLerpInt(from_y, to_y, t);
    uint32_t selected_color = MainMenuBrighten(colors->selected, MainMenuPulse(time, 0.82, 0, 14));

    MainMenuDrawSelectionBar(render, bar_x, bar_y, colors->selected, time);
    MainMenuDrawMenuText(text_x, start_center_y, L"시작", start_hover, colors, selected_color);
    MainMenuDrawMenuText(text_x, exit_center_y, L"종료", exit_hover, colors, selected_color);
}