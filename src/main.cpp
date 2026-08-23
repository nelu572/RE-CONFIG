#include <stdint.h>
#include <stddef.h>
#include <windows.h>

#include "game_config.h"
#include "audio.h"
#include "perf.h"
#include "exit_sequence.h"
#include "framebuffer.h"
#include "input.h"
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
static int g_overlay_redraw_pending = 0;
static constexpr int START_ROOM_INDEX = 4;

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
}

static void UpdateStage(float dt) {
    GameUpdateStage(&g_game, dt, !g_perf_config.disable_static_cache);
    const RoomDef* room = CurrentRoom();
    AudioUpdateSpeaker(PerfNowSeconds() - g_game.room_started_at_seconds,
                       SettingsUiAudioVolume(),
                       room && room->speaker_count > 0);
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
    state.type_a_off_line_thickness = g_type_a_off_line_thickness;
    state.type_a_off_visible_path_len = g_type_a_off_visible_path_len;
    state.effect_color = COL_EFFECT;
    state.text_color = COL_TEXT;
    state.text_dim_color = COL_TEXT_DIM;
    state.type_a_active = FeatureActive(FEATURE_COLLISION_TYPE_A);
    state.speaker_volume = SettingsUiAudioVolume();
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

static StageCacheState CurrentStageCacheState() {
    StageCacheState state;
    state.render = &g_render;
    state.room = CurrentRoom();
    state.player = &g_game.player;
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
    state.bg_color = COL_BG;
    return state;
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

static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_CLOSE:
        case WM_DESTROY:
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

    g_game.camera.x = 0.0f;
    g_game.camera.y = 0.0f;
    g_game.current_room = START_ROOM_INDEX;
    ResetStage();
    SettingsUiColors settings_colors = CurrentSettingsColors();
    SettingsUiBuildMenuCache(&settings_colors);
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

        if (InputWasPressed(KEY_F11)) {
            ToggleFullscreen();
        }

        if (g_type_a_art_test) {
            DrawTypeAArtTest();
            FramebufferDownsampleRenderTarget();
            FramebufferPresent(COL_BG);
            WaitUntilSeconds(frame_timer, PerfNowSeconds() + target_frame_seconds);
            if (g_perf_config.enabled) {
                PerfAddFrame(target_frame_seconds * 1000.0);
                if (g_perf_config.bench_frames > 0 && g_perf_stats.frames >= g_perf_config.bench_frames) {
                    g_running = 0;
                }
            }
            continue;
        }

        if (g_perf_config.settings_bench) {
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
        UpdateStage(dt);
        g_perf_frame_settings_anim = SettingsUiAnimationActive();
        g_perf_frame_tutorial_fade = TutorialUiFadeActive(g_game.current_room);
        t1 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
        if (g_perf_config.enabled) {
            double ms = (t1 - t0) * 1000.0;
            g_perf_stats.update_ms += ms;
            PerfMax(&g_perf_stats.max_update_ms, ms);
        }

        int overlay_visible = SettingsUiOverlayVisible();
        if (overlay_visible) {
            g_overlay_redraw_pending = 1;
        }
        int force_full_redraw = overlay_visible ||
                                g_overlay_redraw_pending ||
                                !FeatureActive(FEATURE_COLLISION_TYPE_A);

        if (g_perf_config.disable_static_cache || force_full_redraw) {
            t0 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
            DrawStage();
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

        t0 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
        if (g_perf_config.legacy_pacing) {
            Sleep(1);
        } else {
            next_frame_time += target_frame_seconds;
            double after_work = PerfNowSeconds();
            if (next_frame_time < after_work - target_frame_seconds) {
                next_frame_time = after_work;
            }
            WaitUntilSeconds(frame_timer, next_frame_time);
        }
        t1 = g_perf_config.enabled ? PerfNowSeconds() : 0.0;
        if (g_perf_config.enabled) {
            double ms = (t1 - t0) * 1000.0;
            g_perf_stats.sleep_ms += ms;
            PerfMax(&g_perf_stats.max_sleep_ms, ms);
            PerfAddFrame((t1 - frame_start) * 1000.0);
            if (g_perf_config.bench_frames > 0 && g_perf_stats.frames >= g_perf_config.bench_frames) {
                g_running = 0;
            }
        }
    }

    if (frame_timer) {
        CloseHandle(frame_timer);
    }
    AudioShutdown();
    PerfWriteReport();
    ExitProcess(0);
}
