#include "delete_rules.h"

static const SettingsCategoryDef g_settings_categories[SETTINGS_CATEGORY_COUNT] = {
    { L"게임" },
    { L"오디오" },
    { L"화면" },
    { L"시스템" },
};

static const wchar_t* const g_toggle_values[] = {
    L"ON",
    L"OFF",
};

static const wchar_t* const g_gravity_direction_values[] = {
    L"↑",
    L"→",
    L"↓",
    L"←",
};

static const wchar_t* const g_volume_values[] = {
    L"0%",
    L"10%",
    L"20%",
    L"30%",
    L"40%",
    L"50%",
    L"60%",
    L"70%",
    L"80%",
    L"90%",
    L"100%",
};

static const wchar_t* const g_flex_values[] = {
    L"단단함",
    L"보통",
    L"말랑함",
};

static const wchar_t* const g_camera_values[] = {
    L"가까이",
    L"기본",
    L"멀리",
};

static const wchar_t* const g_speed_values[] = {
    L"느림",
    L"보통",
    L"빠름",
};

static const SettingsItemDef g_settings_items[] = {
    { SETTINGS_GAMEPLAY, FEATURE_COUNT, L"캐릭터 유연성", SETTINGS_VALUE_CHOICES, SETTINGS_ITEM_IMPLEMENTED, g_flex_values, SETTINGS_FLEXIBILITY_COUNT, SETTINGS_FLEXIBILITY_NORMAL },
    { SETTINGS_GAMEPLAY, FEATURE_COUNT, L"기믹 속도", SETTINGS_VALUE_STEPS, SETTINGS_ITEM_IMPLEMENTED, g_speed_values, SETTINGS_GAME_SPEED_COUNT, SETTINGS_GAME_SPEED_NORMAL },
    { SETTINGS_AUDIO, FEATURE_COUNT, L"BGM 음량", SETTINGS_VALUE_STEPS, SETTINGS_ITEM_IMPLEMENTED, g_volume_values, 11, 7 },
    { SETTINGS_AUDIO, FEATURE_COUNT, L"효과음 음량", SETTINGS_VALUE_STEPS, SETTINGS_ITEM_IMPLEMENTED, g_volume_values, 11, 7 },
    { SETTINGS_VIDEO, FEATURE_COUNT, L"무채색", SETTINGS_VALUE_TOGGLE, SETTINGS_ITEM_PLANNED, g_toggle_values, 2, 1 },
    { SETTINGS_VIDEO, FEATURE_COUNT, L"카메라 크기", SETTINGS_VALUE_CHOICES, SETTINGS_ITEM_PLANNED, g_camera_values, 3, 1 },
    { SETTINGS_VIDEO, FEATURE_COUNT, L"해상도 / 화면비", SETTINGS_VALUE_PENDING, SETTINGS_ITEM_PLANNED, 0, 0, 0 },
    { SETTINGS_SYSTEM, FEATURE_COUNT, L"중력 방향", SETTINGS_VALUE_CHOICES, SETTINGS_ITEM_IMPLEMENTED, g_gravity_direction_values, 4, GRAVITY_DOWN },
    { SETTINGS_SYSTEM, FEATURE_COLLISION_TYPE_A, L"BRICK 충돌", SETTINGS_VALUE_TOGGLE, SETTINGS_ITEM_IMPLEMENTED, g_toggle_values, 2, 0 },
};

const SettingsCategoryDef* SettingsCategoryAt(SettingsCategory category) {
    return &g_settings_categories[category];
}

const SettingsItemDef* SettingsItemAt(int index) {
    return &g_settings_items[index];
}

int SettingsItemTotalCount() {
    return (int)(sizeof(g_settings_items) / sizeof(g_settings_items[0]));
}

int SettingsItemCount(SettingsCategory category) {
    int count = 0;
    for (int i = 0; i < SettingsItemTotalCount(); ++i) {
        if (g_settings_items[i].category == category) {
            ++count;
        }
    }
    return count;
}

int SettingsItemIndex(SettingsCategory category, int category_index) {
    int at = 0;
    for (int i = 0; i < SettingsItemTotalCount(); ++i) {
        if (g_settings_items[i].category == category) {
            if (at == category_index) {
                return i;
            }
            ++at;
        }
    }
    return -1;
}

int SettingsFlexibilityItemIndex() {
    for (int i = 0; i < SettingsItemTotalCount(); ++i) {
        const SettingsItemDef* item = SettingsItemAt(i);
        if (item->category == SETTINGS_GAMEPLAY &&
            item->value_view == SETTINGS_VALUE_CHOICES &&
            item->values == g_flex_values) {
            return i;
        }
    }
    return -1;
}

static int SettingsAudioVolumeItemIndexByOrder(int order) {
    int found = 0;
    for (int i = 0; i < SettingsItemTotalCount(); ++i) {
        const SettingsItemDef* item = SettingsItemAt(i);
        if (item->category == SETTINGS_AUDIO &&
            item->value_view == SETTINGS_VALUE_STEPS &&
            item->values == g_volume_values) {
            if (found == order) {
                return i;
            }
            ++found;
        }
    }
    return -1;
}

int SettingsBgmVolumeItemIndex() {
    return SettingsAudioVolumeItemIndexByOrder(0);
}

int SettingsSfxVolumeItemIndex() {
    return SettingsAudioVolumeItemIndexByOrder(1);
}

int SettingsGravityDirectionItemIndex() {
    for (int i = 0; i < SettingsItemTotalCount(); ++i) {
        const SettingsItemDef* item = SettingsItemAt(i);
        if (item->category == SETTINGS_SYSTEM &&
            item->value_view == SETTINGS_VALUE_CHOICES &&
            item->values == g_gravity_direction_values) {
            return i;
        }
    }
    return -1;
}

int SettingsGameSpeedItemIndex() {
    for (int i = 0; i < SettingsItemTotalCount(); ++i) {
        const SettingsItemDef* item = SettingsItemAt(i);
        if (item->category == SETTINGS_GAMEPLAY &&
            item->value_view == SETTINGS_VALUE_STEPS &&
            item->values == g_speed_values) {
            return i;
        }
    }
    return -1;
}
