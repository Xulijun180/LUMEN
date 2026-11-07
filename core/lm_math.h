/*
 * Small float helpers. Only sqrtf/fabsf/floorf/ceilf come from libm; the
 * trig functions are cheap polynomial approximations so the core does not
 * pull the full sin/cos/atan2 implementations into flash on an MCU.
 */
#ifndef LM_MATH_H
#define LM_MATH_H

#include <math.h>
#include <stdint.h>

#define LM_PI  3.14159265f
#define LM_TAU 6.28318531f

static inline float lm_clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static inline float lm_clamp01(float v) { return lm_clampf(v, 0.f, 1.f); }
static inline float lm_lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float lm_absf(float v) { return v < 0 ? -v : v; }
static inline float lm_minf(float a, float b) { return a < b ? a : b; }
static inline float lm_maxf(float a, float b) { return a > b ? a : b; }
static inline int   lm_mini(int a, int b) { return a < b ? a : b; }
static inline int   lm_maxi(int a, int b) { return a > b ? a : b; }

/* round half away from zero -> int */
static inline int lm_roundi(float v) { return (int)(v < 0 ? v - 0.5f : v + 0.5f); }

static inline float lm_remap(float v, float a0, float a1, float b0, float b1)
{
    float t = (a1 != a0) ? (v - a0) / (a1 - a0) : 1.f;
    return b0 + (b1 - b0) * lm_clamp01(t);
}

float lm_sinf(float x);                 /* |err| < 1e-3 */
float lm_cosf(float x);
float lm_atan2f(float y, float x);      /* |err| < 5e-3 rad */

/* deterministic PRNG (xorshift32) for demo data */
typedef struct { uint32_t s; } lm_rng_t;
static inline uint32_t lm_rng_u32(lm_rng_t *r)
{
    uint32_t x = r->s ? r->s : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return r->s = x;
}
static inline float lm_rng_f(lm_rng_t *r) { return (lm_rng_u32(r) >> 8) * (1.f / 16777216.f); }

#endif
