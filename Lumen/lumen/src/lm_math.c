#include "lm_math.h"

/* Bhaskara-style parabola with one refinement step: |err| < 1e-3 over a
 * period, far below one pixel at any radius used on a 128x64 panel. */
float lm_sinf(float x)
{
    x = x - LM_TAU * floorf((x + LM_PI) / LM_TAU);   /* -> [-pi, pi) */
    float y = (4.f / LM_PI) * x - (4.f / (LM_PI * LM_PI)) * x * lm_absf(x);
    return 0.225f * (y * lm_absf(y) - y) + y;
}

float lm_cosf(float x) { return lm_sinf(x + LM_PI * 0.5f); }

float lm_atan2f(float y, float x)
{
    float ax = lm_absf(x), ay = lm_absf(y);
    if (ax < 1e-9f && ay < 1e-9f)
        return 0.f;
    float a = (ax > ay) ? ay / ax : ax / ay;
    float s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) r = 1.57079637f - r;
    if (x < 0) r = LM_PI - r;
    if (y < 0) r = -r;
    return r;
}
