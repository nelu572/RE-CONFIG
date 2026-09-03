#include <stdint.h>
#include <stddef.h>
#include <windows.h>

#include "game_config.h"
#include "audio.h"
#include "perf.h"
#include "exit_sequence.h"
#include "framebuffer.h"
#include "input.h"
#include "main_menu.h"
#include "pause_menu.h"
#include "render.h"
#include "settings_ui.h"
#include "stage_cache.h"
#include "stage_render.h"
#include "stage_update.h"
#include "tutorial_ui.h"
#include "ui_text.h"
#include "ui_text_small.h"
#include "world.h"

#ifdef _MSC_VER
extern "C" int _fltused = 0;
#pragma function(memcpy)
#endif
extern "C" void* memcpy(void* dest, const void* src, size_t count) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < count; ++i) {
        d[i] = s[i];
    }
    return dest;
}

#ifdef _MSC_VER
#pragma function(memset)
#endif
extern "C" void* memset(void* dest, int value, size_t count) {
    unsigned char* d = (unsigned char*)dest;
    for (size_t i = 0; i < count; ++i) {
        d[i] = (unsigned char)value;
    }
    return dest;
}

static HWND g_window;
static int g_running = 1;
static int g_fullscreen = 0;
static WINDOWPLACEMENT g_windowed_placement = { sizeof(g_windowed_placement) };
static GameState g_game;
static RenderContext g_render = { 0, &g_game.camera, RENDER_W, RENDER_H, RENDER_SCALE };
static MainMenuState g_main_menu;
static PauseMenuState g_pause_menu;
static int g_overlay_redraw_pending = 0;
static int g_pause_redraw_pending = 0;
static constexpr int START_ROOM_INDEX = 0;
static constexpr int DEBUG_ROOM_00_INDEX = 0;
static constexpr int DEBUG_ROOM_01_INDEX = 1;
static constexpr int STAGE_SELECT_LAST_ROOM_INDEX = 10;
static constexpr int STAGE_SELECT_DISPLAY_COUNT = STAGE_SELECT_LAST_ROOM_INDEX + 1;
static int g_stage_select_index = 0;
static float g_stage_select_world_offset = 0.0f;
static float g_stage_select_target_offset = 0.0f;
static float g_stage_select_start_offset = 0.0f;
static float g_stage_select_player_y = 0.0f;
static float g_stage_select_start_player_y = 0.0f;
static float g_stage_select_target_player_y = 0.0f;
static float g_stage_select_anim_seconds = 0.0f;
static float g_stage_select_platform_grow_seconds = 0.0f;
static float g_stage_select_player_move_dir = 0.0f;
static float g_stage_select_player_face_dir = 0.0f;
static float g_stage_select_player_visual_sx = 1.0f;
static float g_stage_select_player_visual_sy = 1.0f;
static double g_stage_select_last_seconds = 0.0;
static int g_stage_select_cleared[STAGE_SELECT_DISPLAY_COUNT];
static int g_last_played_room = START_ROOM_INDEX;

static constexpr unsigned int STAGE_PROGRESS_MAGIC = 0x31535652u;
static constexpr unsigned int STAGE_PROGRESS_VERSION = 3u;
static constexpr unsigned int STAGE_PROGRESS_CLEARS_ONLY_VERSION = 2u;
static constexpr int STAGE_PROGRESS_V2_DATA_SIZE = 8 + STAGE_SELECT_DISPLAY_COUNT;
static constexpr int STAGE_PROGRESS_CLEARED_OFFSET = 12;
static constexpr int STAGE_PROGRESS_DATA_SIZE = STAGE_PROGRESS_CLEARED_OFFSET + STAGE_SELECT_DISPLAY_COUNT;

static void StageProgressWriteU32(unsigned char* data, int offset, unsigned int value) {
    data[offset + 0] = (unsigned char)(value & 255u);
    data[offset + 1] = (unsigned char)((value >> 8) & 255u);
    data[offset + 2] = (unsigned char)((value >> 16) & 255u);
    data[offset + 3] = (unsigned char)((value >> 24) & 255u);
}

static unsigned int StageProgressReadU32(const unsigned char* data, int offset) {
    return (unsigned int)data[offset + 0] |
           ((unsigned int)data[offset + 1] << 8) |
           ((unsigned int)data[offset + 2] << 16) |
           ((unsigned int)data[offset + 3] << 24);
}

static int StageProgressClampRoomIndex(int room_index) {
    int room_count = DevelopedRoomCount();
    if (room_count <= 0) {
        return 0;
    }
    if (room_index < 0) {
        return 0;
    }
    if (room_index >= room_count) {
        return room_count - 1;
    }
    return room_index;
}

static int StageProgressRoomAfterHighestCleared() {
    int highest_cleared = -1;
    int room_count = DevelopedRoomCount();
    int count = room_count < STAGE_SELECT_DISPLAY_COUNT ? room_count : STAGE_SELECT_DISPLAY_COUNT;
    for (int i = 0; i < count; ++i) {
        if (g_stage_select_cleared[i]) {
            highest_cleared = i;
        }
    }
    if (highest_cleared < 0) {
        return START_ROOM_INDEX;
    }
    return StageProgressClampRoomIndex(highest_cleared + 1);
}

static void StageProgressSave() {
    unsigned char data[STAGE_PROGRESS_DATA_SIZE];
    for (int i = 0; i < STAGE_PROGRESS_DATA_SIZE; ++i) {
        data[i] = 0;
    }

    StageProgressWriteU32(data, 0, STAGE_PROGRESS_MAGIC);
    StageProgressWriteU32(data, 4, STAGE_PROGRESS_VERSION);
    StageProgressWriteU32(data, 8, (unsigned int)StageProgressClampRoomIndex(g_last_played_room));
    for (int i = 0; i < (int)(sizeof(g_stage_select_cleared) / sizeof(g_stage_select_cleared[0])); ++i) {
        data[STAGE_PROGRESS_CLEARED_OFFSET + i] = g_stage_select_cleared[i] ? 1 : 0;
    }

    HANDLE file = CreateFileA("save.dat", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD written = 0;
    WriteFile(file, data, (DWORD)sizeof(data), &written, 0);
    CloseHandle(file);
}

static void StageProgressLoad() {
    unsigned char data[STAGE_PROGRESS_DATA_SIZE];
    for (int i = 0; i < STAGE_PROGRESS_DATA_SIZE; ++i) {
        data[i] = 0;
    }

    HANDLE file = CreateFileA("save.dat", GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD read = 0;
    int ok = ReadFile(file, data, (DWORD)sizeof(data), &read, 0);
    CloseHandle(file);
    if (!ok || read < 8 || StageProgressReadU32(data, 0) != STAGE_PROGRESS_MAGIC) {
        return;
    }

    unsigned int version = StageProgressReadU32(data, 4);
    int cleared_offset = 8;
    if (version == STAGE_PROGRESS_VERSION) {
        if (read < STAGE_PROGRESS_DATA_SIZE) {
            return;
        }
        g_last_played_room = StageProgressClampRoomIndex((int)StageProgressReadU32(data, 8));
        cleared_offset = STAGE_PROGRESS_CLEARED_OFFSET;
    } else if (version == STAGE_PROGRESS_CLEARS_ONLY_VERSION) {
        if (read < STAGE_PROGRESS_V2_DATA_SIZE) {
            return;
        }
    } else {
        return;
    }

    for (int i = 0; i < (int)(sizeof(g_stage_select_cleared) / sizeof(g_stage_select_cleared[0])); ++i) {
        g_stage_select_cleared[i] = data[cleared_offset + i] ? 1 : 0;
    }
    if (version == STAGE_PROGRESS_CLEARS_ONLY_VERSION) {
        g_last_played_room = StageProgressRoomAfterHighestCleared();
    }
}
enum AppState {
    APP_STATE_MAIN_MENU,
    APP_STATE_STAGE_SELECT,
    APP_STATE_GAME
};

static AppState g_app_state = APP_STATE_MAIN_MENU;
static AppState g_app_transition_target_state = APP_STATE_MAIN_MENU;
static int g_app_transition_pending = 0;
static int g_app_transition_target_room = 0;
static int g_stage_select_entry_from_room = -1;
static float g_app_transition_amount = 0.0f;
static float g_app_transition_hold_seconds = 0.0f;
static constexpr float APP_TRANSITION_HOLD_SECONDS = 0.09f;
static double g_app_transition_last_seconds = 0.0;

static uint32_t COL_BG = 0x00292324;
static const uint32_t COL_STAGE_SOFT = 0x006f3038;
static uint32_t COL_PLATFORM = 0x00b73845;
static uint32_t COL_PLATFORM_TOP = 0x00b73845;
static uint32_t COL_PLATFORM_FACE = 0x00b73845;
static const uint32_t COL_PLATFORM_EDGE = 0x006f3038;
static uint32_t COL_PLAYER = 0x00f7f0e5;
static const uint32_t COL_PLAYER_DARK = 0x00292324;
static uint32_t COL_TEXT = 0x00f7f0e5;
static uint32_t COL_TEXT_DIM = 0x00d8b9b4;
static const uint32_t COL_TEXT_DARK = 0x006f3038;
static const uint32_t COL_MENU_CYAN = 0x003ffcff;
static uint32_t COL_TYPE_A = 0x006f3038;
static uint32_t COL_TYPE_A_PATTERN = 0x00c2a4a8;
static uint32_t COL_TYPE_A_OFF = 0x00cb4855;
static uint32_t COL_TYPE_A_OFF_PATTERN = 0x00e78d96;
static uint32_t COL_EFFECT = 0x008a3b45;
static int g_type_a_art_test = 0;
static int g_type_a_off_line_thickness = 2;
static int g_type_a_off_visible_path_len = 14;
static double g_type_a_phase_lock_seconds = -1.0;

static void ClearBytes(void* dest, size_t count) {
    volatile unsigned char* out = (volatile unsigned char*)dest;
    for (size_t i = 0; i < count; ++i) {
        out[i] = 0;
    }
}

static void ApplyVisualPalette(uint32_t bg, uint32_t ground, uint32_t player, uint32_t type_a, uint32_t exit_color, uint32_t text, uint32_t text_dim, uint32_t effect) {
    COL_BG = bg;
    COL_PLATFORM = ground;
    COL_PLATFORM_TOP = ground;
    COL_PLATFORM_FACE = ground;
    COL_PLAYER = player;
    COL_TYPE_A = type_a;
    ExitSequenceSetExitColor(exit_color);
    COL_TEXT = text;
    COL_TEXT_DIM = text_dim;
    COL_EFFECT = effect;
}

static void LoadVisualPalette() {
    char palette[8];
    DWORD len = GetEnvironmentVariableA("RECONFIG_PALETTE", palette, (DWORD)sizeof(palette));
    if (len > 0 && len < sizeof(palette) && (palette[0] == 'B' || palette[0] == 'b')) {
        ApplyVisualPalette(
            0x00292324,
            0x00b73845,
            0x00f7f0e5,
            0x006f3038,
            0x00f04a5b,
            0x00f7f0e5,
            0x00d8b9b4,
            0x008a3b45);
        return;
    }

    ApplyVisualPalette(
        0x00292324,
        0x00b73845,
        0x00f7f0e5,
        0x006f3038,
        0x00f04a5b,
        0x00f7f0e5,
        0x00d8b9b4,
        0x008a3b45);
}

static void LoadBrickVisual() {
    COL_TYPE_A = 0x00cb4855;
    COL_TYPE_A_PATTERN = 0x00e78d96;
    COL_TYPE_A_OFF = COL_TYPE_A;
    COL_TYPE_A_OFF_PATTERN = COL_TYPE_A_PATTERN;

    g_type_a_off_line_thickness = 2;
    g_type_a_off_visible_path_len = 14;

    char fixed_phase[8];
    DWORD len = GetEnvironmentVariableA("RECONFIG_BRICK_OFF_PHASE_LOCK", fixed_phase, (DWORD)sizeof(fixed_phase));
    g_type_a_phase_lock_seconds = (len > 0 && len < sizeof(fixed_phase)) ? 0.45 : -1.0;
}

static void LoadArtTestMode() {
    char enabled[8];
    DWORD len = GetEnvironmentVariableA("RECONFIG_TYPE_A_ART_TEST", enabled, (DWORD)sizeof(enabled));
    if (len > 0 && len < sizeof(enabled) && (enabled[0] == 'O' || enabled[0] == 'o' || enabled[0] == '4')) {
        g_type_a_art_test = 4;
    } else if (len > 0 && len < sizeof(enabled) && (enabled[0] == 'P' || enabled[0] == 'p' || enabled[0] == '3')) {
        g_type_a_art_test = 3;
    } else if (len > 0 && len < sizeof(enabled) && (enabled[0] == 'G' || enabled[0] == 'g' || enabled[0] == '2')) {
        g_type_a_art_test = 2;
    } else {
        g_type_a_art_test = len > 0 && len < sizeof(enabled) && enabled[0] != '0';
    }
}

static int FeatureActive(DeleteFeature feature) {
    return GameFeatureActive(&g_game, feature);
}

static const RoomDef* CurrentRoom() {
    return GameCurrentRoom(&g_game);
}

static void ToggleFeature(DeleteFeature feature) {
    GameToggleFeature(&g_game, feature);
}

static void SetSettingsItemValue(int item_index, int value) {
    if (item_index == SettingsGravityDirectionItemIndex()) {
        GameSetGravityDirection(&g_game, (GravityDirection)value);
    } else if (item_index == SettingsFlexibilityItemIndex()) {
        GameSetPlayerFlexibility(&g_game, value);
    } else if (item_index == SettingsBgmVolumeItemIndex()) {
        AudioSetBgmVolume(SettingsUiBgmVolume());
    }
}

static void DrawTextSmallCallback(int x, int y, const char* text, int scale, uint32_t color) {
    UiTextSmallDraw(&g_render, x, y, text, scale, color);
}

static void DrawContextText(int x, int y, const char* text, int scale, uint32_t color) {
    UiTextSmallDrawContext(&g_render, x, y, text, scale, color, COL_BG);
}

static int TypeABumpVisible() {
    return GameTypeABumpVisible(&g_game);
}

static int TypeASettingFeedbackVisible() {
    return GameTypeASettingFeedbackVisible(&g_game);
}

static void ResetStage() {
    GameResetStage(&g_game);
    AudioSetBgmVolume(SettingsUiBgmVolume());
}

static int ClampRoomIndex(int room_index) {
    if (room_index < 0) {
        return 0;
    }
    int room_count = DevelopedRoomCount();
    if (room_index >= room_count) {
        return room_count - 1;
    }
    return room_index;
}

static float StageSelectPlayerYForStage(int stage_index);
static float StageSelectTargetOffset(int stage_index);

static float AppTransitionApproachF(float value, float target, float step) {
    if (value < target) {
        value += step;
        if (value > target) value = target;
    } else if (value > target) {
        value -= step;
        if (value < target) value = target;
    }
    return value;
}

static int AppTransitionActive() {
    return g_app_transition_pending || g_app_transition_hold_seconds > 0.001f || g_app_transition_amount > 0.001f;
}

static void EnterMainMenuNow() {
    SettingsUiClose();
    PauseMenuClose(&g_pause_menu);
    g_app_state = APP_STATE_MAIN_MENU;
    MainMenuInit(&g_main_menu);
}

static void EnterStageSelectNow(int stage_index) {
    SettingsUiClose();
    PauseMenuClose(&g_pause_menu);

    int target_stage = ClampRoomIndex(stage_index);
    int entry_stage = g_stage_select_entry_from_room;
    g_stage_select_entry_from_room = -1;
    if (entry_stage < 0 || entry_stage >= DevelopedRoomCount()) {
        entry_stage = target_stage;
    }

    g_stage_select_index = target_stage;
    g_stage_select_target_offset = StageSelectTargetOffset(target_stage);
    g_stage_select_target_player_y = StageSelectPlayerYForStage(target_stage);
    g_stage_select_start_offset = StageSelectTargetOffset(entry_stage);
    g_stage_select_start_player_y = StageSelectPlayerYForStage(entry_stage);
    g_stage_select_world_offset = g_stage_select_start_offset;
    g_stage_select_player_y = g_stage_select_start_player_y;
    g_stage_select_anim_seconds = entry_stage == target_stage ? 0.36f : 0.0f;
    g_stage_select_platform_grow_seconds = entry_stage == target_stage ? 0.16f : 0.0f;
    g_stage_select_player_move_dir = g_stage_select_target_offset < g_stage_select_start_offset ? 1.0f : -1.0f;
    g_stage_select_player_face_dir = entry_stage == target_stage ? 0.0f : g_stage_select_player_move_dir;
    g_stage_select_player_visual_sx = entry_stage == target_stage ? 1.0f : 1.14f;
    g_stage_select_player_visual_sy = entry_stage == target_stage ? 1.0f : 0.84f;
    g_stage_select_last_seconds = PerfNowSeconds();
    g_app_state = APP_STATE_STAGE_SELECT;
}

static void EnterStageNow(int room_index) {
    SettingsUiClose();
    PauseMenuClose(&g_pause_menu);
    GameClearCheckpoint(&g_game);
    g_game.current_room = ClampRoomIndex(room_index);
    g_last_played_room = g_game.current_room;
    StageProgressSave();
    g_app_state = APP_STATE_GAME;
    ResetStage();
}

static void DebugResetDataAndEnterMainMenu() {
    PauseMenuClose(&g_pause_menu);
    ClearBytes(g_stage_select_cleared, sizeof(g_stage_select_cleared));
    g_last_played_room = START_ROOM_INDEX;
    StageProgressSave();

    g_app_transition_target_state = APP_STATE_MAIN_MENU;
    g_app_transition_target_room = START_ROOM_INDEX;
    g_app_transition_pending = 0;
    g_app_transition_amount = 0.0f;
    g_app_transition_hold_seconds = 0.0f;
    g_app_transition_last_seconds = 0.0;

    g_game.current_room = START_ROOM_INDEX;
    g_last_played_room = g_game.current_room;
    ResetStage();
    EnterMainMenuNow();
}

static void ApplyAppTransitionTarget() {
    if (g_app_transition_target_state == APP_STATE_MAIN_MENU) {
        EnterMainMenuNow();
    } else if (g_app_transition_target_state == APP_STATE_STAGE_SELECT) {
        EnterStageSelectNow(g_app_transition_target_room);
    } else {
        EnterStageNow(g_app_transition_target_room);
    }
}

static void StartAppTransition(AppState target_state, int room_index) {
    int target_room = ClampRoomIndex(room_index);
    if (AppTransitionActive()) {
        return;
    }
    if (target_state == g_app_state && (target_state != APP_STATE_GAME || target_room == g_game.current_room)) {
        return;
    }

    SettingsUiClose();
    AudioPlayTransition(SettingsUiSfxVolume());
    if (target_state == APP_STATE_MAIN_MENU) {
        g_last_played_room = target_room;
        StageProgressSave();
    }
    g_app_transition_target_state = target_state;
    g_app_transition_target_room = target_room;
    g_app_transition_pending = 1;
    g_app_transition_amount = 0.001f;
    g_app_transition_hold_seconds = 0.0f;
}

static void UpdateAppTransition(float dt) {
    if (g_app_transition_pending) {
        g_app_transition_amount = AppTransitionApproachF(g_app_transition_amount, 1.0f, dt * 3.0f);
        if (g_app_transition_amount >= 0.999f) {
            ApplyAppTransitionTarget();
            g_app_transition_pending = 0;
            g_app_transition_amount = 1.0f;
            g_app_transition_hold_seconds = APP_TRANSITION_HOLD_SECONDS;
        }
        return;
    }

    if (g_app_transition_hold_seconds > 0.0f) {
        g_app_transition_hold_seconds -= dt;
        if (g_app_transition_hold_seconds < 0.0f) {
            g_app_transition_hold_seconds = 0.0f;
        }
        return;
    }

    if (g_app_transition_amount > 0.0f) {
        g_app_transition_amount = AppTransitionApproachF(g_app_transition_amount, 0.0f, dt * 2.6f);
    }
}

static void PlayGameAudioEvents(int events);
static void PlayUiAudioForInput();

static void DrawAppTransition() {
    ExitSequenceDrawTransitionAmount(&g_render, g_app_transition_amount);
}

static void StageProgressSaveCurrentPlace() {
    if (g_app_state == APP_STATE_GAME) {
        g_last_played_room = g_game.current_room;
    } else if (g_app_state == APP_STATE_STAGE_SELECT) {
        g_last_played_room = g_stage_select_index;
    }
    StageProgressSave();
}

static void UpdateStage(float dt) {
    GameUpdateStage(&g_game, dt, !g_perf_config.disable_static_cache);
    PlayGameAudioEvents(g_game.audio_events);
    g_game.audio_events = 0;
    if (g_game.cleared_room_this_frame >= 0 &&
        g_game.cleared_room_this_frame < (int)(sizeof(g_stage_select_cleared) / sizeof(g_stage_select_cleared[0]))) {
        int cleared_room = g_game.cleared_room_this_frame;
        if (!g_stage_select_cleared[g_game.cleared_room_this_frame]) {
            g_stage_select_cleared[g_game.cleared_room_this_frame] = 1;
        }
        g_game.cleared_room_this_frame = -1;
        int next_room = cleared_room + 1;
        if (next_room >= DevelopedRoomCount()) {
            next_room = cleared_room;
        }
        g_last_played_room = next_room;
        StageProgressSave();
        g_stage_select_entry_from_room = next_room == cleared_room ? -1 : cleared_room;
        StartAppTransition(APP_STATE_STAGE_SELECT, next_room);
    }
    const RoomDef* room = CurrentRoom();
    if (SettingsUiIsOpen()) {
        AudioUpdateSpeaker(0.0, 0.0f, 0);
    } else {
        AudioUpdateSpeaker(PerfNowSeconds() - g_game.room_started_at_seconds,
                           SettingsUiSfxVolume(),
                           room && room->speaker_count > 0);
    }
}

static void PlayGameAudioEvents(int events) {
    if (!events) {
        return;
    }
    float volume = SettingsUiSfxVolume();
    if (events & GAME_AUDIO_JUMP) AudioPlayJump(volume);
    if (events & GAME_AUDIO_LAND) AudioPlayLand(volume);
    if (events & GAME_AUDIO_DEATH) AudioPlayDeath(volume);
    if (events & GAME_AUDIO_CLEAR) AudioPlayClear(volume);
    if (events & GAME_AUDIO_SWITCH) { }
}

static void PlayUiAudioForInput() {
    int ui_context = g_app_state == APP_STATE_MAIN_MENU ||
                     g_app_state == APP_STATE_STAGE_SELECT ||
                     SettingsUiOverlayVisible() ||
                     PauseMenuIsOpen(&g_pause_menu);
    if (!ui_context || AppTransitionActive()) {
        return;
    }

    float volume = SettingsUiSfxVolume();
    if (InputWasPressed(KEY_UP) || InputWasPressed(KEY_DOWN) ||
        InputWasPressed(KEY_LEFT) || InputWasPressed(KEY_RIGHT)) {
        AudioPlayUiMove(volume);
    }
    if (InputWasPressed(KEY_Z)) {
        AudioPlayUiConfirm(volume);
    }
    if (InputWasPressed(KEY_X) || InputWasPressed(KEY_ESCAPE)) {
        AudioPlayUiBack(volume);
    }
}
static int SettingsHighlightsTypeA() {
    return 0;
}

static SettingsUiColors CurrentSettingsColors() {
    SettingsUiColors colors;
    colors.bg = COL_BG;
    colors.text = COL_TEXT;
    colors.text_dim = COL_TEXT_DIM;
    colors.type_a = COL_TYPE_A;
    colors.main_red = COL_PLATFORM;
    colors.bright_red = 0x00f04a5b;
    colors.accent = COL_EFFECT;
    return colors;
}

static StageRenderState CurrentStageRenderState() {
    StageRenderState state;
    state.render = &g_render;
    state.room = CurrentRoom();
    state.player = &g_game.player;
    state.player_particles = g_game.player_particles;
    state.player_particle_count = PLAYER_PARTICLE_COUNT;
    state.player_visible = !g_game.player_dead;
    state.gravity_direction = g_game.gravity_direction;
    state.bg_color = COL_BG;
    state.platform_color = COL_PLATFORM;
    state.player_color = COL_PLAYER;
    state.type_a_color = COL_TYPE_A;
    state.type_a_pattern_color = COL_TYPE_A_PATTERN;
    state.type_a_off_color = COL_TYPE_A_OFF;
    state.type_a_off_pattern_color = COL_TYPE_A_OFF_PATTERN;
    state.render_time_seconds = g_type_a_phase_lock_seconds >= 0.0 ? g_type_a_phase_lock_seconds : PerfNowSeconds();
    state.speaker_time_seconds = PerfNowSeconds() - g_game.room_started_at_seconds;
    state.piston_time_seconds = g_game.piston_time_seconds;
    state.piston_effective_extension = g_game.piston_effective_extension;
    state.gravity_boxes = g_game.gravity_boxes;
    state.gravity_box_count = CurrentRoom()->gravity_box_count;
    if (state.gravity_box_count > GAME_MAX_GRAVITY_BOXES) state.gravity_box_count = GAME_MAX_GRAVITY_BOXES;
    state.walker_enemies = g_game.walker_enemies;
    state.walker_enemy_count = CurrentRoom()->walker_enemy_count;
    if (state.walker_enemy_count > GAME_MAX_WALKER_ENEMIES) state.walker_enemy_count = GAME_MAX_WALKER_ENEMIES;
    state.walker_enemy_spike_amount = g_game.walker_enemy_spike_amount;
    state.walker_enemy_squash_amount = g_game.walker_enemy_squash_amount;
    state.walker_enemy_eye_crouch_amount = g_game.walker_enemy_eye_crouch_amount;
    state.walker_enemy_turn_squash = g_game.walker_enemy_turn_squash;
    state.walker_enemy_direction = g_game.walker_enemy_direction;
    state.walker_enemy_player_near = g_game.walker_enemy_player_near;
    state.pressure_switch_pressed = g_game.pressure_switch_pressed;
    state.pressure_switch_anim = g_game.pressure_switch_anim;
    state.pressure_platform_open_amount = g_game.pressure_platform_open_amount;
    state.room_exit_unlocked = g_game.room_exit_unlocked;
    state.checkpoint_active = g_game.checkpoint_active;
    state.checkpoint_flag_drop = g_game.checkpoint_flag_drop;
    state.type_a_off_line_thickness = g_type_a_off_line_thickness;
    state.type_a_off_visible_path_len = g_type_a_off_visible_path_len;
    state.effect_color = COL_EFFECT;
    state.text_color = COL_TEXT;
    state.text_dim_color = COL_TEXT_DIM;
    state.type_a_active = FeatureActive(FEATURE_COLLISION_TYPE_A);
    state.speaker_volume = SettingsUiSfxVolume();
    state.highlight_type_a = SettingsHighlightsTypeA();
    state.type_a_bump_visible = TypeABumpVisible();
    state.type_a_setting_feedback_visible = TypeASettingFeedbackVisible();
    state.settings_overlay_visible = SettingsUiOverlayVisible();
    state.disable_static_cache = g_perf_config.disable_static_cache;
    state.draw_text_small = DrawTextSmallCallback;
    return state;
}

static void DrawStaticStage() {
    StageRenderState state = CurrentStageRenderState();
    StageRenderDrawStatic(&state);
}

static void DrawDynamicStage() {
    StageRenderState state = CurrentStageRenderState();
    StageRenderDrawDynamic(&state);
}

static void DrawStage() {
    StageRenderState state = CurrentStageRenderState();
    StageRenderDrawFrame(&state);
}

static void DrawTypeAArtTest() {
    StageRenderState state = CurrentStageRenderState();
    StageRenderDrawTypeAArtTest(&state, g_type_a_art_test);
}

static MainMenuColors CurrentMainMenuColors() {
    MainMenuColors colors;
    colors.fallback_bg = COL_BG;
    colors.title_red = 0x00cb4855;
    colors.title_text = COL_PLAYER;
    colors.selected = COL_MENU_CYAN;
    colors.inactive = COL_TEXT_DIM;
    return colors;
}

static PauseMenuColors CurrentPauseMenuColors() {
    PauseMenuColors colors;
    colors.text = COL_TEXT;
    colors.text_dim = COL_TEXT_DIM;
    colors.accent = COL_MENU_CYAN;
    return colors;
}

static void DrawCornerHintSegment(int key_x, int label_x, int y, const wchar_t* key, const wchar_t* label) {
    DrawTextUi(key_x + 1, y + 2, key, 18, 0x00070506, 1);
    DrawTextUi(key_x, y, key, 18, COL_MENU_CYAN, 1);
    DrawTextUi(label_x + 1, y + 2, label, 18, 0x00070506, 0);
    DrawTextUi(label_x, y, label, 18, COL_TEXT, 0);
}

static void DrawMainMenuControlHints() {
    const int y = FB_H - 42;
    DrawCornerHintSegment(FB_W - 276, FB_W - 226, y, L"↑↓", L"이동");
    DrawCornerHintSegment(FB_W - 136, FB_W - 108, y, L"X", L"선택");
}

static void DrawStageSelectControlHints() {
    const int y = FB_H - 42;
    DrawCornerHintSegment(FB_W - 418, FB_W - 368, y, L"←→", L"이동");
    DrawCornerHintSegment(FB_W - 274, FB_W - 246, y, L"X", L"진입");
    DrawCornerHintSegment(FB_W - 152, FB_W - 124, y, L"Z", L"뒤로");
}

static StageCacheState CurrentStageCacheState() {
    StageCacheState state;
    state.render = &g_render;
    state.room = CurrentRoom();
    state.player = &g_game.player;
    state.player_particles = g_game.player_particles;
    state.player_particle_count = PLAYER_PARTICLE_COUNT;
    state.gravity_boxes = g_game.gravity_boxes;
    state.gravity_box_count = CurrentRoom()->gravity_box_count;
    if (state.gravity_box_count > GAME_MAX_GRAVITY_BOXES) state.gravity_box_count = GAME_MAX_GRAVITY_BOXES;
    state.walker_enemies = g_game.walker_enemies;
    state.walker_enemy_count = CurrentRoom()->walker_enemy_count;
    if (state.walker_enemy_count > GAME_MAX_WALKER_ENEMIES) state.walker_enemy_count = GAME_MAX_WALKER_ENEMIES;
    state.walker_enemy_spike_amount = g_game.walker_enemy_spike_amount;
    state.walker_enemy_squash_amount = g_game.walker_enemy_squash_amount;
    state.walker_enemy_turn_squash = g_game.walker_enemy_turn_squash;
    state.current_room = g_game.current_room;
    state.type_a_active = FeatureActive(FEATURE_COLLISION_TYPE_A);
    state.type_a_highlighted = SettingsHighlightsTypeA();
    state.tutorial_hint_visible = TutorialUiWorldHintVisible();
    state.type_a_bump_visible = TypeABumpVisible();
    state.type_a_setting_feedback_visible = TypeASettingFeedbackVisible();
    state.settings_overlay_visible = SettingsUiOverlayVisible();
    state.settings_dirty = SettingsUiIsDirty();
    state.room_solved = ExitSequenceRoomSolved();
    state.transition_visible = ExitSequenceTransitionVisible();
    state.player_dead = g_game.player_dead;
    state.camera_x = g_game.camera.x;
    state.camera_y = g_game.camera.y;
    state.piston_time_seconds = g_game.piston_time_seconds;
    state.bg_color = COL_BG;
    return state;
}

static constexpr float STAGE_SELECT_CENTER_X = FB_W * 0.5f;
static constexpr float STAGE_SELECT_PLAYER_W = 40.0f;
static constexpr float STAGE_SELECT_PLAYER_H = 40.0f;
static constexpr int STAGE_SELECT_DEFAULT_INDEX = 2;
static constexpr float STAGE_SELECT_MOVE_SECONDS = 0.36f;
static constexpr float STAGE_SELECT_JUMP_ARC_HEIGHT = 220.0f;
static constexpr float STAGE_SELECT_PLATFORM_GROW_SECONDS = 0.22f;
static constexpr float STAGE_SELECT_LAND_INPUT_DELAY_SECONDS = 0.12f;

static int StageSelectStageImplemented(int stage_index) {
    return stage_index >= 0 && stage_index < DevelopedRoomCount();
}

static int StageSelectStageCleared(int stage_index) {
    return stage_index >= 0 &&
           stage_index < STAGE_SELECT_DISPLAY_COUNT &&
           g_stage_select_cleared[stage_index];
}

static int StageSelectPreviousStagesCleared(int stage_index) {
    for (int i = 0; i < stage_index; ++i) {
        if (!StageSelectStageCleared(i)) {
            return 0;
        }
    }
    return 1;
}

static int StageSelectStageUnlocked(int stage_index) {
    // Developer build: keep every implemented stage open while iterating on room order.
    return StageSelectStageImplemented(stage_index);

    // if (stage_index == START_ROOM_INDEX) {
    //     return StageSelectStageImplemented(stage_index);
    // }
    // return StageSelectStageImplemented(stage_index) && StageSelectPreviousStagesCleared(stage_index);
}

static int StageSelectStageSelectable(int stage_index) {
    return StageSelectStageImplemented(stage_index) && StageSelectStageUnlocked(stage_index);
}

static int StageSelectClampInt(int value, int min_value, int max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

static float StageSelectClamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float StageSelectAbsF(float value) {
    return value < 0.0f ? -value : value;
}

static float StageSelectEase(float value) {
    value = StageSelectClamp01(value);
    return value * value * value * (value * (value * 6.0f - 15.0f) + 10.0f);
}

static float StageSelectLerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static float StageSelectFollow(float current, float target, float dt, float rate) {
    return StageSelectLerp(current, target, StageSelectEase(dt * rate));
}

static uint32_t StageSelectRgb(int r, int g, int b) {
    r = StageSelectClampInt(r, 0, 255);
    g = StageSelectClampInt(g, 0, 255);
    b = StageSelectClampInt(b, 0, 255);
    return (uint32_t)((r << 16) | (g << 8) | b);
}

static uint32_t StageSelectBrighten(uint32_t color, int amount) {
    int r = (int)((color >> 16) & 255) + amount;
    int g = (int)((color >> 8) & 255) + amount;
    int b = (int)(color & 255) + amount;
    return StageSelectRgb(r, g, b);
}

static uint32_t StageSelectDim(uint32_t color, int amount) {
    int r = (int)((color >> 16) & 255) - amount;
    int g = (int)((color >> 8) & 255) - amount;
    int b = (int)(color & 255) - amount;
    return StageSelectRgb(r, g, b);
}

static float StageSelectStageGap(int edge_index) {
    static const float gaps[] = { 430.0f, 468.0f, 406.0f, 448.0f, 396.0f, 458.0f };
    int count = (int)(sizeof(gaps) / sizeof(gaps[0]));
    int index = edge_index % count;
    if (index < 0) index += count;
    return gaps[index];
}

static float StageSelectWorldX(int stage_index) {
    float x = STAGE_SELECT_CENTER_X;
    if (stage_index > STAGE_SELECT_DEFAULT_INDEX) {
        for (int i = STAGE_SELECT_DEFAULT_INDEX; i < stage_index; ++i) {
            x += StageSelectStageGap(i);
        }
    } else if (stage_index < STAGE_SELECT_DEFAULT_INDEX) {
        for (int i = stage_index; i < STAGE_SELECT_DEFAULT_INDEX; ++i) {
            x -= StageSelectStageGap(i);
        }
    }
    return x;
}

static float StageSelectTargetOffset(int stage_index) {
    return STAGE_SELECT_CENTER_X - StageSelectWorldX(stage_index);
}

static int StageSelectStageDataIndex(int stage_index) {
    static const int count = 5;
    int index = stage_index % count;
    return index < 0 ? index + count : index;
}

static int StageSelectPlatformBaseWidth(int stage_index) {
    (void)stage_index;
    return 300;
}

static int StageSelectPlatformY(int stage_index) {
    static const int platform_y_by_stage[] = { 780, 622, 648, 622, 780 };
    return platform_y_by_stage[StageSelectStageDataIndex(stage_index)];
}

static float StageSelectPlayerYForStage(int stage_index) {
    return (float)StageSelectPlatformY(stage_index) - STAGE_SELECT_PLAYER_H;
}
static void StageSelectBeginMoveTo(int stage_index) {
    g_stage_select_start_offset = g_stage_select_world_offset;
    g_stage_select_start_player_y = g_stage_select_player_y;
    g_stage_select_target_offset = StageSelectTargetOffset(stage_index);
    g_stage_select_target_player_y = StageSelectPlayerYForStage(stage_index);
    g_stage_select_player_move_dir = g_stage_select_target_offset < g_stage_select_start_offset ? 1.0f : -1.0f;
    g_stage_select_player_visual_sx = 1.14f;
    g_stage_select_player_visual_sy = 0.84f;
    g_stage_select_anim_seconds = 0.0f;
    g_stage_select_platform_grow_seconds = 0.0f;
}
static int StageSelectScreenX(float world_x, float parallax) {
    float x = world_x + g_stage_select_world_offset * parallax;
    return x >= 0.0f ? (int)(x + 0.5f) : (int)(x - 0.5f);
}

static void StageSelectDrawPoly4(int x0, int y0, int x1, int y1,
                                 int x2, int y2, int x3, int y3,
                                 uint32_t color) {
    int xs[4] = { x0, x1, x2, x3 };
    int ys[4] = { y0, y1, y2, y3 };
    int min_y = ys[0];
    int max_y = ys[0];
    for (int i = 1; i < 4; ++i) {
        if (ys[i] < min_y) min_y = ys[i];
        if (ys[i] > max_y) max_y = ys[i];
    }

    min_y = StageSelectClampInt(min_y, 0, FB_H - 1);
    max_y = StageSelectClampInt(max_y, 0, FB_H - 1);
    for (int y = min_y; y <= max_y; ++y) {
        float scan_y = (float)y + 0.5f;
        float hits[4];
        int hit_count = 0;
        for (int i = 0; i < 4; ++i) {
            int j = (i + 1) & 3;
            float ay = (float)ys[i];
            float by = (float)ys[j];
            if ((ay <= scan_y && by > scan_y) || (by <= scan_y && ay > scan_y)) {
                float t = (scan_y - ay) / (by - ay);
                hits[hit_count++] = (float)xs[i] + ((float)xs[j] - (float)xs[i]) * t;
            }
        }

        if (hit_count >= 2) {
            if (hits[0] > hits[1]) {
                float tmp = hits[0];
                hits[0] = hits[1];
                hits[1] = tmp;
            }
            int draw_x0 = StageSelectClampInt((int)(hits[0] + 0.5f), 0, FB_W);
            int draw_x1 = StageSelectClampInt((int)(hits[1] + 0.5f), 0, FB_W);
            if (draw_x1 > draw_x0) {
                DrawRect(&g_render, draw_x0, y, draw_x1 - draw_x0, 1, color);
            }
        }
    }
}

static uint32_t StageSelectBlend(uint32_t src, uint32_t dst, int alpha) {
    alpha = StageSelectClampInt(alpha, 0, 255);
    int inv = 255 - alpha;
    int sr = (int)((src >> 16) & 255);
    int sg = (int)((src >> 8) & 255);
    int sb = (int)(src & 255);
    int dr = (int)((dst >> 16) & 255);
    int dg = (int)((dst >> 8) & 255);
    int db = (int)(dst & 255);
    return StageSelectRgb((sr * alpha + dr * inv) / 255,
                          (sg * alpha + dg * inv) / 255,
                          (sb * alpha + db * inv) / 255);
}

static void StageSelectDrawBgTriangle(float parallax,
                                      int x0, int y0,
                                      int x1, int y1,
                                      int x2, int y2,
                                      uint32_t color,
                                      int alpha) {
    uint32_t blended = StageSelectBlend(color, 0x00100b0d, alpha);
    StageSelectDrawPoly4(StageSelectScreenX((float)x0, parallax), y0,
                         StageSelectScreenX((float)x1, parallax), y1,
                         StageSelectScreenX((float)x2, parallax), y2,
                         StageSelectScreenX((float)x2, parallax), y2,
                         blended);
}

static void StageSelectDrawBgQuad(float parallax,
                                  int x0, int y0,
                                  int x1, int y1,
                                  int x2, int y2,
                                  int x3, int y3,
                                  uint32_t color,
                                  int alpha) {
    uint32_t blended = StageSelectBlend(color, 0x00100b0d, alpha);
    StageSelectDrawPoly4(StageSelectScreenX((float)x0, parallax), y0,
                         StageSelectScreenX((float)x1, parallax), y1,
                         StageSelectScreenX((float)x2, parallax), y2,
                         StageSelectScreenX((float)x3, parallax), y3,
                         blended);
}

static void StageSelectDrawBackground() {
    RenderClear(&g_render, 0x00100b0d);

    StageSelectDrawBgTriangle(0.02f, -180, 228, 430, 92, 520, 162, 0x00332328, 86);
    StageSelectDrawBgTriangle(0.035f, 1280, 204, 1670, 142, 1548, 354, 0x00422a30, 76);
    StageSelectDrawBgQuad(0.045f, 1740, 555, 2060, 506, 2160, 680, 1800, 754, 0x00332328, 82);
    StageSelectDrawBgTriangle(0.06f, 80, 900, 455, 762, 326, 1088, 0x00251a1d, 122);

    StageSelectDrawBgTriangle(0.20f, 1500, 12, 2380, -178, 2250, 84, 0x00cb4855, 112);
    StageSelectDrawBgQuad(0.38f, 1605, -218, 2390, -285, 2340, 64, 1760, 216, 0x00f04a5b, 219);

    StageSelectDrawBgTriangle(0.24f, -420, 1120, 260, 842, 500, 1180, 0x007b2b33, 143);
    StageSelectDrawBgQuad(0.52f, -560, 1260, 860, 982, 2360, 1168, 2360, 1420, 0x00cb4855, 199);
}
static void StageSelectDrawStageNode(int stage_index) {
    float world_x = StageSelectWorldX(stage_index);
    float screen_center_x = (float)StageSelectScreenX(world_x, 1.0f);
    int selected = stage_index == g_stage_select_index;
    int implemented = StageSelectStageImplemented(stage_index);
    int unlocked = StageSelectStageUnlocked(stage_index);
    int locked = !unlocked;
    int wip = !implemented;
    float selected_scale = selected ? StageSelectEase(g_stage_select_platform_grow_seconds / STAGE_SELECT_PLATFORM_GROW_SECONDS) : 0.0f;
    int selected_alpha = (int)(selected_scale * 255.0f + 0.5f);
    int platform_w = StageSelectPlatformBaseWidth(stage_index) + (int)(22.0f * selected_scale + 0.5f);
    int platform_h = 34 + (int)(8.0f * selected_scale + 0.5f);
    int platform_x = (int)(screen_center_x - (float)platform_w * 0.5f);
    int platform_y = StageSelectPlatformY(stage_index);
    uint32_t platform_base_color = wip ? 0x00342d34 : (locked ? 0x00513036 : 0x00cb4855);
    uint32_t platform_selected_color = locked ? StageSelectBrighten(platform_base_color, 10) : StageSelectBrighten(0x00f04a5b, 8);
    uint32_t platform_color = selected ? StageSelectBlend(platform_selected_color, platform_base_color, selected_alpha) : platform_base_color;

    DrawRect(&g_render, platform_x, platform_y, platform_w, platform_h, platform_color);
    if (locked) {
        DrawRect(&g_render, platform_x, platform_y + platform_h - 5, platform_w, 5, 0x00241b1f);
    }

    char label[16];
    wsprintfA(label, "STAGE %02d", stage_index);
    int text_scale = selected_scale > 0.0f ? 4 : 3;
    int text_w = 8 * 6 * text_scale;
    uint32_t text_base_color = locked ? 0x008a7774 : COL_TEXT;
    uint32_t text_color = selected ? StageSelectBlend(0x0039cfc3, text_base_color, selected_alpha) : text_base_color;
    int base_label_y = platform_y + platform_h + 18;
    int selected_label_y = platform_y + platform_h + 26;
    int label_y = selected ? (int)(StageSelectLerp((float)base_label_y, (float)selected_label_y, selected_scale) + 0.5f) : base_label_y;
    UiTextSmallDraw(&g_render, (int)(screen_center_x - text_w * 0.5f), label_y, label, text_scale, text_color);

    if (StageSelectStageCleared(stage_index)) {
        int cx = (int)(screen_center_x + text_w * 0.5f + 24);
        int cy = label_y + 15;
        DrawThickLine(&g_render, cx - 11, cy, cx - 3, cy + 8, 4, 0x0039cfc3);
        DrawThickLine(&g_render, cx - 3, cy + 8, cx + 13, cy - 10, 4, 0x0039cfc3);
    } else if (wip) {
        int icon_y = label_y + 38;
        FillDiamond(&g_render, (int)screen_center_x, icon_y + 12, 24, 20, 0x00645b5f);
        DrawRect(&g_render, (int)screen_center_x - 3, icon_y + 1, 6, 16, 0x00100b0d);
        DrawRect(&g_render, (int)screen_center_x - 3, icon_y + 20, 6, 5, 0x00100b0d);
        UiTextSmallDraw(&g_render, (int)screen_center_x - 36, icon_y + 36, "WIP", 3, 0x008a7774);
    } else if (locked) {
        int icon_x = (int)screen_center_x;
        int icon_y = label_y + 38;
        DrawRectOutline(&g_render, icon_x - 18, icon_y + 16, 36, 24, 0x008a7774);
        DrawRect(&g_render, icon_x - 10, icon_y + 8, 4, 12, 0x008a7774);
        DrawRect(&g_render, icon_x + 6, icon_y + 8, 4, 12, 0x008a7774);
        DrawRect(&g_render, icon_x - 10, icon_y + 8, 20, 4, 0x008a7774);
    }
}
static void StageSelectDrawPlayer() {
    Player player = {};
    player.x = STAGE_SELECT_CENTER_X - STAGE_SELECT_PLAYER_W * 0.5f;
    player.y = g_stage_select_player_y;
    player.collision_w = STAGE_SELECT_PLAYER_W;
    player.collision_h = STAGE_SELECT_PLAYER_H;
    int moving = g_stage_select_anim_seconds < STAGE_SELECT_MOVE_SECONDS;
    player.visual_sx = g_stage_select_player_visual_sx;
    player.visual_sy = g_stage_select_player_visual_sy;
    player.face_dir = g_stage_select_player_face_dir;
    player.grounded = moving ? 0 : 1;

    Camera old_camera = *g_render.camera;
    g_render.camera->x = 0.0f;
    g_render.camera->y = 0.0f;
    DrawPlayer(&g_render, &player, COL_PLAYER, StageSelectDim(COL_PLATFORM, 36), GRAVITY_DOWN);
    *g_render.camera = old_camera;
}

static void UpdateStageSelect() {
    double now = PerfNowSeconds();
    float dt = g_stage_select_last_seconds > 0.0 ? (float)(now - g_stage_select_last_seconds) : 0.016f;
    if (dt > 0.050f) dt = 0.050f;
    if (dt < 0.0f) dt = 0.0f;
    g_stage_select_last_seconds = now;
    if (InputWasPressed(KEY_X) || InputWasPressed(KEY_ESCAPE)) {
        StartAppTransition(APP_STATE_MAIN_MENU, g_stage_select_index);
        return;
    }
    g_stage_select_anim_seconds += dt;
    float move_t = StageSelectClamp01(g_stage_select_anim_seconds / STAGE_SELECT_MOVE_SECONDS);
    float t = StageSelectEase(move_t);
    float top_y = g_stage_select_start_player_y < g_stage_select_target_player_y ? g_stage_select_start_player_y : g_stage_select_target_player_y;
    float peak_y = top_y - STAGE_SELECT_JUMP_ARC_HEIGHT;
    float inv_t = 1.0f - move_t;
    g_stage_select_world_offset = StageSelectLerp(g_stage_select_start_offset, g_stage_select_target_offset, t);
    g_stage_select_player_y = inv_t * inv_t * g_stage_select_start_player_y +
                              2.0f * inv_t * move_t * peak_y +
                              move_t * move_t * g_stage_select_target_player_y;

    int moving = g_stage_select_anim_seconds < STAGE_SELECT_MOVE_SECONDS;
    float jump_arc = 4.0f * move_t * (1.0f - move_t);
    float target_face_dir = moving ? g_stage_select_player_move_dir : 0.0f;
    float target_visual_sx;
    float target_visual_sy;
    if (moving) {
        target_visual_sx = 0.96f - 0.06f * jump_arc;
        target_visual_sy = 1.08f + 0.14f * jump_arc;
    } else {
        float land_t = StageSelectClamp01(g_stage_select_platform_grow_seconds / STAGE_SELECT_PLATFORM_GROW_SECONDS);
        float land_squash = 1.0f - StageSelectEase(land_t);
        target_visual_sx = 1.0f + 0.24f * land_squash;
        target_visual_sy = 1.0f - 0.20f * land_squash;
    }
    g_stage_select_player_face_dir = StageSelectFollow(g_stage_select_player_face_dir, target_face_dir, dt, moving ? 13.0f : 7.0f);
    g_stage_select_player_visual_sx = StageSelectFollow(g_stage_select_player_visual_sx, target_visual_sx, dt, 15.0f);
    g_stage_select_player_visual_sy = StageSelectFollow(g_stage_select_player_visual_sy, target_visual_sy, dt, 15.0f);

    if (g_stage_select_anim_seconds >= STAGE_SELECT_MOVE_SECONDS) {
        g_stage_select_world_offset = g_stage_select_target_offset;
        g_stage_select_player_y = g_stage_select_target_player_y;
        g_stage_select_start_offset = g_stage_select_world_offset;
        g_stage_select_start_player_y = g_stage_select_player_y;
        g_stage_select_platform_grow_seconds += dt;
        if (g_stage_select_platform_grow_seconds > STAGE_SELECT_PLATFORM_GROW_SECONDS) {
            g_stage_select_platform_grow_seconds = STAGE_SELECT_PLATFORM_GROW_SECONDS;
        }
    } else {
        g_stage_select_platform_grow_seconds = 0.0f;
    }

    if (g_stage_select_anim_seconds < STAGE_SELECT_MOVE_SECONDS ||
        g_stage_select_platform_grow_seconds < STAGE_SELECT_LAND_INPUT_DELAY_SECONDS) {
        return;
    }

    int room_count = STAGE_SELECT_DISPLAY_COUNT;
    int previous_index = g_stage_select_index;
    int move_left = InputIsDown(KEY_LEFT) && !InputIsDown(KEY_RIGHT);
    int move_right = InputIsDown(KEY_RIGHT) && !InputIsDown(KEY_LEFT);
    if (move_left && g_stage_select_index > 0 && StageSelectStageSelectable(g_stage_select_index - 1)) {
        --g_stage_select_index;
    } else if (move_right && g_stage_select_index < room_count - 1 && StageSelectStageSelectable(g_stage_select_index + 1)) {
        ++g_stage_select_index;
    }
    if (g_stage_select_index != previous_index) {
        g_last_played_room = g_stage_select_index;
        StageProgressSave();
        StageSelectBeginMoveTo(g_stage_select_index);
        return;
    }

    if (InputWasPressed(KEY_Z) && StageSelectStageSelectable(g_stage_select_index)) {
        StartAppTransition(APP_STATE_GAME, g_stage_select_index);
        return;
    }
}
static void DrawStageSelect() {
    StageSelectDrawBackground();

    int room_count = STAGE_SELECT_DISPLAY_COUNT;
    for (int i = 0; i < room_count; ++i) {
        int sx = StageSelectScreenX(StageSelectWorldX(i), 1.0f);
        if (sx < -520 || sx > FB_W + 520) {
            continue;
        }
        StageSelectDrawStageNode(i);
    }
    StageSelectDrawPlayer();
    DrawStageSelectControlHints();
}
static void ToggleFullscreen() {
    DWORD style = GetWindowLongA(g_window, GWL_STYLE);
    if (!g_fullscreen) {
        MONITORINFO monitor = { sizeof(monitor) };
        if (GetWindowPlacement(g_window, &g_windowed_placement) &&
            GetMonitorInfoA(MonitorFromWindow(g_window, MONITOR_DEFAULTTOPRIMARY), &monitor)) {
            SetWindowLongA(g_window, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
            SetWindowPos(g_window, HWND_TOP,
                         monitor.rcMonitor.left, monitor.rcMonitor.top,
                         monitor.rcMonitor.right - monitor.rcMonitor.left,
                         monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            g_fullscreen = 1;
        }
    } else {
        SetWindowLongA(g_window, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(g_window, &g_windowed_placement);
        SetWindowPos(g_window, 0, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_fullscreen = 0;
    }
}

static void EnsureStaticCache() {
    StageCacheState state = CurrentStageCacheState();
    StageCacheEnsure(&state, DrawStaticStage);
}

static void DrawStageCached() {
    StageCacheState state = CurrentStageCacheState();
    StageCacheDrawCached(&state, DrawDynamicStage);
}

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

static HANDLE CreateFrameTimer() {
    HANDLE timer = CreateWaitableTimerExA(0, 0, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer) {
        timer = CreateWaitableTimerA(0, FALSE, 0);
    }
    return timer;
}

static void WaitUntilSeconds(HANDLE timer, double target_time) {
    double now = PerfNowSeconds();
    double remaining = target_time - now;
    if (remaining <= 0.0) {
        return;
    }
    if (timer) {
        LARGE_INTEGER due_time;
        due_time.QuadPart = -(LONGLONG)(remaining * 10000000.0);
        if (due_time.QuadPart > -1) {
            due_time.QuadPart = -1;
        }
        if (SetWaitableTimer(timer, &due_time, 0, 0, 0, FALSE)) {
            WaitForSingleObject(timer, INFINITE);
            return;
        }
    }
    DWORD sleep_ms = (DWORD)(remaining * 1000.0);
    if (sleep_ms > 0) {
        Sleep(sleep_ms);
    }
}

static void PerfAddDuration(double start, double end, double* total, double* max_value) {
    if (!g_perf_config.enabled) {
        return;
    }
    double ms = (end - start) * 1000.0;
    *total += ms;
    PerfMax(max_value, ms);
}

static void PaceFrame(HANDLE frame_timer, double* next_frame_time, double frame_start, double target_frame_seconds) {
    double t0 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
    if (g_perf_config.legacy_pacing) {
        Sleep(1);
    } else {
        *next_frame_time += target_frame_seconds;
        double after_work = PerfNowSeconds();
        if (*next_frame_time < after_work - target_frame_seconds) {
            *next_frame_time = after_work;
        }
        WaitUntilSeconds(frame_timer, *next_frame_time);
    }
    double t1 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
    if (g_perf_config.enabled) {
        PerfAddDuration(t0, t1, &g_perf_stats.sleep_ms, &g_perf_stats.max_sleep_ms);
        PerfAddFrame((t1 - frame_start) * 1000.0);
        if (g_perf_config.bench_frames > 0 && g_perf_stats.frames >= g_perf_config.bench_frames) {
            g_running = 0;
        }
    }
}

static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_CLOSE:
        case WM_DESTROY:
            StageProgressSaveCurrentPlace();
            g_running = 0;
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcA(window, message, wparam, lparam);
    }
}

extern "C" void WinMainCRTStartup() {
    SetProcessDPIAware();
    LoadPerfConfig();
    LoadVisualPalette();
    LoadBrickVisual();
    LoadArtTestMode();
    HINSTANCE instance = GetModuleHandleA(0);

    WNDCLASSA wc;
    ClearBytes(&wc, sizeof(wc));
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = "RECONFIG_STAGE01_WINDOW";
    wc.hCursor = LoadCursorA(0, IDC_ARROW);
    wc.hbrBackground = 0;
    RegisterClassA(&wc);

    RECT rect = { 0, 0, WIN_W, WIN_H };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    g_window = CreateWindowExA(
        0,
        wc.lpszClassName,
        "RE:CONFIG",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        0,
        0,
        instance,
        0);

    FramebufferInit(g_window, &g_render);
    UiTextInit(g_window, &g_render, g_perf_config.font_mode, g_perf_config.text_quality, PerfBucketAddTextCall);
    UiTextEnsureSurface();
    UiTextWarmSettingsFonts();
    UiTextWarmSettingsTextCache();
    SettingsUiInit(&g_render, FeatureActive, ToggleFeature, SetSettingsItemValue, DrawContextText);
    MainMenuInit(&g_main_menu);
    PauseMenuInit(&g_pause_menu);
    MainMenuLoadBackground();
    StageProgressLoad();

    g_game.camera.x = 0.0f;
    g_game.camera.y = 0.0f;
    g_game.current_room = StageProgressClampRoomIndex(g_last_played_room);
    g_last_played_room = g_game.current_room;
    ResetStage();
    SettingsUiColors settings_colors = CurrentSettingsColors();
    SettingsUiBuildMenuCache(&settings_colors);
    AudioStartBgm(SettingsUiBgmVolume());
    if (!g_perf_config.disable_static_cache) {
        EnsureStaticCache();
    }
    PerfBegin();

    double last = PerfNowSeconds();
    double next_frame_time = last;
    const double target_frame_seconds = 1.0 / 60.0;
    HANDLE frame_timer = CreateFrameTimer();
    while (g_running) {
        double frame_start = PerfNowSeconds();
        MSG msg;
        while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                g_running = 0;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        double t0 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
        InputBeginFrame();
        InputUpdate();
        double t1 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
        if (g_perf_config.enabled) {
            double ms = (t1 - t0) * 1000.0;
            g_perf_stats.input_ms += ms;
            PerfMax(&g_perf_stats.max_input_ms, ms);
        }

        float app_dt = 0.016f;
        if (g_app_transition_last_seconds > 0.0) {
            app_dt = (float)(frame_start - g_app_transition_last_seconds);
            if (app_dt > 0.050f) app_dt = 0.050f;
            if (app_dt < 0.0f) app_dt = 0.0f;
        }
        g_app_transition_last_seconds = frame_start;
        UpdateAppTransition(app_dt);
        AudioUpdateBgm(SettingsUiBgmVolume());
        PlayUiAudioForInput();

        if (InputWasPressed(KEY_F11)) {
            ToggleFullscreen();
        }
#ifndef NDEBUG
        if (InputWasPressed(KEY_0)) {
            DebugResetDataAndEnterMainMenu();
        }
        if (!AppTransitionActive() && InputWasPressed(KEY_1)) {
            StartAppTransition(APP_STATE_MAIN_MENU, g_game.current_room);
        }
        if (!AppTransitionActive() && InputWasPressed(KEY_2)) {
            StartAppTransition(APP_STATE_STAGE_SELECT, START_ROOM_INDEX);
        }
        if (!AppTransitionActive() && InputWasPressed(KEY_3)) {
            StartAppTransition(APP_STATE_GAME, DEBUG_ROOM_00_INDEX);
        }
        if (!AppTransitionActive() && InputWasPressed(KEY_4)) {
            StartAppTransition(APP_STATE_GAME, DEBUG_ROOM_01_INDEX);
        }
#endif

        if (g_type_a_art_test) {
            t0 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            DrawTypeAArtTest();
            t1 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            PerfAddDuration(t0, t1, &g_perf_stats.render_ms, &g_perf_stats.max_render_ms);

            t0 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            FramebufferDownsampleRenderTarget();
            t1 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            PerfAddDuration(t0, t1, &g_perf_stats.downsample_ms, &g_perf_stats.max_downsample_ms);

            t0 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            FramebufferPresent(COL_BG);
            t1 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            PerfAddDuration(t0, t1, &g_perf_stats.present_ms, &g_perf_stats.max_present_ms);
            PerfBucketAddPresent((t1 - t0) * 1000.0);

            PaceFrame(frame_timer, &next_frame_time, frame_start, target_frame_seconds);
            continue;
        }
        if (g_app_state == APP_STATE_MAIN_MENU) {
            t0 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            if (!AppTransitionActive()) {
                MainMenuUpdate(&g_main_menu);
                if (g_main_menu.action == MAIN_MENU_ACTION_START) {
                    StartAppTransition(APP_STATE_STAGE_SELECT, g_last_played_room);
                }
                if (g_main_menu.action == MAIN_MENU_ACTION_EXIT) {
                    g_running = 0;
                }
            }
            t1 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            PerfAddDuration(t0, t1, &g_perf_stats.update_ms, &g_perf_stats.max_update_ms);

            t0 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            MainMenuColors menu_colors = CurrentMainMenuColors();
            MainMenuDraw(&g_render, &g_main_menu, &menu_colors);
            DrawMainMenuControlHints();
            DrawAppTransition();
            t1 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            PerfAddDuration(t0, t1, &g_perf_stats.render_ms, &g_perf_stats.max_render_ms);

            t0 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            FramebufferDownsampleRenderTarget();
            t1 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            PerfAddDuration(t0, t1, &g_perf_stats.downsample_ms, &g_perf_stats.max_downsample_ms);

            t0 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            FramebufferPresent(COL_BG);
            t1 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            PerfAddDuration(t0, t1, &g_perf_stats.present_ms, &g_perf_stats.max_present_ms);
            PerfBucketAddPresent((t1 - t0) * 1000.0);

            PaceFrame(frame_timer, &next_frame_time, frame_start, target_frame_seconds);
            continue;
        }
        if (g_app_state == APP_STATE_STAGE_SELECT) {
            t0 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            if (!AppTransitionActive()) {
                UpdateStageSelect();
            }
            t1 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            PerfAddDuration(t0, t1, &g_perf_stats.update_ms, &g_perf_stats.max_update_ms);

            t0 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            DrawStageSelect();
            DrawAppTransition();
            t1 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            PerfAddDuration(t0, t1, &g_perf_stats.render_ms, &g_perf_stats.max_render_ms);

            t0 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            FramebufferDownsampleRenderTarget();
            t1 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            PerfAddDuration(t0, t1, &g_perf_stats.downsample_ms, &g_perf_stats.max_downsample_ms);

            t0 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            FramebufferPresent(COL_BG);
            t1 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            PerfAddDuration(t0, t1, &g_perf_stats.present_ms, &g_perf_stats.max_present_ms);
            PerfBucketAddPresent((t1 - t0) * 1000.0);

            PaceFrame(frame_timer, &next_frame_time, frame_start, target_frame_seconds);
            continue;
        }

        if (!AppTransitionActive() && !PauseMenuIsOpen(&g_pause_menu) && g_perf_config.settings_bench) {
            int f = g_perf_stats.frames;
            if (f == 30 || f == 130 || f == 230) {
                SettingsUiOpen(!g_perf_config.disable_static_cache);
            }
            if (f == 70 || f == 170 || f == 270) {
                SettingsUiClose();
            }
        }

        float dt = (float)(frame_start - last);
        last = frame_start;
        if (dt > 0.033f) dt = 0.033f;
        if (dt < 0.0f) dt = 0.0f;

        t0 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
        if (!AppTransitionActive()) {
            if (PauseMenuIsOpen(&g_pause_menu)) {
                PauseMenuUpdate(&g_pause_menu);
                PauseMenuAction pause_action = g_pause_menu.action;
                if (pause_action == PAUSE_MENU_ACTION_RESUME) {
                    PauseMenuClose(&g_pause_menu);
                } else if (pause_action == PAUSE_MENU_ACTION_RESTART) {
                    PauseMenuClose(&g_pause_menu);
                    ResetStage();
                } else if (pause_action == PAUSE_MENU_ACTION_STAGE_SELECT) {
                    PauseMenuClose(&g_pause_menu);
                    StartAppTransition(APP_STATE_STAGE_SELECT, g_game.current_room);
                }
                AudioUpdateSpeaker(0.0, 0.0f, 0);
                g_perf_frame_settings_anim = 0;
                g_perf_frame_tutorial_fade = 0;
            } else if (InputWasPressed(KEY_ESCAPE) && !SettingsUiOverlayVisible()) {
                PauseMenuOpen(&g_pause_menu);
                g_pause_redraw_pending = 1;
                AudioUpdateSpeaker(0.0, 0.0f, 0);
                g_perf_frame_settings_anim = 0;
                g_perf_frame_tutorial_fade = 0;
            } else {
                UpdateStage(dt);
                g_perf_frame_settings_anim = SettingsUiAnimationActive();
                g_perf_frame_tutorial_fade = TutorialUiFadeActive(g_game.current_room);
            }
        } else {
            g_perf_frame_settings_anim = 0;
            g_perf_frame_tutorial_fade = 0;
        }
        t1 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
        if (g_perf_config.enabled) {
            double ms = (t1 - t0) * 1000.0;
            g_perf_stats.update_ms += ms;
            PerfMax(&g_perf_stats.max_update_ms, ms);
        }

        int overlay_visible = SettingsUiOverlayVisible();
        int pause_visible = PauseMenuIsOpen(&g_pause_menu);
        if (overlay_visible) {
            g_overlay_redraw_pending = 1;
        }
        if (pause_visible) {
            g_pause_redraw_pending = 1;
        }
        int force_full_redraw = AppTransitionActive() ||
                                overlay_visible ||
                                pause_visible ||
                                g_overlay_redraw_pending ||
                                g_pause_redraw_pending ||
                                !FeatureActive(FEATURE_COLLISION_TYPE_A);

        if (g_perf_config.disable_static_cache || force_full_redraw) {
            t0 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            DrawStage();
            if (PauseMenuIsOpen(&g_pause_menu)) {
                PauseMenuColors pause_colors = CurrentPauseMenuColors();
                PauseMenuDraw(&g_render, &g_pause_menu, &pause_colors);
            }
            DrawAppTransition();
            t1 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            if (g_perf_config.enabled) {
                double ms = (t1 - t0) * 1000.0;
                g_perf_stats.render_ms += ms;
                PerfMax(&g_perf_stats.max_render_ms, ms);
            }

            t0 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            FramebufferDownsampleRenderTarget();
            t1 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            if (g_perf_config.enabled) {
                double ms = (t1 - t0) * 1000.0;
                g_perf_stats.downsample_ms += ms;
                PerfMax(&g_perf_stats.max_downsample_ms, ms);
            }
            if (!overlay_visible) {
                g_overlay_redraw_pending = 0;
            }
            if (!PauseMenuIsOpen(&g_pause_menu)) {
                g_pause_redraw_pending = 0;
            }
        } else {
            t0 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            EnsureStaticCache();
            t1 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            if (g_perf_config.enabled) {
                double ms = (t1 - t0) * 1000.0;
                g_perf_stats.static_ms += ms;
                PerfMax(&g_perf_stats.max_static_ms, ms);
            }
            DrawStageCached();
        }

        t0 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
        FramebufferPresent(COL_BG);
        t1 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
        if (g_perf_config.enabled) {
            double ms = (t1 - t0) * 1000.0;
            g_perf_stats.present_ms += ms;
            PerfMax(&g_perf_stats.max_present_ms, ms);
            PerfBucketAddPresent(ms);
        }

        PaceFrame(frame_timer, &next_frame_time, frame_start, target_frame_seconds);
    }

    if (frame_timer) {
        CloseHandle(frame_timer);
    }
    AudioShutdown();
    PerfWriteReport();
    ExitProcess(0);
}
