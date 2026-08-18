#include "camera.h"

static float CameraClamp(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

static float CameraExpFollow(float value) {
    if (value <= 0.0f) return 0.0f;
    if (value >= 8.0f) return 1.0f;

    float exp_neg = 1.0f / (1.0f + value + value * value * 0.5f + value * value * value * 0.1666667f);
    return 1.0f - exp_neg;
}

void CameraFollowEntity(Camera* camera, const Entity* target) {
    camera->x = EntityCenterX(target) - FB_W * 0.5f;
    camera->y = EntityCenterY(target) - FB_H * 0.5f;
    camera->x = CameraClamp(camera->x, 0.0f, (float)(WORLD_W - FB_W));
    camera->y = CameraClamp(camera->y, 0.0f, (float)(WORLD_H - FB_H));
}

void CameraFollowPoint(Camera* camera, float target_x, float target_y, float dt) {
    float desired_x = target_x - FB_W * 0.5f;
    float desired_y = target_y - FB_H * 0.5f;
    desired_x = CameraClamp(desired_x, 0.0f, (float)(WORLD_W - FB_W));
    desired_y = CameraClamp(desired_y, 0.0f, (float)(WORLD_H - FB_H));

    float follow = CameraExpFollow(dt * CAMERA_FOLLOW_SPEED);
    if (follow > 1.0f) follow = 1.0f;
    camera->x += (desired_x - camera->x) * follow;
    camera->y += (desired_y - camera->y) * follow;
}

float WorldToScreenX(const Camera* camera, float x) {
    return x - camera->x;
}

float WorldToScreenY(const Camera* camera, float y) {
    return y - camera->y;
}

float ScreenToWorldX(const Camera* camera, float x) {
    return x + camera->x;
}

float ScreenToWorldY(const Camera* camera, float y) {
    return y + camera->y;
}
