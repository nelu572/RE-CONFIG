#pragma once

#include <stdint.h>
#include <windows.h>

#include "render.h"

typedef void (*UiTextPerfCallback)(int cache_miss);

void UiTextInit(HWND window, RenderContext* render, int font_mode, int text_quality, UiTextPerfCallback perf_callback);
void UiTextEnsureSurface();
void UiTextWarmSettingsFonts();
void UiTextWarmSettingsTextCache();
void DrawTextUi(int x, int y, const wchar_t* text, int size, uint32_t color, int bold);
