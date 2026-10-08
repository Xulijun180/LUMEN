/*
 * Motion primitives.
 *
 *  lm_spring_t  second-order system with a zero:
 *                  y + k1*y' + k2*y'' = x + k3*x'
 *                  k1 = 2z/w, k2 = 1/w^2, k3 = r*z/w, w = 2*pi*f
 *               f: speed, z: damping ratio, r: initial response
 *               (r>0 reacts on frame one, r<0 anticipates, r=0 plain spring)
 *
 *  lm_curve_t   CSS cubic-bezier timing curve, baked offline into a Q12 LUT
 *               (tools/gen_curves.py) so evaluation is one lookup + lerp.
 *
 *  Motion "tokens" (LM_MOTION_*) are the shared design language: widgets
 *  pick a token, never raw numbers, so the whole UI moves consistently.
 */
#ifndef LM_ANIM_H
#define LM_ANIM_H

#include <stdbool.h>
#include <stdint.h>

/* ---- second-order system ---------------------------------------------- */
typedef struct {
    float f, z, r;       /* design parameters (kept for tooling/debug)     */
    float k1, k2, k3;    /* derived coefficients                           */
} lm_motion_t;

/* Build a motion at compile time: the result lives in flash. */
#define LM_MOTION_DEF(F, Z, R) \
    { (F), (Z), (R), \
      2.f * (Z) / (6.2831853f * (F)), \
      1.f / ((6.2831853f * (F)) * (6.2831853f * (F))), \
      (R) * (Z) / (6.2831853f * (F)) }

typedef enum {
    LM_MOTION_SNAPPY,     /* cursors, underlines: 90% in ~85 ms            */
    LM_MOTION_CAROUSEL,   /* heavier content, 4% overshoot                 */
    LM_MOTION_SMOOTH,     /* scrolling, paging: critically damped          */
    LM_MOTION_FOLLOW,     /* secondary elements trailing a primary one     */
    LM_MOTION_BOUNCY,     /* playful feedback, one visible bounce          */
    LM_MOTION_TOGGLE,     /* switch knob: leaps, overshoots, settles       */
    LM_MOTION_GENTLE,     /* physical inertia (brightness, dimming)        */
    LM_MOTION_ZOOM,       /* confirmed navigation: anticipation then launch*/
    LM_MOTION_PRESS,      /* press squash that rings once                  */
    LM_MOTION_WOBBLE,     /* boundary shake                                */
    LM_MOTION_EXIT,       /* leave the screen: lift slightly, then go      */
    LM_MOTION_VU_ATTACK,  /* meter ballistics: fast rise                   */
    LM_MOTION_VU_RELEASE, /*                  slow fall                    */
    LM_MOTION_PLATTER,    /* heavy motor-driven platter                    */
    LM_MOTION_COUNT
} lm_motion_id_t;

extern const lm_motion_t lm_motions[LM_MOTION_COUNT];

typedef struct {
    float value;          /* output y                                       */
    float vel;            /* y'                                             */
    float target;         /* input x                                        */
    float xp;             /* previous input, for x'                         */
    float eps;            /* rest threshold in value units                  */
    const lm_motion_t *m;
    bool  rest;
} lm_spring_t;

/* scale: how many screen pixels one unit of `value` spans. The spring snaps
 * to rest once its residual is below ~0.35 px, which removes the 1 px
 * flicker a damped tail would otherwise cause on a 1-bit panel. */
void  lm_spring_init(lm_spring_t *s, lm_motion_id_t id, float value, float scale);
void  lm_spring_init_custom(lm_spring_t *s, const lm_motion_t *m, float value, float scale);
void  lm_spring_motion(lm_spring_t *s, lm_motion_id_t id);   /* swap dynamics, keep state */
void  lm_spring_set(lm_spring_t *s, float target);
void  lm_spring_snap(lm_spring_t *s, float value);
void  lm_spring_kick(lm_spring_t *s, float velocity);        /* impulse                    */
void  lm_spring_poke(lm_spring_t *s, float value);           /* displace output, not target*/
void  lm_spring_update(lm_spring_t *s, float dt);
static inline bool lm_spring_rest(const lm_spring_t *s) { return s->rest; }

/* ---- bezier timing curves ------------------------------------------- */
typedef enum {
    LM_CURVE_STANDARD,    /* cubic-bezier(0.2, 0, 0, 1)      move / enter   */
    LM_CURVE_DECEL,       /* cubic-bezier(0.05, 0.7, 0.1, 1) arrive         */
    LM_CURVE_ACCEL,       /* cubic-bezier(0.3, 0, 0.8, 0.15) leave          */
    LM_CURVE_OVERSHOOT,   /* cubic-bezier(0.34, 1.45, 0.64, 1) small pops   */
    LM_CURVE_INOUT,       /* cubic-bezier(0.65, 0, 0.35, 1)  sweeps         */
    LM_CURVE_LINEAR,
    LM_CURVE_COUNT
} lm_curve_t;

float lm_curve(lm_curve_t c, float t);

/* Arbitrary curve evaluated at runtime (Newton on x(u)); use when a
 * design needs a one-off curve that isn't worth a baked table. */
float lm_bezier(float x1, float y1, float x2, float y2, float t);

/* quadratic Bezier point, for arc motion paths */
static inline void lm_qbez(float x0, float y0, float x1, float y1, float x2, float y2,
                           float t, float *ox, float *oy)
{
    float m = 1.f - t;
    *ox = m * m * x0 + 2.f * m * t * x1 + t * t * x2;
    *oy = m * m * y0 + 2.f * m * t * y1 + t * t * y2;
}

/* ---- tween --------------------------------------------------------------- */
typedef struct {
    float a, b, t, dur;
    lm_curve_t curve;
} lm_tween_t;

void  lm_tween_start(lm_tween_t *tw, float a, float b, float dur, lm_curve_t c, float delay);
static inline void lm_tween_update(lm_tween_t *tw, float dt) { tw->t += dt; }
float lm_tween_value(const lm_tween_t *tw);
static inline bool lm_tween_done(const lm_tween_t *tw) { return tw->t >= tw->dur; }
static inline float lm_tween_progress(const lm_tween_t *tw)
{
    float p = tw->t / tw->dur;
    return p < 0 ? 0 : p > 1 ? 1 : p;
}

#endif
