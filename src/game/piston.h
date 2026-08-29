#pragma once

#include "world.h"

struct PistonPose {
    float extension;
    int descending;
};

PistonPose PistonPoseAt(const PistonDevice* piston, float piston_time_seconds);
RectF PistonBodyRect(const PistonDevice* piston);
RectF PistonShaftRectForExtension(const PistonDevice* piston, float extension);
RectF PistonPlateRectForExtension(const PistonDevice* piston, float extension);
RectF PistonShaftRectAt(const PistonDevice* piston, float piston_time_seconds);
RectF PistonPlateRectAt(const PistonDevice* piston, float piston_time_seconds);
RectF PistonTravelDirtyRect(const PistonDevice* piston);
int PistonOverlapsRectAt(const PistonDevice* piston, float piston_time_seconds, const RectF* rect);