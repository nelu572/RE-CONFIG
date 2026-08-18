#pragma once

#include <stdint.h>
#include <windows.h>

#include "game_config.h"
#include "rect_i.h"
#include "render.h"

#ifndef RENDER_SCALE
#define RENDER_SCALE 2
#endif
#define RENDER_W (FB_W * RENDER_SCALE)
#define RENDER_H (FB_H * RENDER_SCALE)

typedef void (*FramebufferDrawCallback)();

void FramebufferInit(HWND window, RenderContext* render);
uint32_t* FramebufferPixels();
uint32_t* FramebufferRenderPixels();
uint32_t* FramebufferStaticRenderPixels();
RectI FramebufferClampRect(RectI rect);
void FramebufferCopyPixels(uint32_t* dst, const uint32_t* src, int count);
void FramebufferCopyStaticToLive();
void FramebufferDownsampleRectFromTo(const uint32_t* src_pixels, uint32_t* dst_pixels, RectI rect);
void FramebufferDownsampleRenderTarget();
void FramebufferRestoreStaticRect(RectI rect);
void FramebufferBuildStaticCache(RenderContext* render, uint32_t bg_color, FramebufferDrawCallback draw_static, int copy_to_live);
void FramebufferPresent(uint32_t bg_color);
