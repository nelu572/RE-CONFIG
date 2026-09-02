#pragma once

#include <stdint.h>

#include "camera.h"

struct RenderContext {
    uint32_t* pixels;
    Camera* camera;
    int width;
    int height;
    int scale;
};

struct TrailVertex {
    float x;
    float y;
    float r;
    float g;
    float b;
    float a;
};

void RenderClear(RenderContext* render, uint32_t color);
void DrawPixel(RenderContext* render, int x, int y, uint32_t color);
void DrawRect(RenderContext* render, int x, int y, int w, int h, uint32_t color);
void DrawRectBlend(RenderContext* render, int x, int y, int w, int h, uint32_t color, float alpha);
void DrawRectOutline(RenderContext* render, int x, int y, int w, int h, uint32_t color);
void DrawLine(RenderContext* render, int x0, int y0, int x1, int y1, uint32_t color);
void DrawThickLine(RenderContext* render, int x0, int y0, int x1, int y1, int size, uint32_t color);
void DrawDiamond(RenderContext* render, int cx, int cy, int rx, int ry, uint32_t color);
void FillCircle(RenderContext* render, int cx, int cy, int radius, uint32_t color);
void FillCircleBlend(RenderContext* render, int cx, int cy, int radius, uint32_t color, float alpha);
void FillDiamond(RenderContext* render, int cx, int cy, int rx, int ry, uint32_t color);
void BlendPixel(RenderContext* render, int x, int y, float r, float g, float b, float alpha);
void DrawAlphaTriangle(RenderContext* render, const TrailVertex* a, const TrailVertex* b, const TrailVertex* c);
int WorldX(RenderContext* render, float x);
int WorldY(RenderContext* render, float y);
int WorldW(RenderContext* render, float w);
int WorldH(RenderContext* render, float h);
void DrawWorldThickLine(RenderContext* render, float x0, float y0, float x1, float y1, int size, uint32_t color);
