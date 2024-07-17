#pragma once

#include <stdint.h>
#include <math.h>

typedef struct ColorRGB {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} ColorRGB;

typedef struct ColorHSL {
    float h;
    float s;
    float l;
} ColorHSL;

// See
//
//   https://en.wikipedia.org/wiki/HSL_and_HSV#HSL_to_RGB_alternative
//
// for explanation.
static inline float hsl_f(ColorHSL col, int n) {
    float k = fmodf((n + col.h/30.0f), 12.0f);
    float a = col.s*fminf(col.l, 1.0f-col.l);
    return col.l - a*fmaxf(fminf(fminf(k-3.0f, 9.0f-k),1.0f),-1.0f);
}

static inline ColorRGB hsl_to_rgb(ColorHSL col) {
    return (ColorRGB) {
        .r = (uint8_t) (255.0f*hsl_f(col, 0)),
        .g = (uint8_t) (255.0f*hsl_f(col, 8)),
        .b = (uint8_t) (255.0f*hsl_f(col, 4)),
    };
}
