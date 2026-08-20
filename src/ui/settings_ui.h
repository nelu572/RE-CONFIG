#pragma once

#include <stdint.h>

#include "delete_rules.h"
#include "rect_i.h"
#include "render.h"

typedef int (*SettingsUiFeatureActiveCallback)(DeleteFeature feature);
typedef void (*SettingsUiToggleFeatureCallback)(DeleteFeature feature);
typedef void (*SettingsUiSetItemValueCallback)(int item_index, int value);
typedef void (*SettingsUiDrawContextTextCallback)(int x, int y, const char* text, int scale, uint32_t color);

struct SettingsUiColors {
    uint32_t bg;
    uint32_t text;
    uint32_t text_dim;
    uint32_t type_a;
    uint32_t main_red;
    uint32_t bright_red;
    uint32_t accent;
};

struct SettingsUiTutorialState {
    int mark_system_category;
    int mark_type_a_setting;
    int active;
    uint32_t primary_color;
    uint32_t secondary_color;
};

void SettingsUiInit(RenderContext* render,
                    SettingsUiFeatureActiveCallback feature_active,
                    SettingsUiToggleFeatureCallback toggle_feature,
                    SettingsUiSetItemValueCallback set_item_value,
                    SettingsUiDrawContextTextCallback draw_context_text);
void SettingsUiReset();
void SettingsUiSetItemValue(int item_index, int value);
void SettingsUiInvalidateCache();
void SettingsUiMarkDirty();
void SettingsUiMarkFullDirty();
void SettingsUiClearDirty();
void SettingsUiOpen(int use_static_cache);
void SettingsUiClose();
void SettingsUiUpdateInput(int use_static_cache);
void SettingsUiUpdateFade(float dt);
int SettingsUiIsOpen();
int SettingsUiOverlayVisible();
int SettingsUiAnimationActive();
int SettingsUiIsDirty();
int SettingsUiSelectedCategory();
int SettingsUiCategoryFocusActive();
int SettingsUiItemFocusActive();
DeleteFeature SettingsUiSelectedFeature();
int SettingsUiHighlightsFeature(DeleteFeature feature);
RectI SettingsUiMenuDirtyRect();
void SettingsUiDrawDimRect(RectI rect);
void SettingsUiDrawMenu(const SettingsUiColors* colors, const SettingsUiTutorialState* tutorial);
void SettingsUiBuildMenuCache(const SettingsUiColors* colors);
