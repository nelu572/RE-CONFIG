#pragma once

#include "world.h"

void AudioStartBgm(float volume);
void AudioSetBgmVolume(float volume);
void AudioUpdateBgm(float volume);
void AudioPlayTransition(float volume);
void AudioPlayUiMove(float volume);
void AudioPlayUiConfirm(float volume);
void AudioPlayUiBack(float volume);
void AudioPlayJump(float volume);
void AudioPlayLand(float volume);
void AudioPlayDeath(float volume);
void AudioPlayClear(float volume);
void AudioPauseSpeakers();
void AudioUpdateSpeakers(double speaker_time_seconds,
                         float volume,
                         const SpeakerDevice* speakers,
                         int speaker_count,
                         const RectF* listener);
void AudioShutdown();
