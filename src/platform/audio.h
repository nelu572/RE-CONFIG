#pragma once

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
void AudioUpdateSpeaker(double speaker_time_seconds, float volume, int active);
void AudioShutdown();
