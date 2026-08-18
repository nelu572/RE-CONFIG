#pragma once

int IntAbs(int value);
int FloorToInt(float value);
int CeilToInt(float value);
float FloatAbs(float value);
float Clamp01(float value);
float Lerp(float a, float b, float t);
float EaseOut(float t);
float SmoothStep(float t);
float SinApprox(float radians);
float CosApprox(float radians);
void Normalize2(float* x, float* y);
