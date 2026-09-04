#include "piston.h"

#include "collision.h"

static float PistonClampF(float value, float lo, float hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

float PistonConnectionScale(const PistonDevice* piston) {
    static constexpr float FIVE_TILE_PISTON_WIDTH = 200.0f;
    if (!piston) {
        return 0.0f;
    }
    return PistonClampF(piston->width / FIVE_TILE_PISTON_WIDTH, 0.0f, 1.0f);
}

static float PistonMaxF(float value, float minimum) {
    return value > minimum ? value : minimum;
}

static float PistonAccelerate01(float value) {
    value = PistonClampF(value, 0.0f, 1.0f);
    return value * value * value;
}

static float PistonTravel(const PistonDevice* piston) {
    return piston && piston->travel > 0.0f ? piston->travel : 0.0f;
}

static float PistonExtensionSeconds(const PistonDevice* piston) {
    return PistonMaxF(PistonTravel(piston) / PISTON_MAX_EXTENSION_SPEED, PISTON_MIN_EXTENSION_SECONDS);
}

static float PistonRetractionSeconds(const PistonDevice* piston) {
    return PistonMaxF(PistonTravel(piston) / PISTON_MAX_RETRACTION_SPEED, PISTON_MIN_RETRACTION_SECONDS);
}

float PistonExtensionSpeed(const PistonDevice* piston) {
    return PistonTravel(piston) / PistonExtensionSeconds(piston);
}

float PistonRetractionSpeed(const PistonDevice* piston) {
    return PistonTravel(piston) / PistonRetractionSeconds(piston);
}

static float PistonCycleSeconds(const PistonDevice* piston) {
    return PISTON_PRE_EXTENSION_HOLD_SECONDS
        + PistonExtensionSeconds(piston)
        + PISTON_EXTENDED_HOLD_SECONDS
        + PistonRetractionSeconds(piston)
        + PISTON_POST_RETRACTION_HOLD_SECONDS;
}

static float PistonCycleTime(float time_seconds, float cycle_seconds) {
    int completed_cycles = (int)(time_seconds / cycle_seconds);
    return time_seconds - (float)completed_cycles * cycle_seconds;
}

static PistonDirection PistonDirectionOrDefault(const PistonDevice* piston) {
    if (!piston) {
        return PISTON_DOWN;
    }
    if (piston->direction < PISTON_DOWN || piston->direction > PISTON_LEFT) {
        return PISTON_DOWN;
    }
    return piston->direction;
}

static void PistonDirectionVector(const PistonDevice* piston, float* x, float* y) {
    *x = 0.0f;
    *y = 1.0f;
    PistonDirection direction = PistonDirectionOrDefault(piston);
    if (direction == PISTON_UP) {
        *y = -1.0f;
    } else if (direction == PISTON_RIGHT) {
        *x = 1.0f;
        *y = 0.0f;
    } else if (direction == PISTON_LEFT) {
        *x = -1.0f;
        *y = 0.0f;
    }
}

static int PistonIsHorizontal(const PistonDevice* piston) {
    PistonDirection direction = PistonDirectionOrDefault(piston);
    return direction == PISTON_RIGHT || direction == PISTON_LEFT;
}

PistonPose PistonPoseAt(const PistonDevice* piston, float piston_time_seconds) {
    PistonPose pose = {};
    if (!piston) {
        return pose;
    }

    float delayed_time = piston_time_seconds - piston->start_delay_seconds;
    if (delayed_time <= 0.0f) {
        pose.extension = 0.0f;
        pose.descending = 0;
        return pose;
    }

    float cycle_time = PistonCycleTime(delayed_time, PistonCycleSeconds(piston));
    if (cycle_time < PISTON_PRE_EXTENSION_HOLD_SECONDS) {
        pose.extension = 0.0f;
        pose.descending = 0;
        return pose;
    }

    cycle_time -= PISTON_PRE_EXTENSION_HOLD_SECONDS;
    float extension_time = PistonExtensionSeconds(piston);
    if (cycle_time < extension_time) {
        pose.extension = piston->travel * PistonAccelerate01(cycle_time / extension_time);
        pose.descending = 1;
        return pose;
    }

    cycle_time -= extension_time;
    if (cycle_time < PISTON_EXTENDED_HOLD_SECONDS) {
        pose.extension = piston->travel;
        pose.descending = 0;
        return pose;
    }

    cycle_time -= PISTON_EXTENDED_HOLD_SECONDS;
    float retraction_time = PistonRetractionSeconds(piston);
    if (cycle_time < retraction_time) {
        pose.extension = piston->travel * (1.0f - cycle_time / retraction_time);
        pose.descending = 0;
        return pose;
    }

    pose.extension = 0.0f;
    pose.descending = 0;
    return pose;
}

RectF PistonBodyRect(const PistonDevice* piston) {
    RectF rect = {};
    if (!piston) {
        return rect;
    }
    rect.x = piston->x;
    rect.y = piston->y;
    if (PistonIsHorizontal(piston)) {
        rect.w = piston->body_height;
        rect.h = piston->width;
    } else {
        rect.w = piston->width;
        rect.h = piston->body_height;
    }
    return rect;
}

RectF PistonShaftRectForExtension(const PistonDevice* piston, float extension) {
    RectF rect = {};
    if (!piston) {
        return rect;
    }
    extension = PistonClampF(extension, 0.0f, piston->travel);
    float dir_x;
    float dir_y;
    PistonDirectionVector(piston, &dir_x, &dir_y);
    RectF body = PistonBodyRect(piston);
    float shaft_width = PistonIsHorizontal(piston) ? piston->shaft_width * PistonConnectionScale(piston) : piston->shaft_width;

    if (dir_x > 0.0f) {
        rect.x = body.x + body.w;
        rect.y = body.y + body.h * 0.5f - shaft_width * 0.5f;
        rect.w = extension;
        rect.h = shaft_width;
    } else if (dir_x < 0.0f) {
        rect.x = body.x - extension;
        rect.y = body.y + body.h * 0.5f - shaft_width * 0.5f;
        rect.w = extension;
        rect.h = shaft_width;
    } else if (dir_y < 0.0f) {
        rect.x = body.x + body.w * 0.5f - shaft_width * 0.5f;
        rect.y = body.y - extension;
        rect.w = shaft_width;
        rect.h = extension;
    } else {
        rect.x = body.x + body.w * 0.5f - shaft_width * 0.5f;
        rect.y = body.y + body.h;
        rect.w = shaft_width;
        rect.h = extension;
    }
    return rect;
}

RectF PistonPlateRectForExtension(const PistonDevice* piston, float extension) {
    RectF rect = {};
    if (!piston) {
        return rect;
    }
    extension = PistonClampF(extension, 0.0f, piston->travel);
    float dir_x;
    float dir_y;
    PistonDirectionVector(piston, &dir_x, &dir_y);
    RectF body = PistonBodyRect(piston);
    float plate_height = piston->plate_height;

    if (dir_x > 0.0f) {
        rect.x = body.x + body.w + extension;
        rect.y = body.y;
        rect.w = plate_height;
        rect.h = body.h;
    } else if (dir_x < 0.0f) {
        rect.x = body.x - extension - plate_height;
        rect.y = body.y;
        rect.w = plate_height;
        rect.h = body.h;
    } else if (dir_y < 0.0f) {
        rect.x = body.x;
        rect.y = body.y - extension - plate_height;
        rect.w = body.w;
        rect.h = plate_height;
    } else {
        rect.x = body.x;
        rect.y = body.y + body.h + extension;
        rect.w = body.w;
        rect.h = plate_height;
    }
    return rect;
}

RectF PistonShaftRectAt(const PistonDevice* piston, float piston_time_seconds) {
    return PistonShaftRectForExtension(piston, PistonPoseAt(piston, piston_time_seconds).extension);
}

RectF PistonPlateRectAt(const PistonDevice* piston, float piston_time_seconds) {
    return PistonPlateRectForExtension(piston, PistonPoseAt(piston, piston_time_seconds).extension);
}


RectF PistonTravelDirtyRect(const PistonDevice* piston) {
    RectF rect = {};
    if (!piston) {
        return rect;
    }
    RectF body = PistonBodyRect(piston);
    RectF retracted_plate = PistonPlateRectAt(piston, 0.0f);
    float dir_x;
    float dir_y;
    PistonDirectionVector(piston, &dir_x, &dir_y);

    rect = body;
    float x0 = retracted_plate.x < rect.x ? retracted_plate.x : rect.x;
    float y0 = retracted_plate.y < rect.y ? retracted_plate.y : rect.y;
    float x1 = retracted_plate.x + retracted_plate.w > rect.x + rect.w ? retracted_plate.x + retracted_plate.w : rect.x + rect.w;
    float y1 = retracted_plate.y + retracted_plate.h > rect.y + rect.h ? retracted_plate.y + retracted_plate.h : rect.y + rect.h;
    if (dir_x > 0.0f) {
        x1 += piston->travel;
    } else if (dir_x < 0.0f) {
        x0 -= piston->travel;
    } else if (dir_y > 0.0f) {
        y1 += piston->travel;
    } else if (dir_y < 0.0f) {
        y0 -= piston->travel;
    }

    rect.x = x0 - 8.0f;
    rect.y = y0 - 8.0f;
    rect.w = x1 - x0 + 16.0f;
    rect.h = y1 - y0 + 16.0f;
    return rect;
}

int PistonOverlapsRectAt(const PistonDevice* piston, float piston_time_seconds, const RectF* rect) {
    RectF shaft = PistonShaftRectAt(piston, piston_time_seconds);
    if (shaft.w > 0.001f && shaft.h > 0.001f && RectsOverlap(&shaft, rect)) {
        return 1;
    }
    RectF plate = PistonPlateRectAt(piston, piston_time_seconds);
    return RectsOverlap(&plate, rect);
}
