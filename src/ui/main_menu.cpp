#include "main_menu.h"

#include <windows.h>

#include "game_config.h"
#include "input.h"
#include "math_util.h"
#include "main_menu_embedded_bg.h"
#include "perf.h"
#include "render.h"
#include "ui_text.h"

static uint32_t g_main_menu_background[FB_W * FB_H];
static unsigned char g_main_menu_bmp_row[FB_W * 4];
static int g_main_menu_huff_child0[8192];
static int g_main_menu_huff_child1[8192];
static int g_main_menu_huff_symbol[8192];
static int g_main_menu_background_loaded;
static int g_main_menu_background_load_attempted;


enum MainMenuUpscaleMode {
    MAIN_MENU_UPSCALE_BILINEAR,
    MAIN_MENU_UPSCALE_BICUBIC,
    MAIN_MENU_UPSCALE_LANCZOS
};

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


static int MainMenuFindCompressedBackgroundPath(char* out, int cap) {
    char env_path[MAX_PATH];
    DWORD env_len = GetEnvironmentVariableA("RECONFIG_MENU_BGC", env_path, (DWORD)sizeof(env_path));
    if (env_len > 0 && env_len < sizeof(env_path) && MainMenuFileExists(env_path)) {
        DWORD len = GetFullPathNameA(env_path, (DWORD)cap, out, 0);
        return len > 0 && len < (DWORD)cap;
    }

    static const char* paths[] = {
        "assets\\ui\\main_menu_background.bgc",
        "..\\assets\\ui\\main_menu_background.bgc",
        "..\\..\\assets\\ui\\main_menu_background.bgc",
    };
    for (int i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); ++i) {
        if (MainMenuFileExists(paths[i])) {
            DWORD len = GetFullPathNameA(paths[i], (DWORD)cap, out, 0);
            return len > 0 && len < (DWORD)cap;
        }
    }
    return 0;
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




static uint32_t MainMenuRgb(int r, int g, int b) {
    r = MainMenuClampInt(r, 0, 255);
    g = MainMenuClampInt(g, 0, 255);
    b = MainMenuClampInt(b, 0, 255);
    return (uint32_t)((r << 16) | (g << 8) | b);
}

static unsigned int MainMenuHashPixel(int x, int y, unsigned int seed) {
    unsigned int v = (unsigned int)x * 1973u + (unsigned int)y * 9277u + seed * 26699u + 0x68bc21ebu;
    v ^= v >> 13;
    v *= 1274126177u;
    v ^= v >> 16;
    return v;
}

static void MainMenuBackgroundBlendPixel(int x, int y, uint32_t color, int alpha) {
    if ((unsigned)x >= (unsigned)FB_W || (unsigned)y >= (unsigned)FB_H || alpha <= 0) {
        return;
    }
    if (alpha > 255) alpha = 255;
    uint32_t* dst = g_main_menu_background + y * FB_W + x;
    uint32_t old = *dst;
    int inv = 255 - alpha;
    int sr = (int)((color >> 16) & 255);
    int sg = (int)((color >> 8) & 255);
    int sb = (int)(color & 255);
    int dr = (int)((old >> 16) & 255);
    int dg = (int)((old >> 8) & 255);
    int db = (int)(old & 255);
    *dst = MainMenuRgb((sr * alpha + dr * inv) / 255,
                       (sg * alpha + dg * inv) / 255,
                       (sb * alpha + db * inv) / 255);
}

static void MainMenuBackgroundBlendRect(int x, int y, int w, int h, uint32_t color, int alpha) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > FB_W) w = FB_W - x;
    if (y + h > FB_H) h = FB_H - y;
    if (w <= 0 || h <= 0) return;
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) {
            MainMenuBackgroundBlendPixel(xx, yy, color, alpha);
        }
    }
}

static void MainMenuBackgroundBlendPoly4(int x0, int y0, int x1, int y1,
                                         int x2, int y2, int x3, int y3,
                                         uint32_t color_a, uint32_t color_b, int alpha) {
    int xs[4] = { x0, x1, x2, x3 };
    int ys[4] = { y0, y1, y2, y3 };
    int min_y = ys[0];
    int max_y = ys[0];
    for (int i = 1; i < 4; ++i) {
        if (ys[i] < min_y) min_y = ys[i];
        if (ys[i] > max_y) max_y = ys[i];
    }
    int unclamped_min_y = min_y;
    int span_y = max_y - min_y;
    if (span_y <= 0) span_y = 1;
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
            float color_t = (float)(y - unclamped_min_y) / (float)span_y;
            uint32_t color = MainMenuBlendColor(color_a, color_b, color_t);
            for (int x = draw_x0; x < draw_x1; ++x) {
                MainMenuBackgroundBlendPixel(x, y, color, alpha);
            }
        }
    }
}

static void MainMenuBackgroundDrawFlag(int x, int y, int scale, int alpha) {
    MainMenuBackgroundBlendPoly4(x, y + 4 * scale, x + 7 * scale, y + 6 * scale,
                                 x - 4 * scale, y + 54 * scale, x - 11 * scale, y + 52 * scale,
                                 0x006b2a31, 0x004b2025, alpha);
    MainMenuBackgroundBlendPoly4(x + 2 * scale, y, x + 34 * scale, y + 8 * scale,
                                 x + 22 * scale, y + 28 * scale, x - 2 * scale, y + 20 * scale,
                                 0x00963a43, 0x006a2d33, alpha);
}

static void MainMenuBackgroundDrawPlayer() {
    MainMenuBackgroundBlendPoly4(1165, 623, 1308, 579, 1350, 724, 1205, 768, 0x00fff6e8, 0x00e6ddcc, 255);
    MainMenuBackgroundBlendPoly4(1168, 623, 1308, 579, 1317, 609, 1178, 653, 0x00fff9ed, 0x00f3ead9, 145);
    MainMenuBackgroundBlendPoly4(1219, 662, 1240, 657, 1246, 694, 1225, 699, 0x00ba3c4a, 0x00a93643, 255);
    MainMenuBackgroundBlendPoly4(1262, 649, 1282, 644, 1288, 681, 1268, 686, 0x00ba3c4a, 0x00a93643, 255);
}

static void MainMenuGenerateProceduralBackground() {
    for (int y = 0; y < FB_H; ++y) {
        int vertical = y * 256 / FB_H;
        for (int x = 0; x < FB_W; ++x) {
            int horizontal = x * 256 / FB_W;
            int warm_top = MainMenuClampInt(190 - (IntAbs(x - 1040) + y) / 5, 0, 190);
            int warm_right = MainMenuClampInt((x - 980) / 10, 0, 65);
            int lower_warm = MainMenuClampInt((x + y - 1360) / 12, 0, 75);
            int vignette = MainMenuClampInt((IntAbs(x - 980) + IntAbs(y - 520) - 760) / 8, 0, 38);
            int grain = (int)(MainMenuHashPixel(x / 2, y / 2, 31u) & 15u) - 7;
            int r = 35 + horizontal / 18 + vertical / 36 + warm_top / 9 + warm_right / 5 + lower_warm / 5 - vignette / 2 + grain / 2;
            int g = 26 + horizontal / 55 + vertical / 58 + warm_top / 32 + lower_warm / 22 - vignette / 5 + grain / 5;
            int b = 27 + horizontal / 58 + vertical / 66 + warm_top / 36 + lower_warm / 24 - vignette / 5 + grain / 5;
            g_main_menu_background[y * FB_W + x] = MainMenuRgb(r, g, b);
        }
    }

    MainMenuBackgroundBlendPoly4(900, -10, 1264, -8, 1138, 324, 836, 220,
                                 0x00bd414d, 0x0093343c, 248);
    MainMenuBackgroundBlendPoly4(846, 224, 1184, 332, 1164, 428, 716, 292,
                                 0x002b2022, 0x0021191a, 122);
    MainMenuBackgroundBlendPoly4(716, 292, 1928, 560, 1928, 708, 616, 430,
                                 0x00211a1b, 0x001a1415, 174);

    MainMenuBackgroundBlendPoly4(1434, 174, 1930, 284, 1930, 392, 1418, 276,
                                 0x00893a40, 0x006f3034, 232);
    MainMenuBackgroundBlendPoly4(1708, 338, 1836, 370, 1662, 775, 1512, 728,
                                 0x00c23d48, 0x0097343d, 244);

    MainMenuBackgroundBlendPoly4(1352, 416, 1538, 468, 1504, 508, 1344, 452,
                                 0x0086363c, 0x00652c31, 224);
    MainMenuBackgroundBlendPoly4(1048, 480, 1272, 468, 1212, 536, 1056, 492,
                                 0x00863a40, 0x00602a2f, 224);
    MainMenuBackgroundBlendPoly4(932, 604, 1012, 632, 970, 720, 888, 692,
                                 0x006a3035, 0x00482327, 196);

    MainMenuBackgroundDrawFlag(1465, 384, 1, 130);
    MainMenuBackgroundDrawFlag(1176, 470, 1, 122);
    MainMenuBackgroundDrawFlag(930, 606, 1, 134);
    MainMenuBackgroundDrawPlayer();

    MainMenuBackgroundBlendPoly4(832, 840, 1930, 590, 1930, 1088, 456, 1088,
                                 0x00d3424e, 0x0088343c, 250);
    MainMenuBackgroundBlendPoly4(508, 964, 832, 840, 620, 1088, 456, 1088,
                                 0x0088343c, 0x00401f22, 150);
    MainMenuBackgroundBlendPoly4(830, 858, 1128, 776, 930, 1088, 440, 1088,
                                 0x00401f22, 0x0024181a, 70);

    for (int i = 0; i < 9; ++i) {
        int x = 826 + i * 39;
        int y = 846 - i * 23;
        uint32_t color = i & 1 ? 0x00d54954 : 0x00a93b43;
        MainMenuBackgroundBlendPoly4(x, y, x + 15, y - 5, x + 21, y + 11, x + 5, y + 17, color, color, 230);
    }

    for (int y = 0; y < FB_H; ++y) {
        for (int x = 0; x < FB_W; ++x) {
            uint32_t* dst = g_main_menu_background + y * FB_W + x;
            uint32_t color = *dst;
            int r = (int)((color >> 16) & 255);
            int g = (int)((color >> 8) & 255);
            int b = (int)(color & 255);
            int left_shadow = MainMenuClampInt((620 - x) / 18, 0, 30);
            int top_shadow = MainMenuClampInt((62 - y) / 5, 0, 10);
            int inv = 255 - left_shadow - top_shadow;
            if (inv < 200) inv = 200;
            r = r * inv / 255;
            g = g * inv / 255;
            b = b * inv / 255;
            int grain = (int)(MainMenuHashPixel(x, y, 91u) & 7u) - 3;
            *dst = MainMenuRgb(r + grain, g + grain / 2, b + grain / 2);
        }
    }

    MainMenuBackgroundBlendRect(0, FB_H - 3, FB_W, 3, 0x009d3540, 220);
}

enum MainMenuBackgroundMode {
    MAIN_MENU_BG_EMBEDDED,
    MAIN_MENU_BG_EXTERNAL,
    MAIN_MENU_BG_PROCEDURAL
};

static MainMenuBackgroundMode MainMenuBackgroundModeFromEnv() {
    char mode[32];
    DWORD len = GetEnvironmentVariableA("RECONFIG_MENU_BG_MODE", mode, (DWORD)sizeof(mode));
    if (len > 0 && len < sizeof(mode)) {
        if (mode[0] == 'B' || mode[0] == 'b' || mode[0] == 'I' || mode[0] == 'i' || mode[0] == 'F' || mode[0] == 'f') {
            return MAIN_MENU_BG_EXTERNAL;
        }
        if (mode[0] == 'P' || mode[0] == 'p' || mode[0] == 'V' || mode[0] == 'v') {
            return MAIN_MENU_BG_PROCEDURAL;
        }
    }
    return MAIN_MENU_BG_EMBEDDED;
}

static MainMenuUpscaleMode MainMenuUpscaleModeFromEnv() {
    char value[32];
    DWORD len = GetEnvironmentVariableA("RECONFIG_MENU_UPSCALE", value, (DWORD)sizeof(value));
    if (len > 0 && len < sizeof(value)) {
        if (value[0] == 'B' || value[0] == 'b') {
            if ((value[1] == 'I' || value[1] == 'i') && (value[2] == 'L' || value[2] == 'l')) {
                return MAIN_MENU_UPSCALE_BILINEAR;
            }
            return MAIN_MENU_UPSCALE_BICUBIC;
        }
        if (value[0] == 'L' || value[0] == 'l') {
            return MAIN_MENU_UPSCALE_LANCZOS;
        }
    }
    return MAIN_MENU_UPSCALE_BICUBIC;
}

static int MainMenuClampIndex(int value, int max_value) {
    if (value < 0) return 0;
    if (value > max_value) return max_value;
    return value;
}

static float MainMenuCubicWeight(float x) {
    x = FloatAbs(x);
    if (x <= 1.0f) {
        return (1.5f * x - 2.5f) * x * x + 1.0f;
    }
    if (x < 2.0f) {
        return ((-0.5f * x + 2.5f) * x - 4.0f) * x + 2.0f;
    }
    return 0.0f;
}

static float MainMenuLanczosWeight(float x) {
    x = FloatAbs(x);
    if (x < 0.0001f) {
        return 1.0f;
    }
    if (x >= 3.0f) {
        return 0.0f;
    }
    const float pi = 3.14159265f;
    float pix = pi * x;
    return (SinApprox(pix) / pix) * (SinApprox(pix / 3.0f) / (pix / 3.0f));
}

static uint32_t MainMenuSampleBilinear(const unsigned char* pixels, int row_bytes, int width, int height, float sx, float sy) {
    int x0 = FloorToInt(sx);
    int y0 = FloorToInt(sy);
    float fx = sx - (float)x0;
    float fy = sy - (float)y0;
    x0 = MainMenuClampIndex(x0, width - 1);
    y0 = MainMenuClampIndex(y0, height - 1);
    int x1 = MainMenuClampIndex(x0 + 1, width - 1);
    int y1 = MainMenuClampIndex(y0 + 1, height - 1);
    const unsigned char* p00 = pixels + y0 * row_bytes + x0 * 3;
    const unsigned char* p10 = pixels + y0 * row_bytes + x1 * 3;
    const unsigned char* p01 = pixels + y1 * row_bytes + x0 * 3;
    const unsigned char* p11 = pixels + y1 * row_bytes + x1 * 3;
    int out[3];
    for (int c = 0; c < 3; ++c) {
        float c0 = (float)p00[c] + ((float)p10[c] - (float)p00[c]) * fx;
        float c1 = (float)p01[c] + ((float)p11[c] - (float)p01[c]) * fx;
        out[c] = (int)(c0 + (c1 - c0) * fy + 0.5f);
    }
    return (uint32_t)((out[0] << 16) | (out[1] << 8) | out[2]);
}

static uint32_t MainMenuSampleFiltered(const unsigned char* pixels, int row_bytes, int width, int height, float sx, float sy, MainMenuUpscaleMode mode) {
    int base_x = FloorToInt(sx);
    int base_y = FloorToInt(sy);
    float sum_r = 0.0f;
    float sum_g = 0.0f;
    float sum_b = 0.0f;
    float sum_w = 0.0f;
    int radius = mode == MAIN_MENU_UPSCALE_LANCZOS ? 3 : 2;
    for (int yy = base_y - radius + 1; yy <= base_y + radius; ++yy) {
        float wy = mode == MAIN_MENU_UPSCALE_LANCZOS ? MainMenuLanczosWeight(sy - (float)yy) : MainMenuCubicWeight(sy - (float)yy);
        if (wy == 0.0f) continue;
        int cy = MainMenuClampIndex(yy, height - 1);
        for (int xx = base_x - radius + 1; xx <= base_x + radius; ++xx) {
            float wx = mode == MAIN_MENU_UPSCALE_LANCZOS ? MainMenuLanczosWeight(sx - (float)xx) : MainMenuCubicWeight(sx - (float)xx);
            float weight = wx * wy;
            if (weight == 0.0f) continue;
            int cx = MainMenuClampIndex(xx, width - 1);
            const unsigned char* p = pixels + cy * row_bytes + cx * 3;
            sum_r += (float)p[0] * weight;
            sum_g += (float)p[1] * weight;
            sum_b += (float)p[2] * weight;
            sum_w += weight;
        }
    }
    if (sum_w < 0.0001f && sum_w > -0.0001f) {
        return MainMenuSampleBilinear(pixels, row_bytes, width, height, sx, sy);
    }
    int r = (int)(sum_r / sum_w + 0.5f);
    int g = (int)(sum_g / sum_w + 0.5f);
    int b = (int)(sum_b / sum_w + 0.5f);
    r = MainMenuClampInt(r, 0, 255);
    g = MainMenuClampInt(g, 0, 255);
    b = MainMenuClampInt(b, 0, 255);
    return (uint32_t)((r << 16) | (g << 8) | b);
}

static uint32_t MainMenuSampleUpscaled(const unsigned char* pixels, int row_bytes, int width, int height, int x, int y, MainMenuUpscaleMode mode) {
    float sx = width <= 1 ? 0.0f : ((float)x + 0.5f) * (float)width / (float)FB_W - 0.5f;
    float sy = height <= 1 ? 0.0f : ((float)y + 0.5f) * (float)height / (float)FB_H - 0.5f;
    if (mode == MAIN_MENU_UPSCALE_BILINEAR) {
        return MainMenuSampleBilinear(pixels, row_bytes, width, height, sx, sy);
    }
    return MainMenuSampleFiltered(pixels, row_bytes, width, height, sx, sy, mode);
}

static int MainMenuLuma(uint32_t color) {
    int r = (int)((color >> 16) & 255);
    int g = (int)((color >> 8) & 255);
    int b = (int)(color & 255);
    return (r * 77 + g * 150 + b * 29) >> 8;
}

static int MainMenuSharpenEnabled() {
    char value[16];
    DWORD len = GetEnvironmentVariableA("RECONFIG_MENU_SHARPEN", value, (DWORD)sizeof(value));
    return !(len > 0 && len < sizeof(value) && value[0] == '0');
}

static void MainMenuSharpenUpscaledBackground() {
    if (!MainMenuSharpenEnabled()) {
        return;
    }

    uint32_t* source = (uint32_t*)HeapAlloc(GetProcessHeap(), 0, FB_W * FB_H * sizeof(uint32_t));
    if (!source) {
        return;
    }
    for (int i = 0; i < FB_W * FB_H; ++i) {
        source[i] = g_main_menu_background[i];
    }

    for (int y = 1; y < FB_H - 1; ++y) {
        uint32_t* dst = g_main_menu_background + y * FB_W;
        const uint32_t* src = source + y * FB_W;
        const uint32_t* up = source + (y - 1) * FB_W;
        const uint32_t* down = source + (y + 1) * FB_W;
        for (int x = 1; x < FB_W - 1; ++x) {
            uint32_t center = src[x];
            uint32_t left = src[x - 1];
            uint32_t right = src[x + 1];
            uint32_t top = up[x];
            uint32_t bottom = down[x];
            int avg_luma = (MainMenuLuma(left) + MainMenuLuma(right) + MainMenuLuma(top) + MainMenuLuma(bottom)) / 4;
            int edge = IntAbs(MainMenuLuma(center) - avg_luma);
            if (edge < 7) {
                continue;
            }

            int amount = 46 + MainMenuClampInt(edge, 0, 44);
            int cr = (int)((center >> 16) & 255);
            int cg = (int)((center >> 8) & 255);
            int cb = (int)(center & 255);
            int ar = (int)(((left >> 16) & 255) + ((right >> 16) & 255) + ((top >> 16) & 255) + ((bottom >> 16) & 255)) / 4;
            int ag = (int)(((left >> 8) & 255) + ((right >> 8) & 255) + ((top >> 8) & 255) + ((bottom >> 8) & 255)) / 4;
            int ab = (int)((left & 255) + (right & 255) + (top & 255) + (bottom & 255)) / 4;
            int dr = MainMenuClampInt((cr - ar) * amount / 256, -20, 20);
            int dg = MainMenuClampInt((cg - ag) * amount / 256, -20, 20);
            int db = MainMenuClampInt((cb - ab) * amount / 256, -20, 20);
            dst[x] = MainMenuRgb(cr + dr, cg + dg, cb + db);
        }
    }

    HeapFree(GetProcessHeap(), 0, source);
}

static int MainMenuDecodeCompressedBackgroundBytes(unsigned char* file_bytes, DWORD file_size) {
    const DWORD header_size = 4 + 4 * 4 + 4 + 256;
    if (file_size == INVALID_FILE_SIZE || file_size <= header_size) {
        HeapFree(GetProcessHeap(), 0, file_bytes);
        return 0;
    }
    if (file_bytes[0] != 'R' || file_bytes[1] != 'C' || file_bytes[2] != 'B' || file_bytes[3] != 'G') {
        HeapFree(GetProcessHeap(), 0, file_bytes);
        return 0;
    }

    uint32_t width = MainMenuReadU32(file_bytes + 4);
    uint32_t height = MainMenuReadU32(file_bytes + 8);
    uint32_t decoded_size = MainMenuReadU32(file_bytes + 12);
    uint32_t encoded_size = MainMenuReadU32(file_bytes + 16);
    unsigned char filter = file_bytes[20];
    if (width == 0 || height == 0 || width > FB_W || height > FB_H ||
        decoded_size != width * height * 3 ||
        encoded_size == 0 || header_size + encoded_size != file_size || filter != 3) {
        HeapFree(GetProcessHeap(), 0, file_bytes);
        return 0;
    }

    const unsigned char* lengths = file_bytes + 24;
    const unsigned char* encoded = file_bytes + header_size;
    int max_len = 0;
    int length_counts[25];
    for (int i = 0; i < 25; ++i) {
        length_counts[i] = 0;
    }
    for (int i = 0; i < 256; ++i) {
        int len = lengths[i];
        if (len > 24) {
            HeapFree(GetProcessHeap(), 0, file_bytes);
            return 0;
        }
        if (len > 0) {
            ++length_counts[len];
            if (len > max_len) {
                max_len = len;
            }
        }
    }
    if (max_len <= 0) {
        HeapFree(GetProcessHeap(), 0, file_bytes);
        return 0;
    }

    uint32_t next_code[25];
    uint32_t code = 0;
    next_code[0] = 0;
    for (int bits = 1; bits <= max_len; ++bits) {
        code = (code + (uint32_t)length_counts[bits - 1]) << 1;
        next_code[bits] = code;
    }

    const int max_nodes = 8192;
    int* child0 = g_main_menu_huff_child0;
    int* child1 = g_main_menu_huff_child1;
    int* symbol = g_main_menu_huff_symbol;
    for (int i = 0; i < max_nodes; ++i) {
        child0[i] = -1;
        child1[i] = -1;
        symbol[i] = -1;
    }
    int node_count = 1;
    for (int sym = 0; sym < 256; ++sym) {
        int len = lengths[sym];
        if (len <= 0) {
            continue;
        }
        uint32_t sym_code = next_code[len]++;
        int node = 0;
        for (int bit_index = len - 1; bit_index >= 0; --bit_index) {
            int bit = (int)((sym_code >> bit_index) & 1);
            int* child = bit ? &child1[node] : &child0[node];
            if (*child < 0) {
                if (node_count >= max_nodes) {
                    HeapFree(GetProcessHeap(), 0, file_bytes);
                    return 0;
                }
                *child = node_count++;
            }
            node = *child;
        }
        symbol[node] = sym;
    }

    unsigned char* decoded = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, decoded_size);
    const int source_row_bytes = (int)width * 3;
    unsigned char* row = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, source_row_bytes);
    unsigned char* prev_row = (unsigned char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, source_row_bytes);
    if (!decoded || !row || !prev_row) {
        if (decoded) HeapFree(GetProcessHeap(), 0, decoded);
        if (row) HeapFree(GetProcessHeap(), 0, row);
        if (prev_row) HeapFree(GetProcessHeap(), 0, prev_row);
        HeapFree(GetProcessHeap(), 0, file_bytes);
        return 0;
    }

    uint32_t out_pos = 0;
    uint32_t bit_pos = 0;
    while (out_pos < decoded_size) {
        int node = 0;
        while (symbol[node] < 0) {
            if (bit_pos >= encoded_size * 8) {
                HeapFree(GetProcessHeap(), 0, prev_row);
                HeapFree(GetProcessHeap(), 0, row);
                HeapFree(GetProcessHeap(), 0, decoded);
                HeapFree(GetProcessHeap(), 0, file_bytes);
                return 0;
            }
            int bit = (encoded[bit_pos >> 3] >> (7 - (bit_pos & 7))) & 1;
            node = bit ? child1[node] : child0[node];
            if (node < 0) {
                HeapFree(GetProcessHeap(), 0, prev_row);
                HeapFree(GetProcessHeap(), 0, row);
                HeapFree(GetProcessHeap(), 0, decoded);
                HeapFree(GetProcessHeap(), 0, file_bytes);
                return 0;
            }
            ++bit_pos;
        }
        decoded[out_pos++] = (unsigned char)symbol[node];
    }

    for (uint32_t y = 0; y < height; ++y) {
        unsigned char* src = decoded + y * source_row_bytes;
        for (int i = 0; i < source_row_bytes; ++i) {
            int left = i >= 3 ? row[i - 3] : 0;
            int up = prev_row[i];
            row[i] = (unsigned char)((src[i] + ((left + up) / 2)) & 255);
            src[i] = row[i];
        }

        unsigned char* tmp = prev_row;
        prev_row = row;
        row = tmp;
    }

    if (width == FB_W && height == FB_H) {
        for (int y = 0; y < FB_H; ++y) {
            const unsigned char* src = decoded + y * source_row_bytes;
            uint32_t* dst = g_main_menu_background + y * FB_W;
            for (int x = 0; x < FB_W; ++x) {
                const unsigned char* px = src + x * 3;
                dst[x] = ((uint32_t)px[0] << 16) | ((uint32_t)px[1] << 8) | (uint32_t)px[2];
            }
        }
    } else {
        MainMenuUpscaleMode upscale_mode = MainMenuUpscaleModeFromEnv();
        for (int y = 0; y < FB_H; ++y) {
            uint32_t* dst = g_main_menu_background + y * FB_W;
            for (int x = 0; x < FB_W; ++x) {
                dst[x] = MainMenuSampleUpscaled(decoded, source_row_bytes, (int)width, (int)height, x, y, upscale_mode);
            }
        }
        MainMenuSharpenUpscaledBackground();
    }

    HeapFree(GetProcessHeap(), 0, prev_row);
    HeapFree(GetProcessHeap(), 0, row);
    HeapFree(GetProcessHeap(), 0, decoded);
    HeapFree(GetProcessHeap(), 0, file_bytes);
    return 1;
}


static int MainMenuLoadCompressedBackground(const char* path) {
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }

    DWORD file_size = GetFileSize(file, 0);
    const DWORD header_size = 4 + 4 * 4 + 4 + 256;
    if (file_size == INVALID_FILE_SIZE || file_size <= header_size) {
        CloseHandle(file);
        return 0;
    }

    unsigned char* file_bytes = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, file_size);
    if (!file_bytes) {
        CloseHandle(file);
        return 0;
    }

    DWORD read = 0;
    int ok = ReadFile(file, file_bytes, file_size, &read, 0) && read == file_size;
    CloseHandle(file);
    if (!ok) {
        HeapFree(GetProcessHeap(), 0, file_bytes);
        return 0;
    }

    return MainMenuDecodeCompressedBackgroundBytes(file_bytes, file_size);
}


static int MainMenuLoadEmbeddedCompressedBackground() {
    DWORD file_size = (DWORD)sizeof(kMainMenuEmbeddedBackgroundBgc);
    unsigned char* file_bytes = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, file_size);
    if (!file_bytes) {
        return 0;
    }
    for (DWORD i = 0; i < file_size; ++i) {
        file_bytes[i] = kMainMenuEmbeddedBackgroundBgc[i];
    }
    return MainMenuDecodeCompressedBackgroundBytes(file_bytes, file_size);
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

    MainMenuBackgroundMode background_mode = MainMenuBackgroundModeFromEnv();
    if (background_mode == MAIN_MENU_BG_EXTERNAL) {
        char path[MAX_PATH];
        if (MainMenuFindCompressedBackgroundPath(path, (int)sizeof(path)) && MainMenuLoadCompressedBackground(path)) {
            g_main_menu_background_loaded = 1;
            return;
        }
        if (MainMenuFindBackgroundPath(path, (int)sizeof(path)) && MainMenuLoadBmp(path)) {
            g_main_menu_background_loaded = 1;
            return;
        }
    } else if (background_mode == MAIN_MENU_BG_PROCEDURAL) {
        MainMenuGenerateProceduralBackground();
        g_main_menu_background_loaded = 1;
        return;
    }

    if (MainMenuLoadEmbeddedCompressedBackground()) {
        g_main_menu_background_loaded = 1;
        return;
    }

    MainMenuGenerateProceduralBackground();
    g_main_menu_background_loaded = 1;
}

void MainMenuUpdate(MainMenuState* menu) {
    menu->action = MAIN_MENU_ACTION_NONE;
    if (InputWasPressed(KEY_UP) || InputWasPressed(KEY_DOWN)) {
        menu->selection_from_index = menu->selected_index;
        menu->selected_index = menu->selected_index == 0 ? 1 : 0;
        menu->selection_changed_at = PerfNowSeconds();
    }

    if (InputWasPressed(KEY_Z)) {
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

static void MainMenuDrawTitle(RenderContext* render, const MainMenuColors* colors, double time) {
    const int title_y = 430;
    const int re_x = 164;
    const int config_x = 366;
    const uint32_t shadow = 0x00100b0b;
    uint32_t title_red = MainMenuBrighten(colors->title_red, MainMenuPulse(time, 0.16, 0, 9));
    uint32_t title_text = MainMenuBrighten(colors->title_text, MainMenuPulse(time + 2.0, 0.13, 0, 5));
    MainMenuDrawLogoText(render, re_x + 3, title_y + 4, "RE:", shadow);
    MainMenuDrawLogoText(render, config_x + 3, title_y + 4, "CONFIG", shadow);
    MainMenuDrawLogoText(render, re_x, title_y, "RE:", title_red);
    MainMenuDrawLogoText(render, config_x, title_y, "CONFIG", title_text);
}

static void MainMenuDrawAmbientMotion(RenderContext* render, const MainMenuColors* colors, double time) {
    uint32_t cyan = MainMenuBrighten(colors->selected, MainMenuPulse(time + 0.4, 0.46, 8, 28));
    uint32_t red = MainMenuBrighten(colors->title_red, MainMenuPulse(time + 1.1, 0.38, 6, 24));

    for (int i = 0; i < 24; ++i) {
        unsigned int h = MainMenuHashPixel(i * 23 + 5, i * 31 + 9, 419u);
        double phase = (double)((h >> 8) & 255u) / 255.0;
        double drift = MainMenuFrac(time * (0.120 + (double)(h & 7u) * 0.010) + phase);
        int x = (int)(drift * (double)(FB_W + 360)) - 180;
        int y = 82 + (int)(((h >> 4) & 1023u) * 880u / 1024u);
        y += (int)(MainMenuTri(time * 0.18 + phase) * 10.0) - 5;
        int w = 22 + (int)((h >> 20) & 31u);
        int alpha = 38 + (int)((h >> 12) & 35u);
        uint32_t color = (i & 1) ? cyan : red;
        MainMenuBlendRectRaw(render, x - 2, y, w + 4, 2, color, alpha / 3);
        MainMenuBlendRectRaw(render, x, y, w, 2, color, alpha);
    }
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
    uint32_t bright = MainMenuBrighten(color, MainMenuPulse(time, 0.82, 0, 12));
    int y = center_y - 17;
    MainMenuBlendRectRaw(render, x - 2, y - 2, 7, 38, bright, 18);
    DrawRect(render, x, y, 4, 34, bright);
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
    MainMenuDrawAmbientMotion(render, colors, time);
    MainMenuDrawTitle(render, colors, time);

    const int bar_x = 186;
    const int text_x = 242;
    const int start_center_y = 604;
    const int exit_center_y = 696;
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
