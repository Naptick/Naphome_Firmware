// Auto-generated LED positions for face LED simulator
#pragma once

#include <stdint.h>

#define FACE_LED_COUNT 50
#define FACE_LED_INNER_COUNT 16
#define FACE_LED_OUTER_COUNT 24

typedef struct {
    int16_t x;
    int16_t y;
    float angle;
} face_led_position_t;

static const face_led_position_t face_led_positions[FACE_LED_COUNT] = {
    { 94, 67, 0.000000f },  // LED 0 (inner)
    { 93, 70, 0.392699f },  // LED 1 (inner)
    { 91, 74, 0.785398f },  // LED 2 (inner)
    { 87, 76, 1.178097f },  // LED 3 (inner)
    { 84, 77, 1.570796f },  // LED 4 (inner)
    { 80, 76, 1.963495f },  // LED 5 (inner)
    { 76, 74, 2.356194f },  // LED 6 (inner)
    { 74, 70, 2.748894f },  // LED 7 (inner)
    { 73, 67, 3.141593f },  // LED 8 (inner)
    { 74, 63, 3.534292f },  // LED 9 (inner)
    { 76, 59, 3.926991f },  // LED 10 (inner)
    { 80, 57, 4.319690f },  // LED 11 (inner)
    { 84, 56, 4.712389f },  // LED 12 (inner)
    { 87, 57, 5.105088f },  // LED 13 (inner)
    { 91, 59, 5.497787f },  // LED 14 (inner)
    { 93, 63, 5.890486f },  // LED 15 (inner)
    { 106, 67, 0.000000f },  // LED 16 (outer)
    { 105, 72, 0.261799f },  // LED 17 (outer)
    { 103, 78, 0.523599f },  // LED 18 (outer)
    { 99, 82, 0.785398f },  // LED 19 (outer)
    { 95, 86, 1.047198f },  // LED 20 (outer)
    { 89, 88, 1.308997f },  // LED 21 (outer)
    { 84, 89, 1.570796f },  // LED 22 (outer)
    { 78, 88, 1.832596f },  // LED 23 (outer)
    { 72, 86, 2.094395f },  // LED 24 (outer)
    { 68, 82, 2.356194f },  // LED 25 (outer)
    { 64, 78, 2.617994f },  // LED 26 (outer)
    { 62, 72, 2.879793f },  // LED 27 (outer)
    { 61, 67, 3.141593f },  // LED 28 (outer)
    { 62, 61, 3.403392f },  // LED 29 (outer)
    { 64, 55, 3.665191f },  // LED 30 (outer)
    { 68, 51, 3.926991f },  // LED 31 (outer)
    { 72, 47, 4.188790f },  // LED 32 (outer)
    { 78, 45, 4.450590f },  // LED 33 (outer)
    { 84, 44, 4.712389f },  // LED 34 (outer)
    { 89, 45, 4.974188f },  // LED 35 (outer)
    { 95, 47, 5.235988f },  // LED 36 (outer)
    { 99, 51, 5.497787f },  // LED 37 (outer)
    { 103, 55, 5.759587f },  // LED 38 (outer)
    { 105, 61, 6.021386f },  // LED 39 (outer)
};
