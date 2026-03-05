#pragma once

#include <stdint.h>

enum {
    ASTICK_MIN= 5,
    ASTICK_SE = 5,
    ASTICK_NE = 6,
    ASTICK_E  = 7,
    ASTICK_SW = 9,
    ASTICK_NW = 10,
    ASTICK_W  = 11,
    ASTICK_S  = 13,
    ASTICK_N  = 14,
    ASTICK_JAM= 15,
    ASTICK_MAX= 15,
};

void fill(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2);

