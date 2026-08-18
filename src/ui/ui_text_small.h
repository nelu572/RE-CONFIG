#pragma once

#include <stdint.h>

#include "render.h"

void UiTextSmallDraw(RenderContext* render, int x, int y, const char* text, int scale, uint32_t color);
void UiTextSmallDrawContext(RenderContext* render, int x, int y, const char* text, int scale, uint32_t color, uint32_t bg_color);
