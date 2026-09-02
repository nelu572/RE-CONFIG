#include "render.h"

#include "game_config.h"
#include "math_util.h"

void RenderClear(RenderContext* render, uint32_t color) {
    for (int i = 0; i < render->width * render->height; ++i) {
        render->pixels[i] = color;
    }
}

static void RawDrawPixel(RenderContext* render, int x, int y, uint32_t color) {
    if ((unsigned)x >= (unsigned)render->width || (unsigned)y >= (unsigned)render->height) {
        return;
    }
    render->pixels[y * render->width + x] = color;
}

static void RawDrawRect(RenderContext* render, int x, int y, int w, int h, uint32_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > render->width) w = render->width - x;
    if (y + h > render->height) h = render->height - y;
    if (w <= 0 || h <= 0) return;

    for (int yy = y; yy < y + h; ++yy) {
        uint32_t* row = render->pixels + yy * render->width;
        for (int xx = x; xx < x + w; ++xx) {
            row[xx] = color;
        }
    }
}

void DrawPixel(RenderContext* render, int x, int y, uint32_t color) {
    int s = render->scale;
    RawDrawRect(render, x * s, y * s, s, s, color);
}

void DrawRect(RenderContext* render, int x, int y, int w, int h, uint32_t color) {
    int s = render->scale;
    RawDrawRect(render, x * s, y * s, w * s, h * s, color);
}

void DrawRectOutline(RenderContext* render, int x, int y, int w, int h, uint32_t color) {
    DrawRect(render, x, y, w, 1, color);
    DrawRect(render, x, y + h - 1, w, 1, color);
    DrawRect(render, x, y, 1, h, color);
    DrawRect(render, x + w - 1, y, 1, h, color);
}

static void RawBlendPixel(RenderContext* render, int x, int y, float r, float g, float b, float alpha) {
    if ((unsigned)x >= (unsigned)render->width || (unsigned)y >= (unsigned)render->height) return;
    alpha = Clamp01(alpha);
    int a = (int)(alpha * 256.0f + 0.5f);
    if (a <= 0) return;
    if (a > 256) a = 256;
    int inv = 256 - a;
    uint32_t dst = render->pixels[y * render->width + x];
    int sr = (int)(Clamp01(r) * 255.0f + 0.5f);
    int sg = (int)(Clamp01(g) * 255.0f + 0.5f);
    int sb = (int)(Clamp01(b) * 255.0f + 0.5f);
    int dr = (int)((dst >> 16) & 255);
    int dg = (int)((dst >> 8) & 255);
    int db = (int)(dst & 255);
    int out_r = (sr * a + dr * inv) >> 8;
    int out_g = (sg * a + dg * inv) >> 8;
    int out_b = (sb * a + db * inv) >> 8;
    render->pixels[y * render->width + x] = (uint32_t)((out_r << 16) | (out_g << 8) | out_b);
}

void BlendPixel(RenderContext* render, int x, int y, float r, float g, float b, float alpha) {
    int s = render->scale;
    for (int yy = 0; yy < s; ++yy) {
        for (int xx = 0; xx < s; ++xx) {
            RawBlendPixel(render, x * s + xx, y * s + yy, r, g, b, alpha);
        }
    }
}

void DrawRectBlend(RenderContext* render, int x, int y, int w, int h, uint32_t color, float alpha) {
    alpha = Clamp01(alpha);
    if (alpha <= 0.0f || w <= 0 || h <= 0) {
        return;
    }
    if (alpha >= 1.0f) {
        DrawRect(render, x, y, w, h, color);
        return;
    }

    int scale = render->scale;
    int raw_x = x * scale;
    int raw_y = y * scale;
    int raw_w = w * scale;
    int raw_h = h * scale;
    if (raw_x < 0) { raw_w += raw_x; raw_x = 0; }
    if (raw_y < 0) { raw_h += raw_y; raw_y = 0; }
    if (raw_x + raw_w > render->width) raw_w = render->width - raw_x;
    if (raw_y + raw_h > render->height) raw_h = render->height - raw_y;
    if (raw_w <= 0 || raw_h <= 0) {
        return;
    }

    float r = (float)((color >> 16) & 255) / 255.0f;
    float g = (float)((color >> 8) & 255) / 255.0f;
    float b = (float)(color & 255) / 255.0f;
    for (int yy = raw_y; yy < raw_y + raw_h; ++yy) {
        for (int xx = raw_x; xx < raw_x + raw_w; ++xx) {
            RawBlendPixel(render, xx, yy, r, g, b, alpha);
        }
    }
}

static float TriangleEdge(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static float ApproxLength(float x, float y) {
    float ax = FloatAbs(x);
    float ay = FloatAbs(y);
    float hi = ax > ay ? ax : ay;
    float lo = ax > ay ? ay : ax;
    return hi + lo * 0.375f;
}

void DrawAlphaTriangle(RenderContext* render, const TrailVertex* a, const TrailVertex* b, const TrailVertex* c) {
    float scale = (float)render->scale;
    TrailVertex av = *a;
    TrailVertex bv = *b;
    TrailVertex cv = *c;
    av.x *= scale;
    av.y *= scale;
    bv.x *= scale;
    bv.y *= scale;
    cv.x *= scale;
    cv.y *= scale;

    float min_x = av.x;
    float max_x = av.x;
    float min_y = av.y;
    float max_y = av.y;
    if (bv.x < min_x) min_x = bv.x;
    if (cv.x < min_x) min_x = cv.x;
    if (bv.x > max_x) max_x = bv.x;
    if (cv.x > max_x) max_x = cv.x;
    if (bv.y < min_y) min_y = bv.y;
    if (cv.y < min_y) min_y = cv.y;
    if (bv.y > max_y) max_y = bv.y;
    if (cv.y > max_y) max_y = cv.y;

    int x0 = FloorToInt(min_x) - 2;
    int x1 = CeilToInt(max_x) + 2;
    int y0 = FloorToInt(min_y) - 2;
    int y1 = CeilToInt(max_y) + 2;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > render->width) x1 = render->width;
    if (y1 > render->height) y1 = render->height;

    float area = TriangleEdge(av.x, av.y, bv.x, bv.y, cv.x, cv.y);
    if (FloatAbs(area) < 0.001f) return;
    float inverse_area = 1.0f / area;
    float sign = area > 0.0f ? 1.0f : -1.0f;
    float inv_len0 = 1.0f / ApproxLength(cv.x - bv.x, cv.y - bv.y);
    float inv_len1 = 1.0f / ApproxLength(av.x - cv.x, av.y - cv.y);
    float inv_len2 = 1.0f / ApproxLength(bv.x - av.x, bv.y - av.y);
    for (int y = y0; y < y1; ++y) {
        float py = y + 0.5f;
        for (int x = x0; x < x1; ++x) {
            float px = x + 0.5f;
            float e0 = TriangleEdge(bv.x, bv.y, cv.x, cv.y, px, py);
            float e1 = TriangleEdge(cv.x, cv.y, av.x, av.y, px, py);
            float e2 = TriangleEdge(av.x, av.y, bv.x, bv.y, px, py);
            float d0 = e0 * sign * inv_len0;
            float d1 = e1 * sign * inv_len1;
            float d2 = e2 * sign * inv_len2;
            float edge_dist = d0;
            if (d1 < edge_dist) edge_dist = d1;
            if (d2 < edge_dist) edge_dist = d2;
            float coverage = Clamp01(edge_dist + 0.5f);
            if (coverage <= 0.0f) {
                continue;
            }
            float w0 = e0 * inverse_area;
            float w1 = e1 * inverse_area;
            float w2 = e2 * inverse_area;
            RawBlendPixel(render, x, y,
                          av.r * w0 + bv.r * w1 + cv.r * w2,
                          av.g * w0 + bv.g * w1 + cv.g * w2,
                          av.b * w0 + bv.b * w1 + cv.b * w2,
                          (av.a * w0 + bv.a * w1 + cv.a * w2) * coverage);
        }
    }
}

void DrawLine(RenderContext* render, int x0, int y0, int x1, int y1, uint32_t color) {
    int s = render->scale;
    float r = (float)((color >> 16) & 255) / 255.0f;
    float g = (float)((color >> 8) & 255) / 255.0f;
    float b = (float)(color & 255) / 255.0f;
    float ax = (float)(x0 * s);
    float ay = (float)(y0 * s);
    float bx = (float)(x1 * s);
    float by = (float)(y1 * s);
    float dx = bx - ax;
    float dy = by - ay;
    float len_sq = dx * dx + dy * dy;
    if (len_sq <= 0.0001f) {
        DrawPixel(render, x0, y0, color);
        return;
    }

    int sx0 = x0 * s;
    int sx1 = x1 * s;
    int sy0 = y0 * s;
    int sy1 = y1 * s;
    int min_x = sx0 < sx1 ? sx0 : sx1;
    int max_x = sx0 > sx1 ? sx0 : sx1;
    int min_y = sy0 < sy1 ? sy0 : sy1;
    int max_y = sy0 > sy1 ? sy0 : sy1;
    min_x -= 2;
    min_y -= 2;
    max_x += 2;
    max_y += 2;
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= render->width) max_x = render->width - 1;
    if (max_y >= render->height) max_y = render->height - 1;

    float half_width = 0.5f * (float)s;
    for (int y = min_y; y <= max_y; ++y) {
        float py = (float)y + 0.5f;
        for (int x = min_x; x <= max_x; ++x) {
            float px = (float)x + 0.5f;
            float t = ((px - ax) * dx + (py - ay) * dy) / len_sq;
            t = Clamp01(t);
            float qx = ax + dx * t;
            float qy = ay + dy * t;
            float ex = px - qx;
            float ey = py - qy;
            float dist = ApproxLength(ex, ey);
            float alpha = Clamp01(half_width + 0.5f - dist);
            RawBlendPixel(render, x, y, r, g, b, alpha);
        }
    }
}

void DrawThickLine(RenderContext* render, int x0, int y0, int x1, int y1, int size, uint32_t color) {
    int dx = IntAbs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -IntAbs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int half = size / 2;

    for (;;) {
        DrawRect(render, x0 - half, y0 - half, size, size, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void DrawDiamond(RenderContext* render, int cx, int cy, int rx, int ry, uint32_t color) {
    DrawLine(render, cx, cy - ry, cx + rx, cy, color);
    DrawLine(render, cx + rx, cy, cx, cy + ry, color);
    DrawLine(render, cx, cy + ry, cx - rx, cy, color);
    DrawLine(render, cx - rx, cy, cx, cy - ry, color);
}

void FillCircle(RenderContext* render, int cx, int cy, int radius, uint32_t color) {
    FillCircleBlend(render, cx, cy, radius, color, 1.0f);
}

void FillCircleBlend(RenderContext* render, int cx, int cy, int radius, uint32_t color, float alpha) {
    if (radius <= 0) return;
    int s = render->scale;
    int scx = cx * s;
    int scy = cy * s;
    int sradius = radius * s;
    float r = (float)((color >> 16) & 255) / 255.0f;
    float g = (float)((color >> 8) & 255) / 255.0f;
    float b = (float)(color & 255) / 255.0f;
    float edge = (float)sradius - 0.5f;
    float outer = (float)sradius + 0.5f;
    float inner_sq = edge * edge;
    float outer_sq = outer * outer;
    for (int y = -sradius - 1; y <= sradius + 1; ++y) {
        float py = (float)y;
        for (int x = -sradius - 1; x <= sradius + 1; ++x) {
            float px = (float)x;
            float dist_sq = px * px + py * py;
            if (dist_sq <= inner_sq) {
                RawBlendPixel(render, scx + x, scy + y, r, g, b, alpha);
            } else if (dist_sq <= outer_sq) {
                float coverage = Clamp01((outer_sq - dist_sq) / (outer_sq - inner_sq));
                RawBlendPixel(render, scx + x, scy + y, r, g, b, alpha * coverage);
            }
        }
    }
}

void FillDiamond(RenderContext* render, int cx, int cy, int rx, int ry, uint32_t color) {
    if (ry <= 0) return;
    for (int y = -ry; y <= ry; ++y) {
        int half = rx * (ry - IntAbs(y)) / ry;
        DrawRect(render, cx - half, cy + y, half * 2 + 1, 1, color);
    }
}

int WorldX(RenderContext* render, float x) {
    float sx = WorldToScreenX(render->camera, x);
    return sx >= 0.0f ? (int)(sx + 0.5f) : (int)(sx - 0.5f);
}

int WorldY(RenderContext* render, float y) {
    float sy = WorldToScreenY(render->camera, y);
    return sy >= 0.0f ? (int)(sy + 0.5f) : (int)(sy - 0.5f);
}

int WorldW(RenderContext* render, float w) {
    float screen_w = w;
    if (screen_w <= 0.0f) return 0;
    int result = (int)(screen_w + 0.5f);
    return result > 0 ? result : 1;
}

int WorldH(RenderContext* render, float h) {
    float screen_h = h;
    if (screen_h <= 0.0f) return 0;
    int result = (int)(screen_h + 0.5f);
    return result > 0 ? result : 1;
}

void DrawWorldThickLine(RenderContext* render, float x0, float y0, float x1, float y1, int size, uint32_t color) {
    DrawThickLine(render, WorldX(render, x0), WorldY(render, y0), WorldX(render, x1), WorldY(render, y1), WorldW(render, (float)size), color);
}
