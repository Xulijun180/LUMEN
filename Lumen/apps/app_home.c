/*
 * Home: spring carousel of app icons.
 *   position: CAROUSEL token (reacts on frame one)
 *   labels chain off the carousel *output* through FOLLOW (follow-through)
 *   page-dot pill = leading edge (SNAPPY) .. trailing edge (FOLLOW)
 *   fast motion leaves a dithered smear instead of strobing
 *   intro: icons fly in along quadratic Bezier arcs
 */
#include "apps.h"
#include "lm_fonts.h"
#include "lm_icons.h"
#include "lm_math.h"

#define N       5
#define CY      29.f
#define SPACING 40.f
#define BIG     26.f
#define SMALL   15.f
#define DOT_GAP 6.f

typedef struct { const char *label; lm_icon_fn icon; lm_page_t *(*open)(void); } item_t;

static const item_t ITEMS[N] = {
    { "Clock",    lm_icon_clock,    app_clock },
    { "Music",    lm_icon_music,    app_music },
    { "Monitor",  lm_icon_monitor,  app_monitor },
    { "Settings", lm_icon_settings, app_settings },
    { "About",    lm_icon_about,    app_about },
};

typedef struct {
    lm_page_t base;
    int sel;
    bool intro, primed;
    lm_spring_t pos, label, lead, trail, pulse, squash;
    lm_spring_t act[N];
    float phase[N];
} home_t;

static home_t s_home;

static void enter(lm_page_t *p)
{
    home_t *h = (home_t *)p;
    if (h->primed) return;             /* keep carousel position across visits */
    h->primed = true;
    h->sel = 0;
    lm_spring_init(&h->pos, LM_MOTION_CAROUSEL, 0, SPACING);
    lm_spring_init(&h->label, LM_MOTION_FOLLOW, 0, 58);
    lm_spring_init(&h->lead, LM_MOTION_SNAPPY, 0, DOT_GAP);
    lm_spring_init(&h->trail, LM_MOTION_FOLLOW, 0, DOT_GAP);
    lm_spring_init(&h->pulse, LM_MOTION_BOUNCY, 0, 3);
    lm_spring_init(&h->squash, LM_MOTION_PRESS, 0, BIG);
    for (int i = 0; i < N; i++) {
        lm_spring_init(&h->act[i], LM_MOTION_GENTLE, i == 0 ? 1.f : 0.f, 30);
        h->phase[i] = 0;
    }
}

static void resume(lm_page_t *p) { ((home_t *)p)->intro = false; }

static bool key(lm_page_t *p, lm_event_t e)
{
    home_t *h = (home_t *)p;
    if ((e.key == LM_KEY_PREV || e.key == LM_KEY_NEXT) && lm_ev_step(e)) {
        int d = e.key == LM_KEY_PREV ? -1 : 1, n = h->sel + d;
        if (n >= 0 && n < N) {
            h->sel = n;
            lm_spring_set(&h->pos, (float)n);
            lm_spring_set(&h->lead, (float)n);
            lm_spring_poke(&h->pulse, 1.f);
            for (int i = 0; i < N; i++) lm_spring_set(&h->act[i], i == n ? 1.f : 0.f);
        } else {
            /* rubber band: push past the end, the system pulls it back */
            lm_spring_kick(&h->pos, (float)d * 4.5f);
            lm_spring_kick(&h->lead, (float)d * 3.f);
            lm_spring_poke(&h->pulse, -1.2f);
            lm_haptic(p->ui);
        }
        return true;
    }
    if (e.key == LM_KEY_OK && e.kind == LM_PRESS) {
        lm_spring_poke(&h->squash, 0.18f);
        lm_push(p->ui, ITEMS[h->sel].open(), LM_TR_ZOOM);
        return true;
    }
    return false;
}

static void focus(lm_page_t *p, float r[4])
{
    (void)p;
    const float s = BIG + 8;
    r[0] = LM_W / 2.f - s / 2; r[1] = CY - s / 2; r[2] = s; r[3] = s;
}

static void update(lm_page_t *p, float dt)
{
    home_t *h = (home_t *)p;
    lm_spring_set(&h->label, h->pos.value);       /* chains */
    lm_spring_set(&h->trail, h->lead.value);
    lm_spring_t *ss[] = { &h->pos, &h->label, &h->lead, &h->trail, &h->pulse, &h->squash };
    for (unsigned i = 0; i < sizeof(ss) / sizeof(ss[0]); i++) lm_spring_update(ss[i], dt);
    for (int i = 0; i < N; i++) {
        lm_spring_update(&h->act[i], dt);
        h->phase[i] += dt * h->act[i].value;
    }
}

static float intro(home_t *h, float delay, float dur, lm_curve_t c)
{
    return h->intro ? lm_curve(c, (h->base.age - delay) / dur) : 1.f;
}

static void icon_xy(home_t *h, int i, float d, float *x, float *y, float *vis)
{
    float ad = lm_absf(d);
    *x = LM_W / 2.f + d * SPACING;
    *y = CY + lm_minf(3.f, 2.f * ad * ad);
    *vis = 1.f;
    if (!h->intro) return;
    float t = lm_clamp01((h->base.age - 0.1f - 0.075f * (float)lm_absf((float)(i - h->sel))) / 0.6f);
    if (t >= 1) return;
    float side = d < 0 ? -1.f : d > 0 ? 1.f : 0.f;
    float u = lm_curve(LM_CURVE_STANDARD, t);
    lm_qbez(LM_W / 2.f + side * 20, LM_H + 16, *x + side * 26, *y - 20, *x, *y, u, x, y);
    *vis = lm_clamp01(t * 2.5f);
}

static void brackets(lm_gfx_t *g, float cx, float cy, float r, float L)
{
    float x0 = cx - r, y0 = cy - r, x1 = cx + r - 1, y1 = cy + r - 1;
    lm_hline(g, x0, y0, L);         lm_vline(g, x0, y0, L);
    lm_hline(g, x1 - L + 1, y0, L); lm_vline(g, x1, y0, L);
    lm_hline(g, x0, y1, L);         lm_vline(g, x0, y1 - L + 1, L);
    lm_hline(g, x1 - L + 1, y1, L); lm_vline(g, x1, y1 - L + 1, L);
}

static void render(lm_page_t *p, lm_gfx_t *g)
{
    home_t *h = (home_t *)p;
    float sb = intro(h, 0.05f, 0.4f, LM_CURVE_DECEL);
    lm_gfx_off_push(g, 0, -9.f * (1 - sb));
    app_status_bar(p->ui, g, 0);
    lm_gfx_off_pop(g);

    float pos = h->pos.value;
    float vel_px = h->pos.vel * SPACING / 60.f;          /* travel per frame */
    for (int i = 0; i < N; i++) {
        float d = (float)i - pos, ad = lm_absf(d);
        if (ad > 2.2f) continue;
        float size = lm_lerpf(BIG, SMALL, lm_clamp01(ad));
        if (i == h->sel) size *= 1 - h->squash.value;
        float x, y, vis;
        icon_xy(h, i, d, &x, &y, &vis);
        float level = lm_clamp01(1 - 0.55f * lm_clamp01(ad)) * vis;
        if (lm_absf(vel_px) > 1.6f && !h->intro) {       /* motion smear */
            lm_gfx_alpha(g, level * 0.35f);
            ITEMS[i].icon(g, x + vel_px * 0.55f, y, size, h->phase[i]);
            lm_gfx_alpha(g, level * 0.15f);
            ITEMS[i].icon(g, x + vel_px * 1.1f, y, size, h->phase[i]);
        }
        lm_gfx_alpha(g, level);
        ITEMS[i].icon(g, x, y, size, h->phase[i]);
    }
    lm_gfx_alpha(g, 1);

    float e = intro(h, 0.4f, 0.5f, LM_CURVE_OVERSHOOT);
    if (e > 0) {
        float spread = 15.5f + h->pulse.value * 2.5f + lm_minf(3.f, lm_absf(h->pos.vel) * 1.2f);
        brackets(g, LM_W / 2.f, CY, spread * e * (1 - h->squash.value * 0.6f), 5);
    }

    /* labels ride the trailing `label` system and cross-dissolve */
    float lp = h->label.value, le = intro(h, 0.3f, 0.45f, LM_CURVE_DECEL);
    lm_font(g, LM_FONT_UI);
    lm_gfx_clip_push(g, 8, 46, LM_W - 16, 10);
    for (int i = 0; i < N; i++) {
        float d = (float)i - lp;
        float lvl = lm_clamp01(1 - lm_absf(d) * 1.25f) * le;
        if (lm_absf(d) > 1.2f || lvl <= 0) continue;
        lm_gfx_alpha(g, lvl);
        lm_text_center(g, LM_W / 2.f + d * 58, 47 + (1 - le) * 6, ITEMS[i].label);
    }
    lm_gfx_alpha(g, 1);
    lm_gfx_clip_pop(g);

    float de = intro(h, 0.45f, 0.4f, LM_CURVE_DECEL);
    if (de > 0) {
        float x0 = LM_W / 2.f - (N - 1) * DOT_GAP / 2;
        for (int i = 0; i < N; i++) lm_pixel(g, x0 + (float)i * DOT_GAP, 59);
        float a = lm_minf(h->lead.value, h->trail.value), b = lm_maxf(h->lead.value, h->trail.value);
        float xa = x0 + a * DOT_GAP - 2, xb = x0 + b * DOT_GAP + 2;
        lm_rbox(g, xa + 0.5f, 58, lm_maxf(4, xb - xa), 3, 1);
    }
}

static const lm_spring_t *probe(lm_page_t *p, const char **label)
{
    *label = "carousel  f=2.6 z=0.78 r=+1";
    return &((home_t *)p)->pos;
}

static const lm_page_ops_t OPS = { enter, resume, 0, key, update, render, focus, probe };

lm_page_t *app_home(bool with_intro)
{
    s_home.base.ops = &OPS;
    s_home.base.title = "Home";
    s_home.intro = with_intro;
    return &s_home.base;
}
