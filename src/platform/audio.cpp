#include "audio.h"
#include "speaker_kick_embedded_wav.h"

#include <stdint.h>
#include <stddef.h>
#include <windows.h>
#include <mmsystem.h>

#ifndef STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_STDIO
#endif
#ifndef STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_PUSHDATA_API
#endif
#ifndef STB_VORBIS_MAX_CHANNELS
#define STB_VORBIS_MAX_CHANNELS 2
#endif
#ifndef STB_VORBIS_FAST_HUFFMAN_LENGTH
#define STB_VORBIS_FAST_HUFFMAN_LENGTH 7
#endif
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

extern "C" float ldexpf(float value, int exponent) {
    if (value == 0.0f) {
        return 0.0f;
    }
    float scale = exponent >= 0 ? 2.0f : 0.5f;
    unsigned int count = (unsigned int)(exponent >= 0 ? exponent : -exponent);
    while (count) {
        if (count & 1u) {
            value *= scale;
        }
        scale *= scale;
        count >>= 1;
    }
    return value;
}
static constexpr double SPEAKER_AUDIO_BPM = 130.0;
static constexpr double SPEAKER_AUDIO_BEAT_SECONDS = 60.0 / SPEAKER_AUDIO_BPM;
static constexpr int AUDIO_VOICE_COUNT = 4;
static constexpr int BGM_LOOP_FADE_MS = 8;
static constexpr int BGM_FADE_IN_MS = 1600;
static constexpr int BGM_FADE_OUT_MS = 240;
static constexpr float BGM_OUTPUT_GAIN = 0.50f;
static constexpr int BGM_TRANSITION_DUCK_MS = 620;
static constexpr float BGM_TRANSITION_DUCK_LEVEL = 0.45f;
static constexpr int TRANSITION_SAMPLE_RATE = 22050;
static constexpr int TRANSITION_SOUND_MS = 420;
static constexpr int TRANSITION_SAMPLE_COUNT = TRANSITION_SAMPLE_RATE * TRANSITION_SOUND_MS / 1000;
static constexpr float TRANSITION_OUTPUT_GAIN = 0.60f;
static constexpr DWORD BGM_LOOP_FOREVER = 0xFFFFFFFFu;

struct AudioWavData {
    WAVEFORMATEX format;
    unsigned char* samples;
    DWORD sample_bytes;
};

struct AudioBgmData {
    WAVEFORMATEX format;
    short* samples;
    DWORD sample_bytes;
    DWORD frame_count;
};

static AudioBgmData g_bgm;
static HWAVEOUT g_bgm_wave_out = 0;
static WAVEHDR g_bgm_header;
static int g_bgm_loaded = 0;
static int g_bgm_load_attempted = 0;
static int g_bgm_prepared = 0;
static int g_bgm_playing = 0;
static float g_bgm_volume = 1.0f;
static DWORD g_bgm_fade_start_ms = 0;
static int g_bgm_fading_in = 0;
static DWORD g_bgm_duck_start_ms = 0;
static int g_bgm_ducking = 0;

static HWAVEOUT g_transition_wave_out = 0;
static WAVEFORMATEX g_transition_format;
static WAVEHDR g_transition_header;
static short* g_transition_samples = 0;
static DWORD g_transition_sample_bytes = 0;
static int g_transition_loaded = 0;
static int g_transition_prepared = 0;

static HWAVEOUT g_sfx_wave_out = 0;
static WAVEFORMATEX g_sfx_format;
static WAVEHDR g_sfx_header;
struct AudioSfxData {
    short* samples;
    DWORD sample_bytes;
    int sample_count;
    int loaded;
    int load_attempted;
};

static AudioSfxData g_sfx_assets[7];
static int g_sfx_prepared = 0;
static AudioWavData g_speaker_kick;
static int g_speaker_kick_loaded = 0;
static int g_speaker_kick_load_attempted = 0;
static HWAVEOUT g_wave_out = 0;
static WAVEHDR g_voice_headers[AUDIO_VOICE_COUNT];
static unsigned char* g_voice_buffers[AUDIO_VOICE_COUNT];
static int g_voice_prepared[AUDIO_VOICE_COUNT];
static int g_next_voice = 0;
static int g_speaker_audio_last_beat = -1;
static int g_speaker_audio_was_active = 0;

static int AudioFileExists(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static int AudioFindSpeakerKickPath(char* out, int cap) {
    static const char* paths[] = {
        "assets\\audio\\speaker_kick_sample.wav",
        "..\\assets\\audio\\speaker_kick_sample.wav",
        "..\\..\\assets\\audio\\speaker_kick_sample.wav",
    };
    for (int i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); ++i) {
        if (AudioFileExists(paths[i])) {
            DWORD len = GetFullPathNameA(paths[i], (DWORD)cap, out, 0);
            return len > 0 && len < (DWORD)cap;
        }
    }
    return 0;
}

static uint32_t AudioReadU32(const unsigned char* p) {
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t AudioReadU16(const unsigned char* p) {
    return (uint16_t)(((uint16_t)p[0]) | ((uint16_t)p[1] << 8));
}

static int AudioFourCC(const unsigned char* p, char a, char b, char c, char d) {
    return p[0] == (unsigned char)a &&
           p[1] == (unsigned char)b &&
           p[2] == (unsigned char)c &&
           p[3] == (unsigned char)d;
}

static void AudioClearBytes(void* dest, size_t count) {
    unsigned char* out = (unsigned char*)dest;
    for (size_t i = 0; i < count; ++i) {
        out[i] = 0;
    }
}

static int AudioLoadFile(const char* path, unsigned char** data, DWORD* size) {
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
        return 0;
    }

    DWORD file_size = GetFileSize(file, 0);
    if (file_size == INVALID_FILE_SIZE || file_size < 44) {
        CloseHandle(file);
        return 0;
    }

    unsigned char* bytes = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, file_size);
    if (!bytes) {
        CloseHandle(file);
        return 0;
    }

    DWORD read = 0;
    int ok = ReadFile(file, bytes, file_size, &read, 0) && read == file_size;
    CloseHandle(file);
    if (!ok) {
        HeapFree(GetProcessHeap(), 0, bytes);
        return 0;
    }

    *data = bytes;
    *size = file_size;
    return 1;
}

static int AudioParseWav(const unsigned char* file, DWORD file_size, AudioWavData* wav) {
    if (!AudioFourCC(file, 'R', 'I', 'F', 'F') || !AudioFourCC(file + 8, 'W', 'A', 'V', 'E')) {
        return 0;
    }

    int found_fmt = 0;
    int found_data = 0;
    WAVEFORMATEX fmt;
    AudioClearBytes(&fmt, sizeof(fmt));
    const unsigned char* data_ptr = 0;
    DWORD data_size = 0;

    DWORD offset = 12;
    while (offset + 8 <= file_size) {
        const unsigned char* chunk = file + offset;
        DWORD chunk_size = AudioReadU32(chunk + 4);
        DWORD chunk_data = offset + 8;
        if (chunk_data > file_size || chunk_size > file_size - chunk_data) {
            return 0;
        }

        if (AudioFourCC(chunk, 'f', 'm', 't', ' ')) {
            if (chunk_size < 16) {
                return 0;
            }
            uint16_t audio_format = AudioReadU16(file + chunk_data);
            fmt.wFormatTag = audio_format;
            fmt.nChannels = AudioReadU16(file + chunk_data + 2);
            fmt.nSamplesPerSec = AudioReadU32(file + chunk_data + 4);
            fmt.nAvgBytesPerSec = AudioReadU32(file + chunk_data + 8);
            fmt.nBlockAlign = AudioReadU16(file + chunk_data + 12);
            fmt.wBitsPerSample = AudioReadU16(file + chunk_data + 14);
            fmt.cbSize = 0;
            found_fmt = 1;
        } else if (AudioFourCC(chunk, 'd', 'a', 't', 'a')) {
            data_ptr = file + chunk_data;
            data_size = chunk_size;
            found_data = 1;
        }

        offset = chunk_data + chunk_size + (chunk_size & 1);
    }

    if (!found_fmt || !found_data ||
        fmt.wFormatTag != WAVE_FORMAT_PCM ||
        fmt.wBitsPerSample != 16 ||
        fmt.nChannels < 1 ||
        fmt.nChannels > 2 ||
        data_size <= 0) {
        return 0;
    }

    unsigned char* samples = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, data_size);
    if (!samples) {
        return 0;
    }
    for (DWORD i = 0; i < data_size; ++i) {
        samples[i] = data_ptr[i];
    }

    wav->format = fmt;
    wav->samples = samples;
    wav->sample_bytes = data_size;
    return 1;
}

static int AudioLoadSpeakerKick() {
    if (g_speaker_kick_loaded) {
        return 1;
    }
    if (g_speaker_kick_load_attempted) {
        return 0;
    }
    g_speaker_kick_load_attempted = 1;

    if (AudioParseWav(kSpeakerKickEmbeddedWav, (DWORD)sizeof(kSpeakerKickEmbeddedWav), &g_speaker_kick)) {
        g_speaker_kick_loaded = 1;
        return 1;
    }

    char path[MAX_PATH];
    if (!AudioFindSpeakerKickPath(path, (int)sizeof(path))) {
        return 0;
    }

    unsigned char* file = 0;
    DWORD file_size = 0;
    if (!AudioLoadFile(path, &file, &file_size)) {
        return 0;
    }

    int ok = AudioParseWav(file, file_size, &g_speaker_kick);
    HeapFree(GetProcessHeap(), 0, file);
    if (!ok) {
        return 0;
    }

    g_speaker_kick_loaded = 1;
    return 1;
}

static int AudioStringLength(const char* s) {
    int len = 0;
    while (s && s[len]) {
        ++len;
    }
    return len;
}

static void AudioDebugString(const char* text) {
    OutputDebugStringA("[audio] ");
    OutputDebugStringA(text);
    OutputDebugStringA("\n");
}

static void AudioDebugValue(const char* prefix, DWORD value) {
    char msg[192];
    wsprintfA(msg, "%s%lu", prefix, (unsigned long)value);
    AudioDebugString(msg);
}


static int AudioAppendPath(char* out, int cap, const char* dir, const char* rel) {
    int dir_len = AudioStringLength(dir);
    int rel_len = AudioStringLength(rel);
    int need_slash = dir_len > 0 && dir[dir_len - 1] != '\\' && dir[dir_len - 1] != '/';
    int total = dir_len + need_slash + rel_len;
    if (!out || total >= cap) {
        if (out && cap > 0) out[0] = 0;
        return 0;
    }
    int at = 0;
    for (int i = 0; i < dir_len; ++i) out[at++] = dir[i];
    if (need_slash) out[at++] = '\\';
    for (int i = 0; i < rel_len; ++i) out[at++] = rel[i];
    out[at] = 0;
    return 1;
}

static int AudioExeDir(char* out, int cap) {
    if (!out || cap <= 0) {
        return 0;
    }
    DWORD len = GetModuleFileNameA(0, out, (DWORD)cap);
    if (len <= 0 || len >= (DWORD)cap) {
        out[0] = 0;
        return 0;
    }
    for (int i = (int)len - 1; i >= 0; --i) {
        if (out[i] == '\\' || out[i] == '/') {
            out[i] = 0;
            return 1;
        }
    }
    out[0] = 0;
    return 0;
}

static int AudioFindExeRelativePath(const char* rel, char* out, int cap) {
    char exe_dir[MAX_PATH];
    if (!AudioExeDir(exe_dir, (int)sizeof(exe_dir))) {
        return 0;
    }
    if (!AudioAppendPath(out, cap, exe_dir, rel)) {
        return 0;
    }
    return AudioFileExists(out);
}

static int AudioFindBgmPath(char* out, int cap) {
    static const char* paths[] = {
        "assets\\audio\\bgm_main.ogg",
        "..\\assets\\audio\\bgm_main.ogg",
        "..\\..\\assets\\audio\\bgm_main.ogg",
    };
    for (int i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); ++i) {
        if (AudioFindExeRelativePath(paths[i], out, cap)) {
            return 1;
        }
    }
    return 0;
}

static float AudioClampVolume(float volume) {
    if (volume < 0.0f) return 0.0f;
    if (volume > 1.0f) return 1.0f;
    return volume;
}

static DWORD AudioVolumeDword(float volume) {
    volume = AudioClampVolume(volume);
    DWORD word = (DWORD)(volume * 65535.0f + 0.5f);
    if (word > 65535u) word = 65535u;
    return word | (word << 16);
}

static float AudioBgmFadeAmount() {
    if (!g_bgm_fading_in) {
        return 1.0f;
    }
    DWORD elapsed = timeGetTime() - g_bgm_fade_start_ms;
    if (elapsed >= (DWORD)BGM_FADE_IN_MS) {
        g_bgm_fading_in = 0;
        return 1.0f;
    }
    return (float)elapsed / (float)BGM_FADE_IN_MS;
}

static float AudioBgmDuckAmount() {
    if (!g_bgm_ducking) {
        return 1.0f;
    }
    DWORD elapsed = timeGetTime() - g_bgm_duck_start_ms;
    if (elapsed >= (DWORD)BGM_TRANSITION_DUCK_MS) {
        g_bgm_ducking = 0;
        return 1.0f;
    }
    float phase = (float)elapsed / (float)BGM_TRANSITION_DUCK_MS;
    if (phase < 0.30f) {
        return BGM_TRANSITION_DUCK_LEVEL;
    }
    float recover = (phase - 0.30f) / 0.70f;
    return BGM_TRANSITION_DUCK_LEVEL + (1.0f - BGM_TRANSITION_DUCK_LEVEL) * recover;
}

static void AudioApplyBgmVolume() {
    if (g_bgm_wave_out) {
        float effective = g_bgm_volume * BGM_OUTPUT_GAIN * AudioBgmFadeAmount() * AudioBgmDuckAmount();
        waveOutSetVolume(g_bgm_wave_out, AudioVolumeDword(effective));
    }
}

static short AudioScaleFloatSample(float sample) {
    int value = (int)(sample + (sample >= 0.0f ? 0.5f : -0.5f));
    if (value < -32768) value = -32768;
    if (value > 32767) value = 32767;
    return (short)value;
}

static void AudioApplyBgmLoopFade(AudioBgmData* bgm) {
    if (!bgm || !bgm->samples || bgm->frame_count <= 1 || bgm->format.nChannels <= 0) {
        return;
    }
    DWORD fade_frames = (DWORD)((bgm->format.nSamplesPerSec * BGM_LOOP_FADE_MS) / 1000);
    if (fade_frames < 1) {
        fade_frames = 1;
    }
    if (fade_frames * 2 > bgm->frame_count) {
        fade_frames = bgm->frame_count / 2;
    }
    int channels = bgm->format.nChannels;
    for (DWORD i = 0; i < fade_frames; ++i) {
        float in_gain = (float)i / (float)fade_frames;
        float out_gain = (float)(fade_frames - 1 - i) / (float)fade_frames;
        DWORD start_frame = i;
        DWORD end_frame = bgm->frame_count - fade_frames + i;
        for (int ch = 0; ch < channels; ++ch) {
            short* start = bgm->samples + start_frame * channels + ch;
            short* end = bgm->samples + end_frame * channels + ch;
            *start = AudioScaleFloatSample((float)(*start) * in_gain);
            *end = AudioScaleFloatSample((float)(*end) * out_gain);
        }
    }
}

static int AudioDecodeBgmOgg(const char* path, AudioBgmData* bgm) {
    unsigned char* file = 0;
    DWORD file_size = 0;
    if (!AudioLoadFile(path, &file, &file_size)) {
        AudioDebugString("BGM load failed: could not read assets\\audio\\bgm_main.ogg");
        return 0;
    }

    int error = 0;
    stb_vorbis* vorbis = stb_vorbis_open_memory(file, (int)file_size, &error, 0);
    if (!vorbis) {
        HeapFree(GetProcessHeap(), 0, file);
        AudioDebugValue("BGM decode failed: stb_vorbis error ", (DWORD)error);
        return 0;
    }

    stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    if (info.channels < 1 || info.channels > 2 || info.sample_rate == 0) {
        stb_vorbis_close(vorbis);
        HeapFree(GetProcessHeap(), 0, file);
        AudioDebugString("BGM decode failed: unsupported channel count or sample rate");
        return 0;
    }

    unsigned int frame_count = stb_vorbis_stream_length_in_samples(vorbis);
    if (frame_count == 0) {
        stb_vorbis_close(vorbis);
        HeapFree(GetProcessHeap(), 0, file);
        AudioDebugString("BGM decode failed: empty stream");
        return 0;
    }

    unsigned long long total_shorts = (unsigned long long)frame_count * (unsigned long long)info.channels;
    unsigned long long total_bytes = total_shorts * sizeof(short);
    if (total_bytes > 0xFFFFFFFFull) {
        stb_vorbis_close(vorbis);
        HeapFree(GetProcessHeap(), 0, file);
        AudioDebugString("BGM decode failed: stream too large");
        return 0;
    }

    short* samples = (short*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)total_bytes);
    if (!samples) {
        stb_vorbis_close(vorbis);
        HeapFree(GetProcessHeap(), 0, file);
        AudioDebugString("BGM decode failed: out of memory");
        return 0;
    }

    unsigned int decoded_frames = 0;
    while (decoded_frames < frame_count) {
        int remaining_shorts = (int)((frame_count - decoded_frames) * (unsigned int)info.channels);
        int got = stb_vorbis_get_samples_short_interleaved(vorbis,
                                                           info.channels,
                                                           samples + decoded_frames * (unsigned int)info.channels,
                                                           remaining_shorts);
        if (got <= 0) {
            break;
        }
        decoded_frames += (unsigned int)got;
    }

    stb_vorbis_close(vorbis);
    HeapFree(GetProcessHeap(), 0, file);
    if (decoded_frames == 0) {
        HeapFree(GetProcessHeap(), 0, samples);
        AudioDebugString("BGM decode failed: no PCM frames decoded");
        return 0;
    }

    AudioClearBytes(&bgm->format, sizeof(bgm->format));
    bgm->format.wFormatTag = WAVE_FORMAT_PCM;
    bgm->format.nChannels = (WORD)info.channels;
    bgm->format.nSamplesPerSec = info.sample_rate;
    bgm->format.wBitsPerSample = 16;
    bgm->format.nBlockAlign = (WORD)(bgm->format.nChannels * (bgm->format.wBitsPerSample / 8));
    bgm->format.nAvgBytesPerSec = bgm->format.nSamplesPerSec * bgm->format.nBlockAlign;
    bgm->format.cbSize = 0;
    bgm->samples = samples;
    bgm->frame_count = decoded_frames;
    bgm->sample_bytes = (DWORD)((unsigned long long)decoded_frames * (unsigned long long)bgm->format.nBlockAlign);
    AudioApplyBgmLoopFade(bgm);
    return 1;
}

static int AudioLoadBgm() {
    if (g_bgm_loaded) {
        return 1;
    }
    if (g_bgm_load_attempted) {
        return 0;
    }
    g_bgm_load_attempted = 1;

    char path[MAX_PATH];
    if (!AudioFindBgmPath(path, (int)sizeof(path))) {
        AudioDebugString("BGM load failed: assets\\audio\\bgm_main.ogg not found near executable");
        return 0;
    }
    if (!AudioDecodeBgmOgg(path, &g_bgm)) {
        return 0;
    }
    g_bgm_loaded = 1;
    return 1;
}

void AudioSetBgmVolume(float volume) {
    g_bgm_volume = AudioClampVolume(volume);
    AudioApplyBgmVolume();
}

void AudioUpdateBgm(float volume) {
    g_bgm_volume = AudioClampVolume(volume);
    AudioApplyBgmVolume();
}

void AudioStartBgm(float volume) {
    AudioSetBgmVolume(volume);
    if (g_bgm_playing) {
        return;
    }
    if (!AudioLoadBgm()) {
        return;
    }
    MMRESULT mm = waveOutOpen(&g_bgm_wave_out, WAVE_MAPPER, &g_bgm.format, 0, 0, CALLBACK_NULL);
    if (mm != MMSYSERR_NOERROR) {
        g_bgm_wave_out = 0;
        AudioDebugValue("BGM waveOutOpen failed: ", (DWORD)mm);
        return;
    }
    g_bgm_fade_start_ms = timeGetTime();
    g_bgm_fading_in = 1;
    waveOutSetVolume(g_bgm_wave_out, AudioVolumeDword(0.0f));

    AudioClearBytes(&g_bgm_header, sizeof(g_bgm_header));
    g_bgm_header.lpData = (LPSTR)g_bgm.samples;
    g_bgm_header.dwBufferLength = g_bgm.sample_bytes;
    g_bgm_header.dwFlags = WHDR_BEGINLOOP | WHDR_ENDLOOP;
    g_bgm_header.dwLoops = BGM_LOOP_FOREVER;
    mm = waveOutPrepareHeader(g_bgm_wave_out, &g_bgm_header, sizeof(g_bgm_header));
    if (mm != MMSYSERR_NOERROR) {
        AudioDebugValue("BGM waveOutPrepareHeader failed: ", (DWORD)mm);
        waveOutClose(g_bgm_wave_out);
        g_bgm_wave_out = 0;
        return;
    }
    g_bgm_prepared = 1;

    mm = waveOutWrite(g_bgm_wave_out, &g_bgm_header, sizeof(g_bgm_header));
    if (mm != MMSYSERR_NOERROR) {
        AudioDebugValue("BGM waveOutWrite failed: ", (DWORD)mm);
        waveOutReset(g_bgm_wave_out);
        waveOutUnprepareHeader(g_bgm_wave_out, &g_bgm_header, sizeof(g_bgm_header));
        waveOutClose(g_bgm_wave_out);
        g_bgm_wave_out = 0;
        g_bgm_prepared = 0;
        return;
    }
    g_bgm_playing = 1;
}
static void AudioStopBgm() {
    if (!g_bgm_wave_out) {
        return;
    }
    if (g_bgm_playing) {
        const int steps = 12;
        for (int i = steps; i >= 0; --i) {
            float t = (float)i / (float)steps;
            waveOutSetVolume(g_bgm_wave_out, AudioVolumeDword(g_bgm_volume * BGM_OUTPUT_GAIN * t));
            Sleep(BGM_FADE_OUT_MS / steps);
        }
    }
    waveOutReset(g_bgm_wave_out);
    if (g_bgm_prepared) {
        waveOutUnprepareHeader(g_bgm_wave_out, &g_bgm_header, sizeof(g_bgm_header));
        g_bgm_prepared = 0;
    }
    g_bgm_playing = 0;
    g_bgm_fading_in = 0;
}
static short AudioClampI16(int value) {
    if (value < -32768) value = -32768;
    if (value > 32767) value = 32767;
    return (short)value;
}

static void AudioGenerateTransitionSound() {
    AudioClearBytes(&g_transition_format, sizeof(g_transition_format));
    g_transition_format.wFormatTag = WAVE_FORMAT_PCM;
    g_transition_format.nChannels = 1;
    g_transition_format.nSamplesPerSec = TRANSITION_SAMPLE_RATE;
    g_transition_format.wBitsPerSample = 16;
    g_transition_format.nBlockAlign = 2;
    g_transition_format.nAvgBytesPerSec = TRANSITION_SAMPLE_RATE * g_transition_format.nBlockAlign;
    g_transition_sample_bytes = TRANSITION_SAMPLE_COUNT * sizeof(short);

    g_transition_samples = (short*)HeapAlloc(GetProcessHeap(), 0, g_transition_sample_bytes);
    if (!g_transition_samples) {
        return;
    }

    unsigned int phase = 0;
    unsigned int noise = 0x144u;
    for (int i = 0; i < TRANSITION_SAMPLE_COUNT; ++i) {
        int attack = TRANSITION_SAMPLE_RATE / 100;
        int env = 1024;
        if (i < attack) {
            env = i * 1024 / attack;
        } else {
            int remain = TRANSITION_SAMPLE_COUNT - i;
            env = remain * 1024 / (TRANSITION_SAMPLE_COUNT - attack);
        }
        if (env < 0) env = 0;
        if (env > 1024) env = 1024;

        int sweep = 820 - (i * 420 / TRANSITION_SAMPLE_COUNT);
        phase += (unsigned int)((sweep * 65536) / TRANSITION_SAMPLE_RATE);
        int ramp = (int)((phase >> 8) & 255u);
        int tri = ramp < 128 ? ramp * 512 - 32768 : (255 - ramp) * 512 - 32768;

        noise = noise * 1664525u + 1013904223u;
        int hiss = ((int)((noise >> 16) & 65535u) - 32768) / 5;
        int hit = 0;
        if (i < TRANSITION_SAMPLE_RATE / 40) {
            hit = (int)((TRANSITION_SAMPLE_RATE / 40 - i) * 16000 / (TRANSITION_SAMPLE_RATE / 40));
        }
        int sample = ((tri / 3 + hiss) * env / 1024) + hit;
        g_transition_samples[i] = AudioClampI16(sample);
    }
    g_transition_loaded = 1;
}

static int AudioEnsureTransitionSound() {
    if (!g_transition_loaded) {
        AudioGenerateTransitionSound();
        if (!g_transition_loaded) {
            AudioDebugString("transition sound init failed: out of memory");
            return 0;
        }
    }
    if (g_transition_wave_out) {
        return 1;
    }
    MMRESULT mm = waveOutOpen(&g_transition_wave_out, WAVE_MAPPER, &g_transition_format, 0, 0, CALLBACK_NULL);
    if (mm != MMSYSERR_NOERROR) {
        g_transition_wave_out = 0;
        AudioDebugValue("transition waveOutOpen failed: ", (DWORD)mm);
        return 0;
    }
    return 1;
}

void AudioPlayTransition(float volume) {
    (void)volume;
}

enum AudioSfxKind {
    AUDIO_SFX_UI_MOVE,
    AUDIO_SFX_UI_CONFIRM,
    AUDIO_SFX_UI_BACK,
    AUDIO_SFX_JUMP,
    AUDIO_SFX_LAND,
    AUDIO_SFX_DEATH,
    AUDIO_SFX_CLEAR,
};

static const char* AudioSfxFileName(AudioSfxKind kind) {
    switch (kind) {
    case AUDIO_SFX_JUMP: return "sfx_jump.ogg";
    case AUDIO_SFX_DEATH: return "sfx_death.ogg";
    default: return 0;
    }
}

static int AudioFindSfxPath(const char* file_name, char* out, int cap) {
    char rel[96];
    static const char* prefixes[] = {
        "assets\\audio\\",
        "..\\assets\\audio\\",
        "..\\..\\assets\\audio\\",
    };
    for (int i = 0; i < (int)(sizeof(prefixes) / sizeof(prefixes[0])); ++i) {
        int at = 0;
        const char* prefix = prefixes[i];
        while (prefix[at] && at < (int)sizeof(rel) - 1) {
            rel[at] = prefix[at];
            ++at;
        }
        for (int j = 0; file_name[j] && at < (int)sizeof(rel) - 1; ++j) {
            rel[at++] = file_name[j];
        }
        rel[at] = 0;
        if (AudioFindExeRelativePath(rel, out, cap)) {
            return 1;
        }
    }
    return 0;
}

static void AudioApplySfxFade(AudioSfxData* sfx) {
    if (!sfx || !sfx->samples || sfx->sample_count <= 1) {
        return;
    }
    int fade = TRANSITION_SAMPLE_RATE / 250;
    if (fade < 1) fade = 1;
    if (fade * 2 > sfx->sample_count) fade = sfx->sample_count / 2;
    for (int i = 0; i < fade; ++i) {
        int in_gain = i * 1024 / fade;
        int out_gain = (fade - 1 - i) * 1024 / fade;
        sfx->samples[i] = AudioClampI16((int)sfx->samples[i] * in_gain / 1024);
        int end = sfx->sample_count - fade + i;
        sfx->samples[end] = AudioClampI16((int)sfx->samples[end] * out_gain / 1024);
    }
}

static void AudioNormalizeSfxAsset(AudioSfxData* sfx, int target_peak) {
    if (!sfx || !sfx->samples || sfx->sample_count <= 0) {
        return;
    }
    int peak = 0;
    for (int i = 0; i < sfx->sample_count; ++i) {
        int sample = sfx->samples[i];
        int mag = sample < 0 ? -sample : sample;
        if (mag > peak) peak = mag;
    }
    if (peak < 1 || peak >= target_peak) {
        return;
    }
    for (int i = 0; i < sfx->sample_count; ++i) {
        int value = (int)sfx->samples[i] * target_peak / peak;
        sfx->samples[i] = AudioClampI16(value);
    }
}

static int AudioDecodeSfxOgg(const char* path, AudioSfxData* sfx) {
    unsigned char* file = 0;
    DWORD file_size = 0;
    if (!AudioLoadFile(path, &file, &file_size)) {
        AudioDebugString("SFX load failed: could not read OGG file");
        return 0;
    }

    int error = 0;
    stb_vorbis* vorbis = stb_vorbis_open_memory(file, (int)file_size, &error, 0);
    if (!vorbis) {
        HeapFree(GetProcessHeap(), 0, file);
        AudioDebugValue("SFX decode failed: stb_vorbis error ", (DWORD)error);
        return 0;
    }

    stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    int src_channels = info.channels;
    int src_rate = (int)info.sample_rate;
    int src_frames = stb_vorbis_stream_length_in_samples(vorbis);
    if (src_channels < 1 || src_channels > 2 || src_rate <= 0 || src_frames <= 0) {
        stb_vorbis_close(vorbis);
        HeapFree(GetProcessHeap(), 0, file);
        AudioDebugString("SFX decode failed: unsupported OGG format");
        return 0;
    }

    int src_count = src_frames * src_channels;
    short* src_samples = (short*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)src_count * sizeof(short));
    if (!src_samples) {
        stb_vorbis_close(vorbis);
        HeapFree(GetProcessHeap(), 0, file);
        AudioDebugString("SFX decode failed: out of memory");
        return 0;
    }
    int got_frames = stb_vorbis_get_samples_short_interleaved(vorbis, src_channels, src_samples, src_count);
    stb_vorbis_close(vorbis);
    HeapFree(GetProcessHeap(), 0, file);
    if (got_frames <= 0) {
        HeapFree(GetProcessHeap(), 0, src_samples);
        AudioDebugString("SFX decode failed: no PCM frames decoded");
        return 0;
    }

    int dst_frames = (int)(((long long)got_frames * TRANSITION_SAMPLE_RATE + src_rate - 1) / src_rate);
    if (dst_frames < 1) dst_frames = 1;
    short* dst_samples = (short*)HeapAlloc(GetProcessHeap(), 0, (SIZE_T)dst_frames * sizeof(short));
    if (!dst_samples) {
        HeapFree(GetProcessHeap(), 0, src_samples);
        AudioDebugString("SFX decode failed: out of memory");
        return 0;
    }

    for (int i = 0; i < dst_frames; ++i) {
        int src_frame = (int)(((long long)i * src_rate) / TRANSITION_SAMPLE_RATE);
        if (src_frame >= got_frames) src_frame = got_frames - 1;
        int src_index = src_frame * src_channels;
        int sample = src_samples[src_index];
        if (src_channels == 2) {
            sample = (sample + src_samples[src_index + 1]) / 2;
        }
        dst_samples[i] = (short)sample;
    }
    HeapFree(GetProcessHeap(), 0, src_samples);

    sfx->samples = dst_samples;
    sfx->sample_count = dst_frames;
    sfx->sample_bytes = (DWORD)(dst_frames * sizeof(short));
    AudioApplySfxFade(sfx);
    AudioNormalizeSfxAsset(sfx, 26000);
    return 1;
}

static int AudioLoadSfx(AudioSfxKind kind) {
    if (kind < 0 || kind >= (AudioSfxKind)7) {
        return 0;
    }
    AudioSfxData* sfx = &g_sfx_assets[(int)kind];
    if (sfx->loaded) {
        return 1;
    }
    if (sfx->load_attempted) {
        return 0;
    }
    sfx->load_attempted = 1;

    const char* file_name = AudioSfxFileName(kind);
    if (!file_name) {
        return 0;
    }
    char path[MAX_PATH];
    if (!AudioFindSfxPath(file_name, path, (int)sizeof(path))) {
        AudioDebugString("SFX load failed: OGG file not found near executable");
        return 0;
    }
    if (!AudioDecodeSfxOgg(path, sfx)) {
        return 0;
    }
    sfx->loaded = 1;
    return 1;
}

static int AudioEnsureSfx() {
    if (g_sfx_wave_out) {
        return 1;
    }
    AudioClearBytes(&g_sfx_format, sizeof(g_sfx_format));
    g_sfx_format.wFormatTag = WAVE_FORMAT_PCM;
    g_sfx_format.nChannels = 1;
    g_sfx_format.nSamplesPerSec = TRANSITION_SAMPLE_RATE;
    g_sfx_format.wBitsPerSample = 16;
    g_sfx_format.nBlockAlign = 2;
    g_sfx_format.nAvgBytesPerSec = TRANSITION_SAMPLE_RATE * g_sfx_format.nBlockAlign;
    MMRESULT mm = waveOutOpen(&g_sfx_wave_out, WAVE_MAPPER, &g_sfx_format, 0, 0, CALLBACK_NULL);
    if (mm != MMSYSERR_NOERROR) {
        g_sfx_wave_out = 0;
        AudioDebugValue("sfx waveOutOpen failed: ", (DWORD)mm);
        return 0;
    }
    return 1;
}

static void AudioPlaySfx(AudioSfxKind kind, float volume, float gain) {
    volume = AudioClampVolume(volume);
    if (volume <= 0.001f || gain <= 0.001f) {
        return;
    }
    if (!AudioEnsureSfx()) {
        return;
    }

    if (g_sfx_prepared) {
        if ((g_sfx_header.dwFlags & WHDR_DONE) == 0) {
            waveOutReset(g_sfx_wave_out);
        }
        waveOutUnprepareHeader(g_sfx_wave_out, &g_sfx_header, sizeof(g_sfx_header));
        g_sfx_prepared = 0;
    }

    if (!AudioLoadSfx(kind)) {
        return;
    }
    AudioSfxData* sfx = &g_sfx_assets[(int)kind];
    waveOutSetVolume(g_sfx_wave_out, AudioVolumeDword(volume * gain));
    AudioClearBytes(&g_sfx_header, sizeof(g_sfx_header));
    g_sfx_header.lpData = (LPSTR)sfx->samples;
    g_sfx_header.dwBufferLength = sfx->sample_bytes;
    MMRESULT mm = waveOutPrepareHeader(g_sfx_wave_out, &g_sfx_header, sizeof(g_sfx_header));
    if (mm != MMSYSERR_NOERROR) {
        AudioDebugValue("sfx waveOutPrepareHeader failed: ", (DWORD)mm);
        return;
    }
    g_sfx_prepared = 1;
    mm = waveOutWrite(g_sfx_wave_out, &g_sfx_header, sizeof(g_sfx_header));
    if (mm != MMSYSERR_NOERROR) {
        AudioDebugValue("sfx waveOutWrite failed: ", (DWORD)mm);
    }
}

void AudioPlayUiMove(float volume) { (void)volume; }
void AudioPlayUiConfirm(float volume) { (void)volume; }
void AudioPlayUiBack(float volume) { (void)volume; }
void AudioPlayJump(float volume) { AudioPlaySfx(AUDIO_SFX_JUMP, volume, 1.00f); }
void AudioPlayLand(float volume) { (void)volume; }
void AudioPlayDeath(float volume) { AudioPlaySfx(AUDIO_SFX_DEATH, volume, 1.55f); }
void AudioPlayClear(float volume) { (void)volume; }

static void AudioStopSfx() {
    if (!g_sfx_wave_out) {
        return;
    }
    waveOutReset(g_sfx_wave_out);
    if (g_sfx_prepared) {
        waveOutUnprepareHeader(g_sfx_wave_out, &g_sfx_header, sizeof(g_sfx_header));
        g_sfx_prepared = 0;
    }
}
static void AudioStopTransition() {
    if (!g_transition_wave_out) {
        return;
    }
    waveOutReset(g_transition_wave_out);
    if (g_transition_prepared) {
        waveOutUnprepareHeader(g_transition_wave_out, &g_transition_header, sizeof(g_transition_header));
        g_transition_prepared = 0;
    }
}
static int AudioEnsureWaveOut() {
    if (g_wave_out) {
        return 1;
    }
    if (!AudioLoadSpeakerKick()) {
        return 0;
    }
    if (waveOutOpen(&g_wave_out, WAVE_MAPPER, &g_speaker_kick.format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        g_wave_out = 0;
        return 0;
    }
    for (int i = 0; i < AUDIO_VOICE_COUNT; ++i) {
        g_voice_buffers[i] = (unsigned char*)HeapAlloc(GetProcessHeap(), 0, g_speaker_kick.sample_bytes);
        if (!g_voice_buffers[i]) {
            return 0;
        }
        AudioClearBytes(&g_voice_headers[i], sizeof(g_voice_headers[i]));
    }
    return 1;
}

static short AudioScaleSample16(short sample, int volume_1000) {
    int value = (int)sample * volume_1000 / 1000;
    if (value < -32768) value = -32768;
    if (value > 32767) value = 32767;
    return (short)value;
}

static void AudioCopyScaled16(unsigned char* dst, const unsigned char* src, DWORD bytes, int volume_1000) {
    short* out = (short*)dst;
    const short* in = (const short*)src;
    DWORD sample_count = bytes / 2;
    for (DWORD i = 0; i < sample_count; ++i) {
        out[i] = AudioScaleSample16(in[i], volume_1000);
    }
}

static void AudioPlaySpeakerKick(float volume) {
    if (!AudioEnsureWaveOut()) {
        return;
    }

    int voice = g_next_voice;
    g_next_voice = (g_next_voice + 1) % AUDIO_VOICE_COUNT;
    WAVEHDR* header = &g_voice_headers[voice];
    if (g_voice_prepared[voice]) {
        if ((header->dwFlags & WHDR_DONE) == 0) {
            waveOutReset(g_wave_out);
        }
        waveOutUnprepareHeader(g_wave_out, header, sizeof(*header));
        g_voice_prepared[voice] = 0;
    }

    int volume_1000 = (int)(volume * 1000.0f + 0.5f);
    if (volume_1000 < 0) volume_1000 = 0;
    if (volume_1000 > 1000) volume_1000 = 1000;
    AudioCopyScaled16(g_voice_buffers[voice], g_speaker_kick.samples, g_speaker_kick.sample_bytes, volume_1000);

    AudioClearBytes(header, sizeof(*header));
    header->lpData = (LPSTR)g_voice_buffers[voice];
    header->dwBufferLength = g_speaker_kick.sample_bytes;
    if (waveOutPrepareHeader(g_wave_out, header, sizeof(*header)) != MMSYSERR_NOERROR) {
        return;
    }
    g_voice_prepared[voice] = 1;
    waveOutWrite(g_wave_out, header, sizeof(*header));
}

static void AudioStopSpeakerKick() {
    if (!g_wave_out) {
        return;
    }
    waveOutReset(g_wave_out);
    for (int i = 0; i < AUDIO_VOICE_COUNT; ++i) {
        if (g_voice_prepared[i]) {
            waveOutUnprepareHeader(g_wave_out, &g_voice_headers[i], sizeof(g_voice_headers[i]));
            g_voice_prepared[i] = 0;
        }
    }
}

void AudioUpdateSpeaker(double speaker_time_seconds, float volume, int active) {
    active = active && volume > 0.001f;
    if (!active) {
        if (g_speaker_audio_was_active) {
            AudioStopSpeakerKick();
        }
        g_speaker_audio_last_beat = -1;
        g_speaker_audio_was_active = 0;
        return;
    }

    if (speaker_time_seconds < 0.0) {
        speaker_time_seconds = 0.0;
    }

    int beat = (int)(speaker_time_seconds / SPEAKER_AUDIO_BEAT_SECONDS);
    if (beat != g_speaker_audio_last_beat) {
        AudioPlaySpeakerKick(volume);
        g_speaker_audio_last_beat = beat;
    }
    g_speaker_audio_was_active = 1;
}

void AudioShutdown() {
    AudioStopBgm();
    if (g_bgm_wave_out) {
        waveOutClose(g_bgm_wave_out);
        g_bgm_wave_out = 0;
    }
    if (g_bgm.samples) {
        HeapFree(GetProcessHeap(), 0, g_bgm.samples);
        g_bgm.samples = 0;
    }
    g_bgm_loaded = 0;
    g_bgm_load_attempted = 0;
    g_bgm_playing = 0;

    AudioStopSfx();
    if (g_sfx_wave_out) {
        waveOutClose(g_sfx_wave_out);
        g_sfx_wave_out = 0;
    }
    for (int i = 0; i < 7; ++i) {
        if (g_sfx_assets[i].samples) {
            HeapFree(GetProcessHeap(), 0, g_sfx_assets[i].samples);
            g_sfx_assets[i].samples = 0;
        }
        g_sfx_assets[i].sample_bytes = 0;
        g_sfx_assets[i].sample_count = 0;
        g_sfx_assets[i].loaded = 0;
        g_sfx_assets[i].load_attempted = 0;
    }

    AudioStopTransition();
    if (g_transition_wave_out) {
        waveOutClose(g_transition_wave_out);
        g_transition_wave_out = 0;
    }
    if (g_transition_samples) {
        HeapFree(GetProcessHeap(), 0, g_transition_samples);
        g_transition_samples = 0;
    }
    g_transition_loaded = 0;
    g_transition_prepared = 0;

    AudioStopSpeakerKick();
    if (g_wave_out) {
        waveOutClose(g_wave_out);
        g_wave_out = 0;
    }
    for (int i = 0; i < AUDIO_VOICE_COUNT; ++i) {
        if (g_voice_buffers[i]) {
            HeapFree(GetProcessHeap(), 0, g_voice_buffers[i]);
            g_voice_buffers[i] = 0;
        }
    }
    if (g_speaker_kick.samples) {
        HeapFree(GetProcessHeap(), 0, g_speaker_kick.samples);
        g_speaker_kick.samples = 0;
    }
    g_speaker_kick_loaded = 0;
    g_speaker_kick_load_attempted = 0;
    g_speaker_audio_last_beat = -1;
    g_speaker_audio_was_active = 0;
}

