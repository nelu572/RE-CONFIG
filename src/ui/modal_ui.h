#pragma once

#include <stdint.h>

#include "rect_i.h"
#include "render.h"

static const uint32_t MODAL_UI_DIM_COLOR = 0x000d0a0b;
static const float MODAL_UI_DIM_ALPHA = 0.52f;
static const uint32_t MODAL_UI_PANEL_FILL = 0x00161214;
static const uint32_t MODAL_UI_PANEL_BORDER = 0x00633039;
static const uint32_t MODAL_UI_PANEL_LINE = 0x00513036;
static const uint32_t MODAL_UI_TEXT = 0x00f5eee4;
static const uint32_t MODAL_UI_TEXT_DIM = 0x00d8d0ca;
static const uint32_t MODAL_UI_CYAN = 0x0039cfc3;
static const uint32_t MODAL_UI_CRIMSON = 0x00f04a5b;
static const uint32_t MODAL_UI_CRIMSON_ARROW = 0x008a3541;

uint32_t ModalUiFadeColor(uint32_t color, float fade);
uint32_t ModalUiLerpColor(uint32_t a, uint32_t b, float t);
void DrawModalBlendRect(RenderContext* render, int x, int y, int w, int h, uint32_t color, float alpha);
void DrawModalDimOverlay(RenderContext* render, float alpha_scale);
void DrawModalDimOverlayRect(RenderContext* render, RectI rect, float alpha_scale);
void DrawUIPanel(RenderContext* render, RectI rect, float alpha_scale);