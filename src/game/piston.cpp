#include "piston.h"

#include "collision.h"

static float PistonClampF(float value, float lo, float hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static float PistonSmooth01(float value) {
    value = PistonClampF(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}

static float PistonDrop01(float value) {
    value = PistonClampF(value, 0.0f, 1.0f);
    return value * value * value;
}

static float PistonWrap01(float value) {
    int whole = (int)value;
    value -= (float)whole;
    if (value < 0.0f) {
        value += 1.0f;
    }
    return value;
}

PistonPose PistonPoseAt(const PistonDevice* piston, float piston_time_seconds) {
    PistonPose pose = {};
    if (!piston || piston->cycle_seconds <= 0.001f) {
        return pose;
    }

    float phase = PistonWrap01(piston_time_seconds / piston->cycle_seconds + piston->phase);
    if (phase < 0.14f) {
        pose.extension = 0.0f;
        pose.descending = 0;
    } else if (phase < 0.32f) {
        float t = (phase - 0.14f) / 0.18f;
        pose.extension = piston->travel * PistonDrop01(t);
        pose.descending = 1;
    } else if (phase < 0.42f) {
        pose.extension = piston->travel;
        pose.descending = 0;
    } else if (phase < 0.88f) {
        float t = (phase - 0.42f) / 0.46f;
        pose.extension = piston->travel * (1.0f - PistonSmooth01(t));
        pose.descending = 0;
    } else {
        pose.extension = 0.0f;
        pose.descending = 0;
    }
    return pose;
}

RectF PistonBodyRect(const PistonDevice* piston) {
    RectF rect = {};
    if (!piston) {
        return rect;
    }
    rect.x = piston->x;
    rect.y = piston->y;
    rect.w = piston->width;
    rect.h = piston->body_height;
    return rect;
}

RectF PistonShaftRectAt(const PistonDevice* piston, float piston_time_seconds) {
    RectF rect = {};
    if (!piston) {
        return rect;
    }
    PistonPose pose = PistonPoseAt(piston, piston_time_seconds);
    rect.w = piston->shaft_width;
    rect.h = pose.extension;
    rect.x = piston->x + piston->width * 0.5f - piston->shaft_width * 0.5f;
    rect.y = piston->y + piston->body_height;
    return rect;
}

RectF PistonPlateRectAt(const PistonDevice* piston, float piston_time_seconds) {
    RectF rect = {};
    if (!piston) {
        return rect;
    }
    PistonPose pose = PistonPoseAt(piston, piston_time_seconds);
    rect.x = piston->x;
    rect.y = piston->y + piston->body_height + pose.extension;
    rect.w = piston->width;
    rect.h = piston->plate_height;
    return rect;
}

RectF PistonTravelDirtyRect(const PistonDevice* piston) {
    RectF rect = {};
    if (!piston) {
        return rect;
    }
    rect.x = piston->x - 8.0f;
    rect.y = piston->y - 8.0f;
    rect.w = piston->width + 16.0f;
    rect.h = piston->body_height + piston->travel + piston->plate_height + 16.0f;
    return rect;
}

int PistonOverlapsRectAt(const PistonDevice* piston, float piston_time_seconds, const RectF* rect) {
    RectF shaft = PistonShaftRectAt(piston, piston_time_seconds);
    if (shaft.h > 0.001f && RectsOverlap(&shaft, rect)) {
        return 1;
    }
    RectF plate = PistonPlateRectAt(piston, piston_time_seconds);
    return RectsOverlap(&plate, rect);
}