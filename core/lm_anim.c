#include "lm_anim.h"

#include "lm_config.h"
#include "lm_math.h"

#include "lm_curves.inc"

/*                               f     z     r   (see lm_anim.h for the roles) */
const lm_motion_t lm_motions[LM_MOTION_COUNT] = {
    [LM_MOTION_SNAPPY]     = LM_MOTION_DEF(3.6f, 0.82f,  1.0f),
    [LM_MOTION_CAROUSEL]   = LM_MOTION_DEF(2.6f, 0.78f,  1.0f),
    [LM_MOTION_SMOOTH]     = LM_MOTION_DEF(2.2f, 1.00f,  0.6f),
    [LM_MOTION_FOLLOW]     = LM_MOTION_DEF(1.9f, 0.90f,  0.0f),
    [LM_MOTION_BOUNCY]     = LM_MOTION_DEF(2.8f, 0.50f,  1.0f),
    [LM_MOTION_TOGGLE]     = LM_MOTION_DEF(3.2f, 0.62f,  1.3f),
    [LM_MOTION_GENTLE]     = LM_MOTION_DEF(1.2f, 1.00f,  0.0f),
    [LM_MOTION_ZOOM]       = LM_MOTION_DEF(2.3f, 0.78f, -0.8f),
    [LM_MOTION_PRESS]      = LM_MOTION_DEF(6.0f, 0.45f,  0.0f),
    [LM_MOTION_WOBBLE]     = LM_MOTION_DEF(9.0f, 0.22f,  0.0f),
    [LM_MOTION_EXIT]       = LM_MOTION_DEF(2.8f, 1.00f, -0.5f),
    [LM_MOTION_VU_ATTACK]  = LM_MOTION_DEF(9.0f, 0.75f,  1.0f),
    [LM_MOTION_VU_RELEASE] = LM_MOTION_DEF(2.4f, 1.00f,  0.0f),
    [LM_MOTION_PLATTER]    = LM_MOTION_DEF(0.6f, 1.00f,  0.0f),
};

/* ------------------------------------------------------------------ spring */
void lm_spring_init_custom(lm_spring_t *s, const lm_motion_t *m, float value, float scale)
{
    s->value = s->target = s->xp = value;
    s->vel = 0.f;
    s->m = m;
    s->eps = 0.35f / (scale > 1e-6f ? scale : 1e-6f);
    s->rest = true;
}

void lm_spring_init(lm_spring_t *s, lm_motion_id_t id, float value, float scale)
{
    lm_spring_init_custom(s, &lm_motions[id], value, scale);
}

void lm_spring_motion(lm_spring_t *s, lm_motion_id_t id) { s->m = &lm_motions[id]; }

void lm_spring_set(lm_spring_t *s, float target)
{
    if (target != s->target) {
        s->target = target;
        s->rest = false;
    }
}

void lm_spring_snap(lm_spring_t *s, float value)
{
    s->value = s->target = s->xp = value;
    s->vel = 0.f;
    s->rest = true;
}

void lm_spring_kick(lm_spring_t *s, float velocity)
{
    s->vel += velocity;
    s->rest = false;
}

void lm_spring_poke(lm_spring_t *s, float value)
{
    s->value = value;
    s->rest = false;
}

void lm_spring_update(lm_spring_t *s, float dt)
{
    if (dt <= 0.f)
        return;
    float x = s->target;
    float xd = (x - s->xp) / dt;          /* step input -> impulse through k3 */
    s->xp = x;
    if (s->rest && xd == 0.f)
        return;

    const lm_motion_t *m = s->m;
    int steps = (int)(dt * LM_SPRING_HZ + 0.999f);
    if (steps < 1) steps = 1;
    float h = dt / (float)steps;
    /* stability: never let the effective k2 fall below what explicit
     * sub-steps can integrate (see "Giving Personality to Procedural
     * Animations", t3ssel8r) */
    float k2 = m->k2;
    float k2min = lm_maxf(h * h / 2.f + h * m->k1 / 2.f, h * m->k1);
    if (k2 < k2min) k2 = k2min;
    float y = s->value, v = s->vel;
    for (int i = 0; i < steps; i++) {
        y += h * v;
        v += h * (x + m->k3 * xd - y - m->k1 * v) / k2;
    }
    s->value = y;
    s->vel = v;
    if (lm_absf(y - x) < s->eps && lm_absf(v) < s->eps * 8.f) {
        s->value = x;
        s->vel = 0.f;
        s->rest = true;
    }
}

/* ------------------------------------------------------------------ curves */
float lm_curve(lm_curve_t c, float t)
{
    if (t <= 0.f) return 0.f;
    if (t >= 1.f) return 1.f;
    const int16_t *q = lm_curve_lut[c];
    float f = t * LM_CURVE_N;
    int i = (int)f;
    float a = q[i], b = q[i + 1];
    return (a + (b - a) * (f - (float)i)) * (1.f / 4096.f);
}

static float bz(float u, float p1, float p2)
{
    float m = 1.f - u;
    return 3.f * m * m * u * p1 + 3.f * m * u * u * p2 + u * u * u;
}

float lm_bezier(float x1, float y1, float x2, float y2, float t)
{
    if (t <= 0.f) return 0.f;
    if (t >= 1.f) return 1.f;
    float u = t;
    for (int i = 0; i < 6; i++) {           /* Newton on x(u) = t */
        float m = 1.f - u;
        float dx = 3.f * m * m * x1 + 6.f * m * u * (x2 - x1) + 3.f * u * u * (1.f - x2);
        float ex = bz(u, x1, x2) - t;
        if (lm_absf(ex) < 1e-4f) break;
        if (lm_absf(dx) < 1e-5f) break;
        u = lm_clamp01(u - ex / dx);
    }
    float lo = 0.f, hi = 1.f;               /* bisection polish */
    for (int i = 0; i < 8 && lm_absf(bz(u, x1, x2) - t) > 1e-4f; i++) {
        if (bz(u, x1, x2) < t) lo = u; else hi = u;
        u = 0.5f * (lo + hi);
    }
    return bz(u, y1, y2);
}

/* ------------------------------------------------------------------ tween */
void lm_tween_start(lm_tween_t *tw, float a, float b, float dur, lm_curve_t c, float delay)
{
    tw->a = a;
    tw->b = b;
    tw->dur = dur > 1e-6f ? dur : 1e-6f;
    tw->curve = c;
    tw->t = -delay;
}

float lm_tween_value(const lm_tween_t *tw)
{
    return lm_lerpf(tw->a, tw->b, lm_curve(tw->curve, lm_tween_progress(tw)));
}
