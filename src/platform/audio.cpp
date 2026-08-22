#include "audio.h"

#include <stdint.h>
#include <stddef.h>
#include <windows.h>
#include <mmsystem.h>

static constexpr double SPEAKER_AUDIO_BPM = 130.0;
static constexpr double SPEAKER_AUDIO_BEAT_SECONDS = 60.0 / SPEAKER_AUDIO_BPM;
static constexpr int AUDIO_VOICE_COUNT = 4;

struct AudioWavData {
    WAVEFORMATEX format;
    unsigned char* samples;
    DWORD sample_bytes;
};

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
