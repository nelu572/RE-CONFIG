#include "ui_text.h"

#include "delete_rules.h"
#include "game_config.h"

#ifndef RENDER_SCALE
#define RENDER_SCALE 2
#endif

#define TEXT_SURFACE_W (760 * RENDER_SCALE)
#define TEXT_SURFACE_H (76 * RENDER_SCALE)
#define TEXT_CACHE_COUNT 32

struct TextCacheEntry {
    int valid;
    int size;
    int bold;
    int font_mode;
    int text_quality;
    int copy_w;
    int copy_h;
    unsigned int last_used;
    wchar_t text[64];
    unsigned char alpha[TEXT_SURFACE_W * TEXT_SURFACE_H];
};

static HWND g_ui_text_window;
static RenderContext* g_ui_text_render;
static int g_ui_text_font_mode;
static int g_ui_text_quality;
static UiTextPerfCallback g_ui_text_perf_callback;
static HDC g_text_dc;
static HBITMAP g_text_bitmap;
static HBITMAP g_text_old_bitmap;
static uint32_t* g_text_bits;
static HFONT g_ui_fonts[96];
static TextCacheEntry g_text_cache[TEXT_CACHE_COUNT];
static unsigned int g_text_cache_tick;

static void ClearTextBytes(void* dest, size_t count) {
    unsigned char* out = (unsigned char*)dest;
    for (size_t i = 0; i < count; ++i) {
        out[i] = 0;
    }
}

void UiTextInit(HWND window, RenderContext* render, int font_mode, int text_quality, UiTextPerfCallback perf_callback) {
    g_ui_text_window = window;
    g_ui_text_render = render;
    g_ui_text_font_mode = font_mode;
    g_ui_text_quality = text_quality;
    g_ui_text_perf_callback = perf_callback;
}

void UiTextEnsureSurface() {
    if (g_text_dc) {
        return;
    }
    BITMAPINFO info;
    ClearTextBytes(&info, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = TEXT_SURFACE_W;
    info.bmiHeader.biHeight = -TEXT_SURFACE_H;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC dc = GetDC(g_ui_text_window);
    g_text_dc = CreateCompatibleDC(dc);
    g_text_bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, (void**)&g_text_bits, 0, 0);
    if (g_text_dc && g_text_bitmap) {
        g_text_old_bitmap = (HBITMAP)SelectObject(g_text_dc, g_text_bitmap);
    }
    ReleaseDC(g_ui_text_window, dc);
}

static HFONT UiFont(int size, int bold) {
    int index = size * 2 + (bold ? 1 : 0);
    if (index < 0 || index >= (int)(sizeof(g_ui_fonts) / sizeof(g_ui_fonts[0]))) {
        index = 0;
    }
    if (!g_ui_fonts[index]) {
        int weight = bold ? FW_MEDIUM : FW_NORMAL;
        const wchar_t* face = bold ? L"Noto Sans KR Medium" : L"Noto Sans KR DemiLight";
        if (g_ui_text_font_mode == 1) {
            face = L"Malgun Gothic";
        } else if (g_ui_text_font_mode == 2) {
            face = L"Segoe UI Variable Text";
        } else if (g_ui_text_font_mode == 3) {
            face = L"Segoe UI";
        } else if (g_ui_text_font_mode == 4) {
            face = L"Gulim";
        } else if (g_ui_text_font_mode == 0) {
            face = bold ? L"Malgun Gothic" : L"Malgun Gothic Semilight";
        } else if (g_ui_text_font_mode == 6) {
            face = L"Noto Sans KR Light";
        } else if (g_ui_text_font_mode == 7) {
            face = L"NanumGothic";
        }
        DWORD quality = ANTIALIASED_QUALITY;
        if (g_ui_text_quality == 1) {
            quality = CLEARTYPE_QUALITY;
        } else if (g_ui_text_quality == 2) {
            quality = CLEARTYPE_NATURAL_QUALITY;
        }
        g_ui_fonts[index] = CreateFontW(
            -size * RENDER_SCALE, 0, 0, 0, weight,
            FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, quality,
            DEFAULT_PITCH | FF_DONTCARE, face);
    }
    return g_ui_fonts[index];
}

void UiTextWarmSettingsFonts() {
    UiFont(18, 0);
    UiFont(20, 0);
    UiFont(21, 0);
    UiFont(23, 0);
    UiFont(24, 0);
    UiFont(27, 0);
    UiFont(32, 0);
}

static int TextCacheTextEquals(const wchar_t* a, const wchar_t* b) {
    while (*a && *b) {
        if (*a != *b) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == *b;
}

static void TextCacheCopy(wchar_t* dst, const wchar_t* src) {
    int i = 0;
    for (; i < 63 && src[i]; ++i) {
        dst[i] = src[i];
    }
    dst[i] = 0;
}

static TextCacheEntry* FindTextCacheEntry(const wchar_t* text, int size, int bold, int* cache_miss) {
    *cache_miss = 0;
    for (int i = 0; i < TEXT_CACHE_COUNT; ++i) {
        TextCacheEntry* entry = &g_text_cache[i];
        if (entry->valid &&
            entry->size == size &&
            entry->bold == bold &&
            entry->font_mode == g_ui_text_font_mode &&
            entry->text_quality == g_ui_text_quality &&
            TextCacheTextEquals(entry->text, text)) {
            entry->last_used = ++g_text_cache_tick;
            return entry;
        }
    }

    *cache_miss = 1;
    TextCacheEntry* entry = 0;
    TextCacheEntry* least_recent = &g_text_cache[0];
    for (int i = 0; i < TEXT_CACHE_COUNT; ++i) {
        if (!g_text_cache[i].valid) {
            entry = &g_text_cache[i];
            break;
        }
        if (g_text_cache[i].last_used < least_recent->last_used) {
            least_recent = &g_text_cache[i];
        }
    }
    if (!entry) {
        entry = least_recent;
    }

    for (int i = 0; i < TEXT_SURFACE_W * TEXT_SURFACE_H; ++i) {
        g_text_bits[i] = 0;
        entry->alpha[i] = 0;
    }

    HFONT font = UiFont(size, bold);
    HFONT old_font = (HFONT)SelectObject(g_text_dc, font);
    SetBkMode(g_text_dc, TRANSPARENT);
    SetTextColor(g_text_dc, RGB(255, 255, 255));
    RECT rect = { 0, 0, TEXT_SURFACE_W, TEXT_SURFACE_H };
    DrawTextW(g_text_dc, text, -1, &rect, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOCLIP);

    SIZE extent = { 0, 0 };
    GetTextExtentPoint32W(g_text_dc, text, lstrlenW(text), &extent);
    if (old_font) {
        SelectObject(g_text_dc, old_font);
    }

    entry->copy_w = extent.cx + 4 * RENDER_SCALE;
    entry->copy_h = extent.cy + 4 * RENDER_SCALE;
    if (entry->copy_w > TEXT_SURFACE_W) entry->copy_w = TEXT_SURFACE_W;
    if (entry->copy_h > TEXT_SURFACE_H) entry->copy_h = TEXT_SURFACE_H;
    for (int yy = 0; yy < entry->copy_h; ++yy) {
        for (int xx = 0; xx < entry->copy_w; ++xx) {
            uint32_t src = g_text_bits[yy * TEXT_SURFACE_W + xx];
            int b = (int)(src & 255);
            int g = (int)((src >> 8) & 255);
            int r = (int)((src >> 16) & 255);
            int a = r;
            if (g > a) a = g;
            if (b > a) a = b;
            entry->alpha[yy * TEXT_SURFACE_W + xx] = (unsigned char)a;
        }
    }
    entry->size = size;
    entry->bold = bold;
    entry->font_mode = g_ui_text_font_mode;
    entry->text_quality = g_ui_text_quality;
    TextCacheCopy(entry->text, text);
    entry->last_used = ++g_text_cache_tick;
    entry->valid = 1;
    return entry;
}

void DrawTextUi(int x, int y, const wchar_t* text, int size, uint32_t color, int bold) {
    UiTextEnsureSurface();
    if (!g_text_dc || !g_text_bits || !g_ui_text_render) {
        return;
    }

    int cache_miss = 0;
    TextCacheEntry* entry = FindTextCacheEntry(text, size, bold, &cache_miss);
    if (g_ui_text_perf_callback) {
        g_ui_text_perf_callback(cache_miss);
    }

    int dr = (int)((color >> 16) & 255);
    int dg = (int)((color >> 8) & 255);
    int db = (int)(color & 255);
    int dst_x = x * RENDER_SCALE;
    int dst_y = y * RENDER_SCALE;
    for (int yy = 0; yy < entry->copy_h; ++yy) {
        int ry = dst_y + yy;
        if ((unsigned)ry >= (unsigned)g_ui_text_render->height) {
            continue;
        }
        for (int xx = 0; xx < entry->copy_w; ++xx) {
            int rx = dst_x + xx;
            if ((unsigned)rx >= (unsigned)g_ui_text_render->width) {
                continue;
            }
            int a = (int)entry->alpha[yy * TEXT_SURFACE_W + xx];
            if (a <= 0) {
                continue;
            }
            int inv = 255 - a;
            uint32_t* dst = g_ui_text_render->pixels + ry * g_ui_text_render->width + rx;
            uint32_t old = *dst;
            int orr = (int)((old >> 16) & 255);
            int og = (int)((old >> 8) & 255);
            int ob = (int)(old & 255);
            int nr = (dr * a + orr * inv) / 255;
            int ng = (dg * a + og * inv) / 255;
            int nb = (db * a + ob * inv) / 255;
            *dst = (uint32_t)((nr << 16) | (ng << 8) | nb);
        }
    }
}

static void WarmTextCacheEntry(const wchar_t* text, int size, int bold) {
    int cache_miss = 0;
    UiTextEnsureSurface();
    if (g_text_dc && g_text_bits) {
        FindTextCacheEntry(text, size, bold, &cache_miss);
    }
}

void UiTextWarmSettingsTextCache() {
    WarmTextCacheEntry(L"설정", 32, 0);
    WarmTextCacheEntry(L"X", 26, 0);
    WarmTextCacheEntry(L"Z", 26, 0);
    WarmTextCacheEntry(L"ON", 21, 0);
    WarmTextCacheEntry(L"ON", 21, 1);
    WarmTextCacheEntry(L"OFF", 21, 0);
    WarmTextCacheEntry(L"OFF", 21, 1);
    WarmTextCacheEntry(L"기획 중", 21, 0);
    for (int i = 0; i < SETTINGS_CATEGORY_COUNT; ++i) {
        const SettingsCategoryDef* category = SettingsCategoryAt((SettingsCategory)i);
        WarmTextCacheEntry(category->name, 23, 0);
        WarmTextCacheEntry(category->name, 23, 1);
        WarmTextCacheEntry(category->name, 27, 0);
    }
    for (int i = 0; i < SettingsItemTotalCount(); ++i) {
        const SettingsItemDef* item = SettingsItemAt(i);
        WarmTextCacheEntry(item->name, 23, 0);
        if (item->values) {
            for (int v = 0; v < item->value_count; ++v) {
                WarmTextCacheEntry(item->values[v], 20, 0);
                WarmTextCacheEntry(item->values[v], 20, 1);
                WarmTextCacheEntry(item->values[v], 21, 0);
                WarmTextCacheEntry(item->values[v], 21, 1);
            }
        }
    }
}
