#include "framebuffer.h"

static HWND g_framebuffer_window;
static uint32_t g_pixels[FB_W * FB_H];
static uint32_t g_static_pixels[FB_W * FB_H];
static uint32_t g_supersample_pixels[RENDER_W * RENDER_H];
static uint32_t g_static_supersample_pixels[RENDER_W * RENDER_H];
static BITMAPINFO g_bitmap;
static HDC g_present_dc;
static HBITMAP g_present_bitmap;
static HBITMAP g_present_old_bitmap;
static int g_present_w;
static int g_present_h;

static void FramebufferClearBytes(void* dest, size_t count) {
    unsigned char* out = (unsigned char*)dest;
    for (size_t i = 0; i < count; ++i) {
        out[i] = 0;
    }
}

void FramebufferInit(HWND window, RenderContext* render) {
    g_framebuffer_window = window;
    render->pixels = g_supersample_pixels;
    render->width = RENDER_W;
    render->height = RENDER_H;
    render->scale = RENDER_SCALE;

    FramebufferClearBytes(&g_bitmap, sizeof(g_bitmap));
    g_bitmap.bmiHeader.biSize = sizeof(g_bitmap.bmiHeader);
    g_bitmap.bmiHeader.biWidth = FB_W;
    g_bitmap.bmiHeader.biHeight = -FB_H;
    g_bitmap.bmiHeader.biPlanes = 1;
    g_bitmap.bmiHeader.biBitCount = 32;
    g_bitmap.bmiHeader.biCompression = BI_RGB;
}

uint32_t* FramebufferPixels() {
    return g_pixels;
}

uint32_t* FramebufferRenderPixels() {
    return g_supersample_pixels;
}

uint32_t* FramebufferStaticRenderPixels() {
    return g_static_supersample_pixels;
}

RectI FramebufferClampRect(RectI rect) {
    if (rect.x < 0) {
        rect.w += rect.x;
        rect.x = 0;
    }
    if (rect.y < 0) {
        rect.h += rect.y;
        rect.y = 0;
    }
    if (rect.x + rect.w > FB_W) {
        rect.w = FB_W - rect.x;
    }
    if (rect.y + rect.h > FB_H) {
        rect.h = FB_H - rect.y;
    }
    if (rect.w < 0) rect.w = 0;
    if (rect.h < 0) rect.h = 0;
    return rect;
}

void FramebufferCopyPixels(uint32_t* dst, const uint32_t* src, int count) {
    for (int i = 0; i < count; ++i) {
        dst[i] = src[i];
    }
}

void FramebufferCopyStaticToLive() {
    FramebufferCopyPixels(g_supersample_pixels, g_static_supersample_pixels, RENDER_W * RENDER_H);
    FramebufferCopyPixels(g_pixels, g_static_pixels, FB_W * FB_H);
}

void FramebufferDownsampleRectFromTo(const uint32_t* src_pixels, uint32_t* dst_pixels, RectI rect) {
    rect = FramebufferClampRect(rect);
    for (int y = rect.y; y < rect.y + rect.h; ++y) {
        uint32_t* dst = dst_pixels + y * FB_W + rect.x;
        for (int x = rect.x; x < rect.x + rect.w; ++x) {
            int r = 0;
            int g = 0;
            int bl = 0;
            for (int yy = 0; yy < RENDER_SCALE; ++yy) {
                const uint32_t* src = src_pixels + (y * RENDER_SCALE + yy) * RENDER_W + x * RENDER_SCALE;
                for (int xx = 0; xx < RENDER_SCALE; ++xx) {
                    uint32_t color = src[xx];
                    r += (int)((color >> 16) & 255);
                    g += (int)((color >> 8) & 255);
                    bl += (int)(color & 255);
                }
            }
            int count = RENDER_SCALE * RENDER_SCALE;
            *dst++ = (uint32_t)(((r / count) << 16) | ((g / count) << 8) | (bl / count));
        }
    }
}

void FramebufferDownsampleRenderTarget() {
    RectI full = { 0, 0, FB_W, FB_H };
    FramebufferDownsampleRectFromTo(g_supersample_pixels, g_pixels, full);
}

void FramebufferRestoreStaticRect(RectI rect) {
    rect = FramebufferClampRect(rect);
    int sx = rect.x * RENDER_SCALE;
    int sy = rect.y * RENDER_SCALE;
    int sw = rect.w * RENDER_SCALE;
    int sh = rect.h * RENDER_SCALE;
    for (int y = 0; y < sh; ++y) {
        uint32_t* dst = g_supersample_pixels + (sy + y) * RENDER_W + sx;
        const uint32_t* src = g_static_supersample_pixels + (sy + y) * RENDER_W + sx;
        FramebufferCopyPixels(dst, src, sw);
    }
}

void FramebufferBuildStaticCache(RenderContext* render, uint32_t bg_color, FramebufferDrawCallback draw_static, int copy_to_live) {
    uint32_t* old_pixels = render->pixels;
    render->pixels = g_static_supersample_pixels;
    RenderClear(render, bg_color);
    draw_static();
    render->pixels = old_pixels;

    RectI full = { 0, 0, FB_W, FB_H };
    FramebufferDownsampleRectFromTo(g_static_supersample_pixels, g_static_pixels, full);
    if (copy_to_live) {
        FramebufferCopyStaticToLive();
    }
}

void FramebufferPresent(uint32_t bg_color) {
    (void)bg_color;
    HDC dc = GetDC(g_framebuffer_window);
    RECT client;
    GetClientRect(g_framebuffer_window, &client);
    int cw = client.right - client.left;
    int ch = client.bottom - client.top;
    if (cw <= 0 || ch <= 0) {
        ReleaseDC(g_framebuffer_window, dc);
        return;
    }

    if (!g_present_dc || g_present_w != cw || g_present_h != ch) {
        if (g_present_dc) {
            SelectObject(g_present_dc, g_present_old_bitmap);
            DeleteObject(g_present_bitmap);
            DeleteDC(g_present_dc);
            g_present_dc = 0;
            g_present_bitmap = 0;
            g_present_old_bitmap = 0;
        }

        BITMAPINFO backbuffer_info;
        FramebufferClearBytes(&backbuffer_info, sizeof(backbuffer_info));
        backbuffer_info.bmiHeader.biSize = sizeof(backbuffer_info.bmiHeader);
        backbuffer_info.bmiHeader.biWidth = cw;
        backbuffer_info.bmiHeader.biHeight = -ch;
        backbuffer_info.bmiHeader.biPlanes = 1;
        backbuffer_info.bmiHeader.biBitCount = 32;
        backbuffer_info.bmiHeader.biCompression = BI_RGB;

        void* bits = 0;
        g_present_dc = CreateCompatibleDC(dc);
        g_present_bitmap = CreateDIBSection(dc, &backbuffer_info, DIB_RGB_COLORS, &bits, 0, 0);
        if (g_present_dc && g_present_bitmap) {
            g_present_old_bitmap = (HBITMAP)SelectObject(g_present_dc, g_present_bitmap);
            g_present_w = cw;
            g_present_h = ch;
        } else {
            if (g_present_bitmap) DeleteObject(g_present_bitmap);
            if (g_present_dc) DeleteDC(g_present_dc);
            g_present_dc = 0;
            g_present_bitmap = 0;
            g_present_old_bitmap = 0;
        }
    }

    HDC target = g_present_dc ? g_present_dc : dc;
    FillRect(target, &client, (HBRUSH)GetStockObject(BLACK_BRUSH));
    int w = cw;
    int h = (cw * 9) / 16;
    if (h > ch) {
        h = ch;
        w = (ch * 16) / 9;
    }
    int x = (cw - w) / 2;
    int y = (ch - h) / 2;
    SetStretchBltMode(target, COLORONCOLOR);
    StretchDIBits(target, x, y, w, h, 0, 0, FB_W, FB_H, g_pixels, &g_bitmap, DIB_RGB_COLORS, SRCCOPY);
    if (g_present_dc) {
        BitBlt(dc, 0, 0, cw, ch, g_present_dc, 0, 0, SRCCOPY);
    }
    ReleaseDC(g_framebuffer_window, dc);
}
