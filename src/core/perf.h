#pragma once

struct PerfConfig {
    int enabled;
    int bench_frames;
    int disable_static_cache;
    int legacy_pacing;
    int settings_bench;
    int font_mode;
    int text_quality;
};

struct PerfBucket {
    int frames;
    double frame_ms[1024];
    double present_ms;
    double max_present_ms;
    unsigned int dirty_area_sum;
    unsigned int max_dirty_area;
    int full_dirty_frames;
    int text_draw_calls;
    int text_cache_misses;
};

struct PerfStats {
    int frames;
    double frame_ms[2048];
    double cpu_start_ms;
    double cpu_end_ms;
    double wall_start;
    double wall_end;
    double input_ms;
    double update_ms;
    double static_ms;
    double restore_ms;
    double render_ms;
    double downsample_ms;
    double present_ms;
    double sleep_ms;
    double max_input_ms;
    double max_update_ms;
    double max_static_ms;
    double max_restore_ms;
    double max_render_ms;
    double max_downsample_ms;
    double max_present_ms;
    double max_sleep_ms;
    PerfBucket normal;
    PerfBucket settings_anim;
    PerfBucket tutorial_fade;
};

extern PerfConfig g_perf_config;
extern PerfStats g_perf_stats;
extern int g_perf_frame_settings_anim;
extern int g_perf_frame_tutorial_fade;

double PerfNowSeconds();
void LoadPerfConfig();
void PerfMax(double* target, double value);
void PerfBegin();
void PerfAddFrame(double frame_ms);
void PerfBucketAddPresent(double present_ms);
void PerfBucketAddDirty(unsigned int area);
void PerfBucketAddTextCall(int cache_miss);
void PerfWriteReport();
