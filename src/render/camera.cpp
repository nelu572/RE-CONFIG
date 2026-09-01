#include "camera.h"

static constexpr float CAMERA_DEAD_ZONE_W = 560.0f;
static constexpr float CAMERA_DEAD_ZONE_H = 320.0f;

static float CameraClamp(float value, float min, float max) {
    if (value < min) return min;
    if (value > max) return max;
    return value;
}

float CameraViewportWidth(const Camera* camera) {
    (void)camera;
    return (float)FB_W;
}

float CameraViewportHeight(const Camera* camera) {
    (void)camera;
    return (float)FB_H;
}

static void CameraClampToRoom(Camera* camera, const RoomDef* room) {
    float viewport_w = CameraViewportWidth(camera);
    float viewport_h = CameraViewportHeight(camera);
    float min_x = room->bounds.x;
    float min_y = room->bounds.y;
    float max_x = room->bounds.x + room->bounds.w - viewport_w;
    float max_y = room->bounds.y + room->bounds.h - viewport_h;

    if (max_x < min_x) {
        min_x = room->bounds.x + (room->bounds.w - viewport_w) * 0.5f;
        max_x = min_x;
    }
    if (max_y < min_y) {
        min_y = room->bounds.y + (room->bounds.h - viewport_h) * 0.5f;
        max_y = min_y;
    }
    camera->x = CameraClamp(camera->x, min_x, max_x);
    camera->y = CameraClamp(camera->y, min_y, max_y);
}

static float CameraRectCenterX(const RectF* rect) {
    return rect->x + rect->w * 0.5f;
}

static float CameraRectCenterY(const RectF* rect) {
    return rect->y + rect->h * 0.5f;
}

static float CameraExpFollow(float value) {
    if (value <= 0.0f) return 0.0f;
    if (value >= 8.0f) return 1.0f;

    float exp_neg = 1.0f / (1.0f + value + value * value * 0.5f + value * value * value * 0.1666667f);
    return 1.0f - exp_neg;
}

void CameraResetToRoom(Camera* camera, const RoomDef* room, const RectF* focus_rect) {
    camera->x = CameraRectCenterX(focus_rect) - CameraViewportWidth(camera) * 0.5f;
    camera->y = CameraRectCenterY(focus_rect) - CameraViewportHeight(camera) * 0.5f;
    CameraClampToRoom(camera, room);
}

void CameraFollowDeadZone(Camera* camera, const RoomDef* room, const RectF* focus_rect) {
    float target_x = CameraRectCenterX(focus_rect);
    float target_y = CameraRectCenterY(focus_rect);
    float screen_x = target_x - camera->x;
    float screen_y = target_y - camera->y;
    float dead_left = ((float)FB_W - CAMERA_DEAD_ZONE_W) * 0.5f;
    float dead_right = dead_left + CAMERA_DEAD_ZONE_W;
    float dead_top = ((float)FB_H - CAMERA_DEAD_ZONE_H) * 0.5f;
    float dead_bottom = dead_top + CAMERA_DEAD_ZONE_H;

    if (screen_x < dead_left) {
        camera->x += screen_x - dead_left;
    } else if (screen_x > dead_right) {
        camera->x += screen_x - dead_right;
    }
    if (screen_y < dead_top) {
        camera->y += screen_y - dead_top;
    } else if (screen_y > dead_bottom) {
        camera->y += screen_y - dead_bottom;
    }
    CameraClampToRoom(camera, room);
}

void CameraFollowEntity(Camera* camera, const Entity* target) {
    camera->x = EntityCenterX(target) - CameraViewportWidth(camera) * 0.5f;
    camera->y = EntityCenterY(target) - CameraViewportHeight(camera) * 0.5f;
    float max_x = (float)WORLD_W - CameraViewportWidth(camera);
    float max_y = (float)WORLD_H - CameraViewportHeight(camera);
    if (max_x < 0.0f) max_x = 0.0f;
    if (max_y < 0.0f) max_y = 0.0f;
    camera->x = CameraClamp(camera->x, 0.0f, max_x);
    camera->y = CameraClamp(camera->y, 0.0f, max_y);
}

void CameraFollowPoint(Camera* camera, float target_x, float target_y, float dt) {
    float desired_x = target_x - CameraViewportWidth(camera) * 0.5f;
    float desired_y = target_y - CameraViewportHeight(camera) * 0.5f;
    float max_x = (float)WORLD_W - CameraViewportWidth(camera);
    float max_y = (float)WORLD_H - CameraViewportHeight(camera);
    if (max_x < 0.0f) max_x = 0.0f;
    if (max_y < 0.0f) max_y = 0.0f;
    desired_x = CameraClamp(desired_x, 0.0f, max_x);
    desired_y = CameraClamp(desired_y, 0.0f, max_y);

    float follow = CameraExpFollow(dt * CAMERA_FOLLOW_SPEED);
    if (follow > 1.0f) follow = 1.0f;
    camera->x += (desired_x - camera->x) * follow;
    camera->y += (desired_y - camera->y) * follow;
}

float WorldToScreenX(const Camera* camera, float x) { return x - camera->x; }
float WorldToScreenY(const Camera* camera, float y) { return y - camera->y; }
float ScreenToWorldX(const Camera* camera, float x) { return x + camera->x; }
float ScreenToWorldY(const Camera* camera, float y) { return y + camera->y; }
