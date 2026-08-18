#include "math_util.h"

int IntAbs(int value) {
    return value < 0 ? -value : value;
}

int FloorToInt(float value) {
    int result = (int)value;
    return (float)result > value ? result - 1 : result;
}

int CeilToInt(float value) {
    int result = (int)value;
    return (float)result < value ? result + 1 : result;
}

float FloatAbs(float value) {
    return value < 0.0f ? -value : value;
}

float Clamp01(float value) {
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float EaseOut(float t) {
    t = Clamp01(t);
    return 1.0f - (1.0f - t) * (1.0f - t);
}

float SmoothStep(float t) {
    t = Clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

float SinApprox(float radians) {
    const float pi = 3.14159265f;
    const float half_pi = 1.57079633f;
    const float two_pi = 6.28318531f;
    while (radians > pi) radians -= two_pi;
    while (radians < -pi) radians += two_pi;
    if (radians > half_pi) radians = pi - radians;
    if (radians < -half_pi) radians = -pi - radians;
    float x2 = radians * radians;
    return radians * (1.0f - x2 * (1.0f / 6.0f) + x2 * x2 * (1.0f / 120.0f) - x2 * x2 * x2 * (1.0f / 5040.0f));
}

float CosApprox(float radians) {
    return SinApprox(radians + 1.57079633f);
}

void Normalize2(float* x, float* y) {
    float len_sq = *x * *x + *y * *y;
    if (len_sq <= 0.000001f) {
        *x = 1.0f;
        *y = 0.0f;
        return;
    }

    float ax = FloatAbs(*x);
    float ay = FloatAbs(*y);
    float len = ax > ay ? ax : ay;
    if (len < 1.0f) len = 1.0f;
    for (int i = 0; i < 5; ++i) {
        len = 0.5f * (len + len_sq / len);
    }
    *x /= len;
    *y /= len;
}
