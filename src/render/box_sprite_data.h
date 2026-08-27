#pragma once

#include <stdint.h>

struct BoxSpriteRect {
    uint8_t x;
    uint8_t y;
    uint8_t w;
    uint8_t h;
    uint32_t rgba;
};

static constexpr int BOX_SPRITE_WIDTH = 40;
static constexpr int BOX_SPRITE_HEIGHT = 40;
static constexpr int BOX_SPRITE_RECT_COUNT = 65;
static const BoxSpriteRect BOX_SPRITE_RECTS[BOX_SPRITE_RECT_COUNT] = {
    { 0u, 0u, 40u, 4u, 0xCB4855FFu },
    { 0u, 4u, 8u, 2u, 0xCB4855FFu },
    { 8u, 4u, 24u, 2u, 0xE78D96FFu },
    { 32u, 4u, 8u, 2u, 0xCB4855FFu },
    { 0u, 6u, 10u, 2u, 0xCB4855FFu },
    { 10u, 6u, 20u, 2u, 0xE78D96FFu },
    { 30u, 6u, 10u, 2u, 0xCB4855FFu },
    { 0u, 8u, 4u, 24u, 0xCB4855FFu },
    { 4u, 8u, 2u, 2u, 0xE78D96FFu },
    { 6u, 8u, 6u, 2u, 0xCB4855FFu },
    { 12u, 8u, 16u, 2u, 0xE78D96FFu },
    { 28u, 8u, 6u, 2u, 0xCB4855FFu },
    { 34u, 8u, 2u, 2u, 0xE78D96FFu },
    { 36u, 8u, 4u, 24u, 0xCB4855FFu },
    { 4u, 10u, 4u, 2u, 0xE78D96FFu },
    { 8u, 10u, 6u, 2u, 0xCB4855FFu },
    { 14u, 10u, 12u, 2u, 0xE78D96FFu },
    { 26u, 10u, 6u, 2u, 0xCB4855FFu },
    { 32u, 10u, 4u, 2u, 0xE78D96FFu },
    { 4u, 12u, 6u, 2u, 0xE78D96FFu },
    { 10u, 12u, 6u, 2u, 0xCB4855FFu },
    { 16u, 12u, 8u, 2u, 0xE78D96FFu },
    { 24u, 12u, 6u, 2u, 0xCB4855FFu },
    { 30u, 12u, 6u, 2u, 0xE78D96FFu },
    { 4u, 14u, 8u, 2u, 0xE78D96FFu },
    { 12u, 14u, 6u, 2u, 0xCB4855FFu },
    { 18u, 14u, 4u, 2u, 0xE78D96FFu },
    { 22u, 14u, 6u, 2u, 0xCB4855FFu },
    { 28u, 14u, 8u, 2u, 0xE78D96FFu },
    { 4u, 16u, 10u, 2u, 0xE78D96FFu },
    { 14u, 16u, 12u, 2u, 0xCB4855FFu },
    { 26u, 16u, 10u, 2u, 0xE78D96FFu },
    { 4u, 18u, 12u, 4u, 0xE78D96FFu },
    { 16u, 18u, 8u, 4u, 0xCB4855FFu },
    { 24u, 18u, 12u, 4u, 0xE78D96FFu },
    { 4u, 22u, 10u, 2u, 0xE78D96FFu },
    { 14u, 22u, 12u, 2u, 0xCB4855FFu },
    { 26u, 22u, 10u, 2u, 0xE78D96FFu },
    { 4u, 24u, 8u, 2u, 0xE78D96FFu },
    { 12u, 24u, 6u, 2u, 0xCB4855FFu },
    { 18u, 24u, 4u, 2u, 0xE78D96FFu },
    { 22u, 24u, 6u, 2u, 0xCB4855FFu },
    { 28u, 24u, 8u, 2u, 0xE78D96FFu },
    { 4u, 26u, 6u, 2u, 0xE78D96FFu },
    { 10u, 26u, 6u, 2u, 0xCB4855FFu },
    { 16u, 26u, 8u, 2u, 0xE78D96FFu },
    { 24u, 26u, 6u, 2u, 0xCB4855FFu },
    { 30u, 26u, 6u, 2u, 0xE78D96FFu },
    { 4u, 28u, 4u, 2u, 0xE78D96FFu },
    { 8u, 28u, 6u, 2u, 0xCB4855FFu },
    { 14u, 28u, 12u, 2u, 0xE78D96FFu },
    { 26u, 28u, 6u, 2u, 0xCB4855FFu },
    { 32u, 28u, 4u, 2u, 0xE78D96FFu },
    { 4u, 30u, 2u, 2u, 0xE78D96FFu },
    { 6u, 30u, 6u, 2u, 0xCB4855FFu },
    { 12u, 30u, 16u, 2u, 0xE78D96FFu },
    { 28u, 30u, 6u, 2u, 0xCB4855FFu },
    { 34u, 30u, 2u, 2u, 0xE78D96FFu },
    { 0u, 32u, 10u, 2u, 0xCB4855FFu },
    { 10u, 32u, 20u, 2u, 0xE78D96FFu },
    { 30u, 32u, 10u, 2u, 0xCB4855FFu },
    { 0u, 34u, 8u, 2u, 0xCB4855FFu },
    { 8u, 34u, 24u, 2u, 0xE78D96FFu },
    { 32u, 34u, 8u, 2u, 0xCB4855FFu },
    { 0u, 36u, 40u, 4u, 0xCB4855FFu },
};
