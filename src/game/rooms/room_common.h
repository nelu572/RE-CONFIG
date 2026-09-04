#pragma once

#include "../world.h"

static constexpr int ROOM_TILE_SIZE = 40;

static constexpr float T(int tiles) {
    return (float)(tiles * ROOM_TILE_SIZE);
}

static constexpr float T(float tiles) {
    return tiles * (float)ROOM_TILE_SIZE;
}

static constexpr float kDefaultGravityBoxSize = T(1.5f);

static constexpr StaticSpikeDef StaticSpikeAt(float x, float y,
                                               StaticSpikeRotation rotation = STATIC_SPIKE_ROTATION_0_DEGREES) {
    if (rotation == STATIC_SPIKE_ROTATION_90_DEGREES) {
        return { { x, y, T(0.5f), T(1) }, rotation };
    }
    if (rotation == STATIC_SPIKE_ROTATION_180_DEGREES) {
        return { { x, y, T(1), T(0.5f) }, rotation };
    }
    if (rotation == STATIC_SPIKE_ROTATION_270_DEGREES) {
        return { { x + T(0.5f), y, T(0.5f), T(1) }, rotation };
    }
    return { { x, y + T(0.5f), T(1), T(0.5f) }, STATIC_SPIKE_ROTATION_0_DEGREES };
}

static constexpr RectF DefaultGravityBoxAt(float x, float y) {
    return { x, y, kDefaultGravityBoxSize, kDefaultGravityBoxSize };
}
static constexpr float kDefaultSpeakerWidth = T(5);
static constexpr float kDefaultSpeakerHeight = T(9);

static constexpr SpeakerDevice DefaultSpeakerAt(float x, float y, SpeakerMount mount = SPEAKER_MOUNT_AUTO) {
    return { x, y, kDefaultSpeakerWidth, kDefaultSpeakerHeight, mount, SPEAKER_STYLE_STANDARD, 1.0f, 1.0f };
}

static constexpr float kMiniSpeakerWidth = T(3);
static constexpr float kMiniSpeakerHeight = T(5);

static constexpr SpeakerDevice MiniSpeakerAt(float x, float y) {
    return { x, y, kMiniSpeakerWidth, kMiniSpeakerHeight, SPEAKER_MOUNT_AUTO, SPEAKER_STYLE_MINI, 0.4f, 0.4f };
}

static constexpr DeleteState kDefaultDeleteState = {};
