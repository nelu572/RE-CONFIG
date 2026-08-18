#pragma once

#include "entity.h"

struct Camera {
    float x;
    float y;
};

void CameraFollowEntity(Camera* camera, const Entity* target);
void CameraFollowPoint(Camera* camera, float target_x, float target_y, float dt);
float WorldToScreenX(const Camera* camera, float x);
float WorldToScreenY(const Camera* camera, float y);
float ScreenToWorldX(const Camera* camera, float x);
float ScreenToWorldY(const Camera* camera, float y);
