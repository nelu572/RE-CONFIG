#include "modal_ui.h"

#include "game_config.h"

static float ModalClamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

uint32_t ModalUiFadeColor(uint32_t color, float fade) {
    fade = ModalClamp01(fade);
    int r = (int)((float)((color >> 16) & 255) * fade);
    int g = (int)((float)((color >> 8) & 255) * fade);
    int b = (int)((float)(color & 255) * fade);
    return (uint32_t)((r << 16) | (g << 8) | b);
}

uint32_t ModalUiLerpColor(uint32_t a, uint32_t b, float t) {
    t = ModalClamp01(t);
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

void DrawModalBlendRect(RenderContext* render, int x, int y, int w, int h, uint32_t color, float alpha) {
    if (!render) {
        return;
    }
    int a = (int)(ModalClamp01(alpha) * 256.0f + 0.5f);
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

void DrawModalDimOverlay(RenderContext* render, float alpha_scale) {
    DrawModalBlendRect(render, 0, 0, FB_W, FB_H, MODAL_UI_DIM_COLOR, MODAL_UI_DIM_ALPHA * alpha_scale);
}

void DrawModalDimOverlayRect(RenderContext* render, RectI rect, float alpha_scale) {
    DrawModalBlendRect(render, rect.x, rect.y, rect.w, rect.h, MODAL_UI_DIM_COLOR, MODAL_UI_DIM_ALPHA * alpha_scale);
}

void DrawUIPanel(RenderContext* render, RectI rect, float alpha_scale) {
    DrawModalBlendRect(render, rect.x, rect.y, rect.w, rect.h, MODAL_UI_PANEL_FILL, 0.84f * alpha_scale);
    DrawRectOutline(render, rect.x, rect.y, rect.w, rect.h, ModalUiFadeColor(MODAL_UI_PANEL_BORDER, 0.72f * alpha_scale));
}