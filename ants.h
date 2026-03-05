#pragma once

#include <stdint.h>

#define MAX_ANTS_SPEED 5

typedef enum {
    MANTS_HORZ,
    MANTS_VERT
} orientation_t;

typedef struct {
    uint8_t orientation;
    int8_t direction;
    uint8_t speed;
    uint8_t x1, y1, x2, y2;
    int8_t accu;
    int8_t pos;
    int8_t div;
} marching_ants_t;

// maybe -- x1,y1 - x2,y2 - x3,y3 - x - x4,y4

void marching_ants_h(marching_ants_t * state, uint8_t x1, uint8_t x2, uint8_t y, int8_t dir);
void marching_ants_v(marching_ants_t * state, uint8_t x, uint8_t y1, uint8_t y2, int8_t dir);
void marching_ants_step(marching_ants_t * state);
void marching_ants_set_speed(marching_ants_t * state, uint8_t speed);

// set yellow/blue separation position, 0 = all blue
void marching_ants_set_div(marching_ants_t * state, uint8_t div);
