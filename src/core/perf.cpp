#include "perf.h"

#include <stddef.h>
#include <windows.h>

#include "game_config.h"

PerfConfig g_perf_config;
PerfStats g_perf_stats;
int g_perf_frame_settings_anim = 0;
int g_perf_frame_tutorial_fade = 0;

static double g_perf_sorted_frame_ms[2048];
static double g_perf_sorted_bucket_ms[1024];
static char g_perf_report_buffer[12000];

static void PerfClearBytes(void* dest, size_t count) {
    volatile unsigned char* out = (volatile unsigned char*)dest;
    for (size_t i = 0; i < count; ++i) {
        out[i] = 0;
    }
}

double PerfNowSeconds() {
    static LARGE_INTEGER freq = {};
    LARGE_INTEGER counter;
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
    }
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
}

static int ParsePositiveInt(const char* text) {
    int value = 0;
    for (const char* p = text; *p; ++p) {
        if (*p < '0' || *p > '9') {
            break;
        }
        value = value * 10 + (*p - '0');
    }
    return value;
}

static int EnvInt(const char* name, int default_value) {
    char buffer[32];
    DWORD len = GetEnvironmentVariableA(name, buffer, (DWORD)sizeof(buffer));
    if (len == 0 || len >= sizeof(buffer)) {
        return default_value;
    }
    return ParsePositiveInt(buffer);
}

void LoadPerfConfig() {
    g_perf_config.bench_frames = EnvInt("RECONFIG_BENCH_FRAMES", 0);
    g_perf_config.settings_bench = EnvInt("RECONFIG_SETTINGS_BENCH", 0) != 0;
    g_perf_config.enabled = g_perf_config.bench_frames > 0 || g_perf_config.settings_bench || EnvInt("RECONFIG_PERF", 0) != 0;
    g_perf_config.disable_static_cache = EnvInt("RECONFIG_DISABLE_STATIC_CACHE", 0) != 0;
    g_perf_config.legacy_pacing = EnvInt("RECONFIG_LEGACY_PACING", 0) != 0;
    g_perf_config.font_mode = EnvInt("RECONFIG_FONT_MODE", 5);
    g_perf_config.text_quality = EnvInt("RECONFIG_TEXT_QUALITY", 0);
}

static double ProcessCpuMilliseconds() {
    FILETIME create_time;
    FILETIME exit_time;
    FILETIME kernel_time;
    FILETIME user_time;
    GetProcessTimes(GetCurrentProcess(), &create_time, &exit_time, &kernel_time, &user_time);
    ULARGE_INTEGER kernel;
    ULARGE_INTEGER user;
    kernel.LowPart = kernel_time.dwLowDateTime;
    kernel.HighPart = kernel_time.dwHighDateTime;
    user.LowPart = user_time.dwLowDateTime;
    user.HighPart = user_time.dwHighDateTime;
    return (double)(kernel.QuadPart + user.QuadPart) / 10000.0;
}

void PerfMax(double* target, double value) {
    if (value > *target) {
        *target = value;
    }
}

void PerfBegin() {
    if (!g_perf_config.enabled) {
        return;
    }
    PerfClearBytes(&g_perf_stats, sizeof(g_perf_stats));
    g_perf_stats.cpu_start_ms = ProcessCpuMilliseconds();
    g_perf_stats.wall_start = PerfNowSeconds();
}

void PerfAddFrame(double frame_ms) {
    if (!g_perf_config.enabled) {
        return;
    }
    if (g_perf_stats.frames < (int)(sizeof(g_perf_stats.frame_ms) / sizeof(g_perf_stats.frame_ms[0]))) {
        g_perf_stats.frame_ms[g_perf_stats.frames] = frame_ms;
    }
    ++g_perf_stats.frames;

    PerfBucket* bucket = g_perf_frame_settings_anim ? &g_perf_stats.settings_anim :
                         (g_perf_frame_tutorial_fade ? &g_perf_stats.tutorial_fade : &g_perf_stats.normal);
    if (bucket->frames < (int)(sizeof(bucket->frame_ms) / sizeof(bucket->frame_ms[0]))) {
        bucket->frame_ms[bucket->frames] = frame_ms;
    }
    ++bucket->frames;
}

void PerfBucketAddPresent(double present_ms) {
    if (!g_perf_config.enabled) {
        return;
    }
    PerfBucket* bucket = g_perf_frame_settings_anim ? &g_perf_stats.settings_anim :
                         (g_perf_frame_tutorial_fade ? &g_perf_stats.tutorial_fade : &g_perf_stats.normal);
    bucket->present_ms += present_ms;
    PerfMax(&bucket->max_present_ms, present_ms);
}

void PerfBucketAddDirty(unsigned int area) {
    if (!g_perf_config.enabled) {
        return;
    }
    PerfBucket* bucket = g_perf_frame_settings_anim ? &g_perf_stats.settings_anim :
                         (g_perf_frame_tutorial_fade ? &g_perf_stats.tutorial_fade : &g_perf_stats.normal);
    bucket->dirty_area_sum += area;
    if (area > bucket->max_dirty_area) {
        bucket->max_dirty_area = area;
    }
    if (area >= (unsigned int)(FB_W * FB_H)) {
        ++bucket->full_dirty_frames;
    }
}

void PerfBucketAddTextCall(int cache_miss) {
    if (!g_perf_config.enabled) {
        return;
    }
    PerfBucket* bucket = g_perf_frame_settings_anim ? &g_perf_stats.settings_anim :
                         (g_perf_frame_tutorial_fade ? &g_perf_stats.tutorial_fade : &g_perf_stats.normal);
    ++bucket->text_draw_calls;
    if (cache_miss) {
        ++bucket->text_cache_misses;
    }
}

static void AppendText(char** out, const char* text) {
    while (*text) {
        **out = *text;
        ++(*out);
        ++text;
    }
}

static void AppendUInt(char** out, unsigned int value) {
    char temp[16];
    int count = 0;
    do {
        temp[count++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value);
    while (count > 0) {
        **out = temp[--count];
        ++(*out);
    }
}

static void AppendDouble3(char** out, double value) {
    if (value < 0.0) {
        **out = '-';
        ++(*out);
        value = -value;
    }
    unsigned int scaled = (unsigned int)(value * 1000.0 + 0.5);
    AppendUInt(out, scaled / 1000);
    **out = '.';
    ++(*out);
    unsigned int frac = scaled % 1000;
    **out = (char)('0' + (frac / 100));
    ++(*out);
    **out = (char)('0' + ((frac / 10) % 10));
    ++(*out);
    **out = (char)('0' + (frac % 10));
    ++(*out);
}

static void SortDoubles(double* values, int count) {
    for (int i = 1; i < count; ++i) {
        double v = values[i];
        int j = i - 1;
        while (j >= 0 && values[j] > v) {
            values[j + 1] = values[j];
            --j;
        }
        values[j + 1] = v;
    }
}

static void AppendMetric(char** out, const char* name, double avg, double max_value) {
    AppendText(out, name);
    AppendText(out, "_avg_ms=");
    AppendDouble3(out, avg);
    AppendText(out, "\r\n");
    AppendText(out, name);
    AppendText(out, "_max_ms=");
    AppendDouble3(out, max_value);
    AppendText(out, "\r\n");
}

static void AppendBucketReport(char** out, const char* name, const PerfBucket* bucket) {
    int stored_frames = bucket->frames;
    if (stored_frames > (int)(sizeof(bucket->frame_ms) / sizeof(bucket->frame_ms[0]))) {
        stored_frames = (int)(sizeof(bucket->frame_ms) / sizeof(bucket->frame_ms[0]));
    }
    double avg = 0.0;
    double max_value = 0.0;
    for (int i = 0; i < stored_frames; ++i) {
        double v = bucket->frame_ms[i];
        avg += v;
        if (v > max_value) max_value = v;
        g_perf_sorted_bucket_ms[i] = v;
    }
    if (stored_frames > 0) {
        avg /= (double)stored_frames;
        SortDoubles(g_perf_sorted_bucket_ms, stored_frames);
    }
    double p95 = stored_frames > 0 ? g_perf_sorted_bucket_ms[(stored_frames * 95) / 100] : 0.0;
    double p99 = stored_frames > 0 ? g_perf_sorted_bucket_ms[(stored_frames * 99) / 100] : 0.0;
    unsigned int avg_dirty = stored_frames > 0 ? bucket->dirty_area_sum / (unsigned int)stored_frames : 0;
    double avg_present = stored_frames > 0 ? bucket->present_ms / (double)stored_frames : 0.0;

    AppendText(out, name);
    AppendText(out, "_frames=");
    AppendUInt(out, (unsigned int)bucket->frames);
    AppendText(out, "\r\n");
    AppendText(out, name);
    AppendText(out, "_avg_frame_ms=");
    AppendDouble3(out, avg);
    AppendText(out, "\r\n");
    AppendText(out, name);
    AppendText(out, "_p95_frame_ms=");
    AppendDouble3(out, p95);
    AppendText(out, "\r\n");
    AppendText(out, name);
    AppendText(out, "_p99_frame_ms=");
    AppendDouble3(out, p99);
    AppendText(out, "\r\n");
    AppendText(out, name);
    AppendText(out, "_max_frame_ms=");
    AppendDouble3(out, max_value);
    AppendText(out, "\r\n");
    AppendText(out, name);
    AppendText(out, "_avg_dirty_pixels=");
    AppendUInt(out, avg_dirty);
    AppendText(out, "\r\n");
    AppendText(out, name);
    AppendText(out, "_max_dirty_pixels=");
    AppendUInt(out, bucket->max_dirty_area);
    AppendText(out, "\r\n");
    AppendText(out, name);
    AppendText(out, "_full_dirty_frames=");
    AppendUInt(out, (unsigned int)bucket->full_dirty_frames);
    AppendText(out, "\r\n");
    AppendText(out, name);
    AppendText(out, "_present_avg_ms=");
    AppendDouble3(out, avg_present);
    AppendText(out, "\r\n");
    AppendText(out, name);
    AppendText(out, "_present_max_ms=");
    AppendDouble3(out, bucket->max_present_ms);
    AppendText(out, "\r\n");
    AppendText(out, name);
    AppendText(out, "_text_draw_calls=");
    AppendUInt(out, (unsigned int)bucket->text_draw_calls);
    AppendText(out, "\r\n");
    AppendText(out, name);
    AppendText(out, "_text_cache_misses=");
    AppendUInt(out, (unsigned int)bucket->text_cache_misses);
    AppendText(out, "\r\n");
}

void PerfWriteReport() {
    if (!g_perf_config.enabled) {
        return;
    }
    g_perf_stats.cpu_end_ms = ProcessCpuMilliseconds();
    g_perf_stats.wall_end = PerfNowSeconds();

    int stored_frames = g_perf_stats.frames;
    if (stored_frames > (int)(sizeof(g_perf_stats.frame_ms) / sizeof(g_perf_stats.frame_ms[0]))) {
        stored_frames = (int)(sizeof(g_perf_stats.frame_ms) / sizeof(g_perf_stats.frame_ms[0]));
    }

    double avg = 0.0;
    double min_value = stored_frames > 0 ? g_perf_stats.frame_ms[0] : 0.0;
    double max_value = 0.0;
    int over_1667 = 0;
    int over_2000 = 0;
    for (int i = 0; i < stored_frames; ++i) {
        double v = g_perf_stats.frame_ms[i];
        avg += v;
        if (v < min_value) min_value = v;
        if (v > max_value) max_value = v;
        if (v > 16.667) ++over_1667;
        if (v > 20.0) ++over_2000;
    }
    if (stored_frames > 0) {
        avg /= (double)stored_frames;
    }

    for (int i = 0; i < stored_frames; ++i) {
        g_perf_sorted_frame_ms[i] = g_perf_stats.frame_ms[i];
    }
    SortDoubles(g_perf_sorted_frame_ms, stored_frames);
    double p95 = stored_frames > 0 ? g_perf_sorted_frame_ms[(stored_frames * 95) / 100] : 0.0;
    double p99 = stored_frames > 0 ? g_perf_sorted_frame_ms[(stored_frames * 99) / 100] : 0.0;
    double wall_ms = (g_perf_stats.wall_end - g_perf_stats.wall_start) * 1000.0;
    double cpu_ms = g_perf_stats.cpu_end_ms - g_perf_stats.cpu_start_ms;
    double cpu_percent = wall_ms > 0.0 ? (cpu_ms / wall_ms) * 100.0 : 0.0;
    double fps = avg > 0.0 ? 1000.0 / avg : 0.0;
    double divisor = stored_frames > 0 ? (double)stored_frames : 1.0;

    char* out = g_perf_report_buffer;
    AppendText(&out, "mode=");
    AppendText(&out, g_perf_config.disable_static_cache ? "full_redraw" : "static_cache");
    AppendText(&out, "\r\npacing=");
    AppendText(&out, g_perf_config.legacy_pacing ? "legacy_sleep_1" : "waitable_timer_60hz");
    AppendText(&out, "\r\nframes=");
    AppendUInt(&out, (unsigned int)g_perf_stats.frames);
    AppendText(&out, "\r\nstored_frames=");
    AppendUInt(&out, (unsigned int)stored_frames);
    AppendText(&out, "\r\navg_fps=");
    AppendDouble3(&out, fps);
    AppendText(&out, "\r\navg_frame_ms=");
    AppendDouble3(&out, avg);
    AppendText(&out, "\r\nmin_frame_ms=");
    AppendDouble3(&out, min_value);
    AppendText(&out, "\r\np95_frame_ms=");
    AppendDouble3(&out, p95);
    AppendText(&out, "\r\np99_frame_ms=");
    AppendDouble3(&out, p99);
    AppendText(&out, "\r\nmax_frame_ms=");
    AppendDouble3(&out, max_value);
    AppendText(&out, "\r\nover_16_667=");
    AppendUInt(&out, (unsigned int)over_1667);
    AppendText(&out, "\r\nover_20=");
    AppendUInt(&out, (unsigned int)over_2000);
    AppendText(&out, "\r\ncpu_single_core_percent=");
    AppendDouble3(&out, cpu_percent);
    AppendText(&out, "\r\nwall_ms=");
    AppendDouble3(&out, wall_ms);
    AppendText(&out, "\r\ncpu_ms=");
    AppendDouble3(&out, cpu_ms);
    AppendText(&out, "\r\n");
    AppendMetric(&out, "input", g_perf_stats.input_ms / divisor, g_perf_stats.max_input_ms);
    AppendMetric(&out, "update", g_perf_stats.update_ms / divisor, g_perf_stats.max_update_ms);
    AppendMetric(&out, "static", g_perf_stats.static_ms / divisor, g_perf_stats.max_static_ms);
    AppendMetric(&out, "restore", g_perf_stats.restore_ms / divisor, g_perf_stats.max_restore_ms);
    AppendMetric(&out, "render", g_perf_stats.render_ms / divisor, g_perf_stats.max_render_ms);
    AppendMetric(&out, "downsample", g_perf_stats.downsample_ms / divisor, g_perf_stats.max_downsample_ms);
    AppendMetric(&out, "present", g_perf_stats.present_ms / divisor, g_perf_stats.max_present_ms);
    AppendMetric(&out, "sleep", g_perf_stats.sleep_ms / divisor, g_perf_stats.max_sleep_ms);
    AppendBucketReport(&out, "normal", &g_perf_stats.normal);
    AppendBucketReport(&out, "settings_anim", &g_perf_stats.settings_anim);
    AppendBucketReport(&out, "tutorial_fade", &g_perf_stats.tutorial_fade);

    HANDLE file = CreateFileA("reconfig_perf.txt", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, g_perf_report_buffer, (DWORD)(out - g_perf_report_buffer), &written, 0);
        CloseHandle(file);
    }
}
