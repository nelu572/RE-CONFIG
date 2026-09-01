#pragma once

#include "render.h"
#include "world.h"

#include <math.h>

static constexpr int WALKER_ENEMY_RENDER_SPIKE_COUNT = 7;

struct WalkerEnemySpikeGeometry {
    int is_side_wedge;
    float left_x;
    float left_y;
    float right_x;
    float right_y;
    float tip_x;
    float tip_y;
    float cut_x;
    float cut_y;
};

struct WalkerEnemyRenderGeometry {
    int body_x;
    int body_y;
    int body_w;
    int body_h;
    int center_x;
    int top_y;
    int radius_x;
    int radius_y;
    int spike_count;
    WalkerEnemySpikeGeometry spikes[WALKER_ENEMY_RENDER_SPIKE_COUNT];
};

static inline int WalkerEnemyGeometryMaxI(int a, int b) {
    return a > b ? a : b;
}

static inline int WalkerEnemyGeometryMinI(int a, int b) {
    return a < b ? a : b;
}

static inline void WalkerEnemyBuildRenderGeometry(RenderContext* render,
                                                  const RectF* enemy,
                                                  float spike_amount,
                                                  float response_squash,
                                                  float turn_squash,
                                                  WalkerEnemyRenderGeometry* out) {
    int natural_x = WorldX(render, enemy->x);
    int natural_w = WorldW(render, enemy->w);
    out->body_y = WorldY(render, enemy->y);
    out->body_h = WorldH(render, enemy->h);

    float squash_amount = response_squash + turn_squash * 0.20f;
    if (squash_amount > 1.0f) squash_amount = 1.0f;
    float widen_amount = response_squash * 0.08f + turn_squash * 0.02f;
    int widened_w = (int)((float)natural_w * (1.0f + widen_amount) + 0.5f);
    out->body_w = WalkerEnemyGeometryMaxI(natural_w, widened_w);
    out->body_x = natural_x + natural_w / 2 - out->body_w / 2;
    int squash_px = (int)((float)out->body_h * 0.18f * squash_amount + 0.5f);
    out->radius_x = WalkerEnemyGeometryMaxI(4, out->body_w / 2);
    int dome_height = WalkerEnemyGeometryMinI(out->body_h - 4, out->radius_x);
    out->radius_y = WalkerEnemyGeometryMaxI(4, dome_height - squash_px);
    out->top_y = out->body_y + squash_px;
    out->center_x = out->body_x + out->body_w / 2;
    out->spike_count = 0;

    if (spike_amount <= 0.01f) {
        return;
    }

    // Five main upper teeth plus two side wedges: exact 30-degree symmetric fan from 180° to 0°.
    static const float direction_x[WALKER_ENEMY_RENDER_SPIKE_COUNT] = {
        -1.0f, -0.8660254f, -0.5f, 0.0f, 0.5f, 0.8660254f, 1.0f
    };
    static const float direction_y[WALKER_ENEMY_RENDER_SPIKE_COUNT] = {
         0.0f, -0.5f, -0.8660254f, -1.0f, -0.8660254f, -0.5f, 0.0f
    };
    static constexpr float VISIBLE_SPIKE_LENGTH = 16.0f;
    static constexpr float SPIKE_ROOT_EMBED_DEPTH = 2.0f;
    float half_width = (float)WalkerEnemyGeometryMaxI(10, out->body_w / 6);
    float ellipse_center_y = (float)(out->top_y + out->radius_y);

    for (int i = 0; i < WALKER_ENEMY_RENDER_SPIKE_COUNT; ++i) {
        float outward_x = direction_x[i];
        float outward_y = direction_y[i];
        float ellipse_scale = sqrtf((outward_x * outward_x) / (float)(out->radius_x * out->radius_x) +
                                    (outward_y * outward_y) / (float)(out->radius_y * out->radius_y));
        if (ellipse_scale <= 0.0f) {
            continue;
        }
        float outline_x = (float)out->center_x + outward_x / ellipse_scale;
        float outline_y = ellipse_center_y + outward_y / ellipse_scale;
        float base_x = outline_x - outward_x * SPIKE_ROOT_EMBED_DEPTH;
        float base_y = outline_y - outward_y * SPIKE_ROOT_EMBED_DEPTH;
        WalkerEnemySpikeGeometry* spike = &out->spikes[out->spike_count++];

        if (i == 0 || i == WALKER_ENEMY_RENDER_SPIKE_COUNT - 1) {
            // Half-cut right-triangle wedge: its clipped lower edge stays flush with the body/floor line.
            float wedge_height = half_width * spike_amount;
            float floor_y = (float)(out->body_y + out->body_h);
            spike->is_side_wedge = 1;
            spike->left_x = base_x;
            spike->left_y = floor_y - wedge_height;
            spike->right_x = base_x;
            spike->right_y = floor_y;
            spike->cut_x = outline_x + outward_x * VISIBLE_SPIKE_LENGTH * 0.65f * spike_amount;
            spike->cut_y = floor_y;
            spike->tip_x = outline_x + outward_x * VISIBLE_SPIKE_LENGTH * spike_amount;
            spike->tip_y = floor_y - wedge_height * 0.60f;
        } else {
            float tangent_x = -outward_y;
            float tangent_y = outward_x;
            spike->is_side_wedge = 0;
            spike->left_x = base_x - tangent_x * half_width;
            spike->left_y = base_y - tangent_y * half_width;
            spike->right_x = base_x + tangent_x * half_width;
            spike->right_y = base_y + tangent_y * half_width;
            spike->cut_x = 0.0f;
            spike->cut_y = 0.0f;
            spike->tip_x = outline_x + outward_x * VISIBLE_SPIKE_LENGTH * spike_amount;
            spike->tip_y = outline_y + outward_y * VISIBLE_SPIKE_LENGTH * spike_amount;
        }
    }
}
