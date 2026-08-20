#pragma once

enum DeleteFeature {
    FEATURE_JUMP,
    FEATURE_GRAVITY,
    FEATURE_COLLISION_TYPE_A,
    FEATURE_COUNT
};

enum GravityDirection {
    GRAVITY_UP,
    GRAVITY_RIGHT,
    GRAVITY_DOWN,
    GRAVITY_LEFT
};

struct DeleteState {
    unsigned char deleted[FEATURE_COUNT];
};

enum SettingsCategory {
    SETTINGS_GAMEPLAY,
    SETTINGS_AUDIO,
    SETTINGS_VIDEO,
    SETTINGS_SYSTEM,
    SETTINGS_CATEGORY_COUNT
};

struct SettingsCategoryDef {
    const wchar_t* name;
};

enum SettingsValueView {
    SETTINGS_VALUE_TOGGLE,
    SETTINGS_VALUE_STEPS,
    SETTINGS_VALUE_CHOICES,
    SETTINGS_VALUE_PENDING
};

enum SettingsItemStatus {
    SETTINGS_ITEM_IMPLEMENTED,
    SETTINGS_ITEM_PLANNED
};

struct SettingsItemDef {
    SettingsCategory category;
    DeleteFeature feature;
    const wchar_t* name;
    SettingsValueView value_view;
    SettingsItemStatus status;
    const wchar_t* const* values;
    int value_count;
    int default_value;
};

const SettingsCategoryDef* SettingsCategoryAt(SettingsCategory category);
const SettingsItemDef* SettingsItemAt(int index);
int SettingsItemTotalCount();
int SettingsItemCount(SettingsCategory category);
int SettingsItemIndex(SettingsCategory category, int category_index);
int SettingsGravityDirectionItemIndex();
