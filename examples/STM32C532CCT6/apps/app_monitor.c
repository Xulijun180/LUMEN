/* System monitor: tabbed live graph. Each of the NP points is its own
 * second-order system whose frequency falls off towards older samples, so a
 * tab switch sweeps across the curve like a wave instead of cutting. */
#include <stdio.h>
#include <string.h>

#include "apps.h"
#include "lm_fonts.h"
#include "lm_math.h"
#include "lm_widgets.h"

#define NP 34
#define STEP 4.f
#define SAMPLE 0.25f
#define NTAB 3
static const char *TABS[NTAB] = { "CPU", "RAM", "TEMP" };

typedef struct {
    lm_page_t base;
    int tab;
    bool paused;
    float t, acc;
    float hist[NTAB][NP];
    float y[NP], v[NP], xp[NP];
    lm_spring_t ul_x, ul_w, badge;
    lm_tween_t reveal;
    lm_roll_t value;
    lm_rng_t rng;
    lm_spring_t probe_pt;      /* mirrors the middle point for tooling */
} mon_t;

static float sample(mon_t *m, int k, float t)
{
    float n = lm_rng_f(&m->rng);
    if (k == 0) return lm_clampf(0.42f + 0.22f * lm_sinf(t * 1.9f) + 0.18f * lm_sinf(t * 5.3f) + 0.12f * (n - 0.5f), 0.04f, 0.97f);
    if (k == 1) return lm_clamp01(0.62f + 0.06f * lm_sinf(t * 0.7f) + 0.03f * (n - 0.5f));
    return lm_clamp01(0.3f + 0.08f * lm_sinf(t * 0.9f) + 0.05f * lm_sinf(t * 3.1f) + 0.02f * (n - 0.5f));
}

static void fmt(mon_t *m, char *b, float v)
{
    if (m->tab == 2) snprintf(b, 5, "%2dC", (int)(28 + v * 50));
    else snprintf(b, 5, "%2d%%", (int)(v * 100));
}

static float tab_x(lm_gfx_t *g, int i)
{
    lm_font(g, LM_FONT_TINY);
    float x = 3;
    for (int j = 0; j < i; j++) x += (float)lm_text_w(g, TABS[j]) + 7;
    return x;
}

static void place_ul(mon_t *m, bool snap)
{
    lm_gfx_t *g = &m->base.ui->gfx;
    float x = tab_x(g, m->tab) - 2, w = (float)lm_text_w(g, TABS[m->tab]) + 4;
    if (snap) { lm_spring_snap(&m->ul_x, x); lm_spring_snap(&m->ul_w, w); }
    lm_spring_set(&m->ul_x, x);
    lm_spring_set(&m->ul_w, w);
}

static void enter(lm_page_t *p)
{
    mon_t *m = (mon_t *)p;
    m->tab = 0; m->paused = false; m->t = m->acc = 0;
    m->rng.s = 3;
    for (int k = 0; k < NTAB; k++)
        for (int i = 0; i < NP; i++) m->hist[k][i] = sample(m, k, -(float)(NP - 1 - i) * SAMPLE);
    memcpy(m->y, m->hist[0], sizeof(m->y));
    memcpy(m->xp, m->hist[0], sizeof(m->xp));
    memset(m->v, 0, sizeof(m->v));
    lm_spring_init(&m->ul_x, LM_MOTION_SNAPPY, 0, 1);
    lm_spring_init(&m->ul_w, LM_MOTION_FOLLOW, 0, 1);   /* width lags: underline stretches */
    lm_spring_init(&m->badge, LM_MOTION_BOUNCY, 0, 15);
    lm_spring_init(&m->probe_pt, LM_MOTION_SMOOTH, 0, 40);
    lm_tween_start(&m->reveal, 0, 1, 0.7f, LM_CURVE_STANDARD, 0.15f);
    char b[5]; fmt(m, b, m->hist[0][NP - 1]);
    lm_roll_init(&m->value, LM_FONT_MID, b, 0.3f, 0.04f);
    place_ul(m, true);
}

static bool key(lm_page_t *p, lm_event_t e)
{
    mon_t *m = (mon_t *)p;
    if ((e.key == LM_KEY_PREV || e.key == LM_KEY_NEXT) && lm_ev_step(e)) {
        int d = e.key == LM_KEY_PREV ? -1 : 1, n = m->tab + d;
        if (n >= 0 && n < NTAB) {
            m->tab = n;
            place_ul(m, false);
            char b[5]; fmt(m, b, m->hist[n][NP - 1]);
            lm_roll_set(&m->value, b, d);
        } else {
            lm_spring_kick(&m->ul_x, (float)d * 60.f);
            lm_haptic(p->ui);
        }
        return true;
    }
    if (e.key == LM_KEY_OK && e.kind == LM_PRESS) {
        m->paused = !m->paused;
        lm_spring_set(&m->badge, m->paused ? 1.f : 0.f);
        return true;
    }
    return false;
}

static void update(lm_page_t *p, float dt)
{
    mon_t *m = (mon_t *)p;
    lm_spring_update(&m->ul_x, dt);
    lm_spring_update(&m->ul_w, dt);
    lm_spring_update(&m->badge, dt);
    lm_tween_update(&m->reveal, dt);
    lm_roll_update(&m->value, dt);
    if (!m->paused) {
        m->t += dt;
        m->acc += dt;
        while (m->acc >= SAMPLE) {
            m->acc -= SAMPLE;
            for (int k = 0; k < NTAB; k++) {
                memmove(m->hist[k], m->hist[k] + 1, sizeof(float) * (NP - 1));
                m->hist[k][NP - 1] = sample(m, k, m->t);
            }
            /* shift the systems with the data; re-base x' so a sample
             * shift is not seen as an input step */
            memmove(m->y, m->y + 1, sizeof(float) * (NP - 1));
            memmove(m->v, m->v + 1, sizeof(float) * (NP - 1));
            m->v[NP - 1] = 0;
            memcpy(m->xp, m->hist[m->tab], sizeof(m->xp));
            char b[5]; fmt(m, b, m->hist[m->tab][NP - 1]);
            lm_roll_set(&m->value, b, 1);
        }
    }
    if (dt <= 0) return;
    const float z = 0.72f, r = 0.5f;
    int steps = (int)(dt * LM_SPRING_HZ + 0.999f);
    float h = dt / (float)(steps < 1 ? 1 : steps);
    for (int i = 0; i < NP; i++) {
        float x = m->hist[m->tab][i];
        float xd = (x - m->xp[i]) / dt;
        m->xp[i] = x;
        float w = LM_TAU * lm_lerpf(1.6f, 3.4f, (float)i / (NP - 1));
        float k1 = 2 * z / w, k2 = 1 / (w * w), k3 = r * z / w;
        float y = m->y[i], v = m->v[i];
        for (int s = 0; s < steps; s++) { y += h * v; v += h * (x + k3 * xd - y - k1 * v) / k2; }
        m->y[i] = y; m->v[i] = v;
    }
    m->probe_pt.value = m->y[NP / 2];
    m->probe_pt.target = m->hist[m->tab][NP / 2];
}

static void render(lm_page_t *p, lm_gfx_t *g)
{
    mon_t *m = (mon_t *)p;
    for (int i = 0; i < NTAB; i++) { float x = tab_x(g, i); lm_text(g, x, 2, TABS[i]); }
    lm_box(g, m->ul_x.value, 9, m->ul_w.value, 1);
    float vx = LM_W - 3 - (float)lm_roll_width(&m->value, g);
    lm_roll_draw(&m->value, g, vx, 1);

    const float gy0 = 20, gh = 42;
    for (int f = 1; f <= 3; f++)
        lm_dotted_hline(g, (float)((int)(m->t * 8) % 4), gy0 + gh * (1 - 0.25f * (float)f), LM_W, 4);
    float rv = lm_tween_value(&m->reveal);
    float scroll = m->paused ? 0 : (m->acc / SAMPLE) * STEP;
    float xs[NP], ys[NP];
    for (int i = 0; i < NP; i++) {
        xs[i] = (float)i * STEP - scroll + (LM_W - (NP - 1) * STEP);
        ys[i] = gy0 + gh - 2 - m->y[i] * (gh - 6);
    }
    lm_gfx_clip_push(g, 0, gy0, LM_W * rv, gh + 1);
    /* ~30% dithered fill under the curve, then the stroke */
    lm_gfx_alpha(g, 0.3f);
    int j = 0;
    for (int x = 0; x < LM_W; x++) {
        float px = (float)x;
        while (j < NP - 2 && xs[j + 1] < px) j++;
        float t = lm_clamp01((px - xs[j]) / (xs[j + 1] - xs[j]));
        float yc = lm_lerpf(ys[j], ys[j + 1], t);
        float top = (float)(int)yc + 1;
        lm_vline(g, px, top + 1, gy0 + gh - top - 1);
    }
    lm_gfx_alpha(g, 1);
    for (int i = 0; i < NP - 1; i++) lm_line(g, xs[i], ys[i], xs[i + 1], ys[i + 1]);
    lm_hline(g, 0, gy0 + gh, LM_W);
    float hx = xs[NP - 1] - 1, hy = ys[NP - 1];
    lm_gfx_mode(g, LM_CLEAR); lm_disc(g, hx, hy, 2.3f);
    lm_gfx_mode(g, LM_SET);   lm_ring(g, hx, hy, 2.3f, 1);
    lm_gfx_clip_pop(g);
    float b = m->badge.value;
    if (b > 0.02f) {
        const float bw = 34;
        float by = lm_lerpf(gy0 - 12, gy0 + 3, b);
        lm_rbox(g, LM_W / 2.f - bw / 2, by, bw, 9, 3);
        lm_gfx_mode(g, LM_CLEAR);
        lm_font(g, LM_FONT_TINY);
        lm_text_center(g, LM_W / 2.f + 0.5f, by + 2, "PAUSED");
        lm_gfx_mode(g, LM_SET);
    }
}

static const lm_spring_t *probe(lm_page_t *p, const char **label)
{
    *label = "graph point  f=1.6..3.4 z=0.72 r=+0.5 x34";
    return &((mon_t *)p)->probe_pt;
}

static const lm_page_ops_t OPS = { enter, 0, 0, key, update, render, 0, probe };
static mon_t s_mon = { { &OPS, "Monitor", 0, 0 } };

lm_page_t *app_monitor(void) { return &s_mon.base; }
