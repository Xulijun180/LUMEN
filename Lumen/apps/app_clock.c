/* Clock with odometer digits + a stopwatch face, paged horizontally. */
#include <stdio.h>

#include "apps.h"
#include "lm_fonts.h"
#include "lm_math.h"
#include "lm_widgets.h"

static const char *DAYS[] = { "MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN" };
static const char *MONTHS[] = { "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                "JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };

typedef struct {
    lm_page_t base;
    int face;
    bool running;
    float sw_t;
    lm_spring_t pager, hint;
    lm_roll_t hh, mm, ss, sw_m, sw_s;
} clock_t_;

static void two(char *b, int v)
{
    unsigned u = (unsigned)(v < 0 ? 0 : v) % 100u;
    b[0] = (char)('0' + u / 10); b[1] = (char)('0' + u % 10); b[2] = 0;
}

static void enter(lm_page_t *p)
{
    clock_t_ *c = (clock_t_ *)p;
    lm_datetime_t n; lm_now(p->ui, &n);
    char b[3];
    c->face = 0;
    lm_spring_init(&c->pager, LM_MOTION_CAROUSEL, 0, LM_W);
    lm_spring_init(&c->hint, LM_MOTION_BOUNCY, 0, 2);
    two(b, n.hour); lm_roll_init(&c->hh, LM_FONT_BIG, b, 0.55f, 0.09f);
    two(b, n.min);  lm_roll_init(&c->mm, LM_FONT_BIG, b, 0.55f, 0.09f);
    two(b, n.sec);  lm_roll_init(&c->ss, LM_FONT_MID, b, 0.32f, 0.05f);
    int m = (int)c->sw_t / 60, s = (int)c->sw_t % 60;
    two(b, m); lm_roll_init(&c->sw_m, LM_FONT_BIG, b, 0.4f, 0.07f);
    two(b, s); lm_roll_init(&c->sw_s, LM_FONT_BIG, b, 0.4f, 0.07f);
}

static void reset_sw(clock_t_ *c)
{
    c->running = false;
    c->sw_t = 0;
    lm_roll_set(&c->sw_m, "00", -1);       /* digits roll back down */
    lm_roll_set(&c->sw_s, "00", -1);
    lm_spring_poke(&c->hint, 1.f);
}

static bool key(lm_page_t *p, lm_event_t e)
{
    clock_t_ *c = (clock_t_ *)p;
    if ((e.key == LM_KEY_PREV || e.key == LM_KEY_NEXT) && lm_ev_step(e)) {
        int d = e.key == LM_KEY_PREV ? -1 : 1, n = c->face + d;
        if (n >= 0 && n <= 1) { c->face = n; lm_spring_set(&c->pager, (float)n); }
        else if (c->face == 1 && d > 0 && !c->running && c->sw_t > 0 && e.kind == LM_PRESS) {
            reset_sw(c);       /* NEXT past the last face while paused: reset (any layout) */
        } else { lm_spring_kick(&c->pager, (float)d * 2.2f); lm_haptic(p->ui); }
        return true;
    }
    if (e.key == LM_KEY_OK) {
        if (c->face != 1) return true;
        if (e.kind == LM_LONG) {                         /* 4-key: hold OK also resets */
            reset_sw(c);
        } else {
            c->running = !c->running;
        }
        lm_spring_poke(&c->hint, 1.f);
        return true;
    }
    return false;
}

static void update(lm_page_t *p, float dt)
{
    clock_t_ *c = (clock_t_ *)p;
    lm_datetime_t n; lm_now(p->ui, &n);
    char b[3];
    two(b, n.hour); lm_roll_set(&c->hh, b, 1);
    two(b, n.min);  lm_roll_set(&c->mm, b, 1);
    two(b, n.sec);  lm_roll_set(&c->ss, b, 1);
    if (c->running) c->sw_t += dt * 100.f / (float)lm_maxi(1, (int)lm_setting(p->ui, LM_S_MOTION));
    two(b, (int)c->sw_t / 60); lm_roll_set(&c->sw_m, b, 1);
    two(b, (int)c->sw_t % 60); lm_roll_set(&c->sw_s, b, 1);
    lm_roll_t *rs[] = { &c->hh, &c->mm, &c->ss, &c->sw_m, &c->sw_s };
    for (int i = 0; i < 5; i++) lm_roll_update(rs[i], dt);
    lm_spring_update(&c->pager, dt);
    lm_spring_update(&c->hint, dt);
}

/* HH:MM (+ small trailing field), centred; returns x after the big part */
static float big_time(lm_gfx_t *g, lm_roll_t *a, lm_roll_t *b, float y, float colon, float extra)
{
    float wa = (float)lm_roll_width(a, g), wb = (float)lm_roll_width(b, g), cw = 8;
    lm_font(g, LM_FONT_BIG);
    float bh = (float)lm_font_h(g);
    float x = LM_W / 2.f - (wa + cw + wb + extra) / 2.f;
    lm_roll_draw(a, g, x, y);
    float cx = x + wa + cw / 2;
    lm_gfx_alpha(g, colon);
    lm_rbox(g, cx - 2, y + bh * 0.28f - 2, 4, 4, 1);   /* square dots match logisoso */
    lm_rbox(g, cx - 2, y + bh * 0.70f - 2, 4, 4, 1);
    lm_gfx_alpha(g, 1);
    lm_roll_draw(b, g, x + wa + cw, y);
    return x + wa + cw + wb;
}

static void face_clock(clock_t_ *c, lm_gfx_t *g)
{
    lm_datetime_t n; lm_now(c->base.ui, &n);
    char date[16];
    snprintf(date, sizeof(date), "%s %02d %s", DAYS[n.wday % 7], n.day, MONTHS[(n.month + 11) % 12]);
    lm_font(g, LM_FONT_TINY);
    lm_text_center(g, LM_W / 2.f, 3, date);
    float frac = (float)n.ms / 1000.f;
    float colon = 0.25f + 0.75f * (0.5f + 0.5f * lm_cosf(frac * LM_TAU));
    lm_font(g, LM_FONT_MID);
    float extra = (float)lm_text_w(g, "00") + 3;
    const float y = 13;
    float ex = big_time(g, &c->hh, &c->mm, y, colon, extra);
    lm_font(g, LM_FONT_BIG);
    float bh = (float)lm_font_h(g);
    lm_font(g, LM_FONT_MID);
    lm_roll_draw(&c->ss, g, ex + 3, y + bh - (float)lm_font_h(g));
    /* seconds ruler: passed ticks tall, the live one grows with overshoot */
    float sec = (float)n.sec + frac;
    const float x0 = LM_W / 2.f - 59;
    for (int i = 0; i < 60; i++) {
        float h;
        if (i < (int)sec) h = i % 5 ? 3.f : 5.f;
        else if (i == (int)sec) h = 1 + lm_curve(LM_CURVE_OVERSHOOT, sec - (float)i) * (i % 5 ? 2.f : 4.f);
        else h = i % 5 ? 1.f : 2.f;
        lm_vline(g, x0 + (float)i * 2, 56 - (float)lm_roundi(h), (float)lm_roundi(h));
    }
}

static void face_stopwatch(clock_t_ *c, lm_gfx_t *g)
{
    lm_font(g, LM_FONT_TINY);
    lm_text_center(g, LM_W / 2.f, 3, "STOPWATCH");
    lm_font(g, LM_FONT_MID);
    float extra = (float)lm_text_w(g, "00") + 3;
    const float y = 13;
    float ex = big_time(g, &c->sw_m, &c->sw_s, y, 1.f, extra);
    char cs[4];
    two(cs, (int)((c->sw_t - (float)(int)c->sw_t) * 100.f));
    lm_font(g, LM_FONT_BIG);
    float bh = (float)lm_font_h(g);
    lm_font(g, LM_FONT_MID);
    lm_text(g, ex + 3, y + bh - (float)lm_font_h(g), cs);
    const float cx = LM_W / 2.f - 34, cy = 51.5f;
    if (c->running) lm_disc(g, cx, cy, 2.2f + 0.6f * lm_sinf(c->base.age * 8));
    else lm_ring(g, cx, cy, 2.6f, 1.f);
    const char *hint = c->running ? "OK PAUSE" : c->sw_t > 0 ? "OK RESUME  DOWN RESET" : "OK START";
    lm_font(g, LM_FONT_TINY);
    lm_text(g, cx + 6, 49 - 2 * c->hint.value, hint);
}

static void render(lm_page_t *p, lm_gfx_t *g)
{
    clock_t_ *c = (clock_t_ *)p;
    float off = -c->pager.value * LM_W;
    lm_gfx_off_push(g, off, 0);      face_clock(c, g);     lm_gfx_off_pop(g);
    lm_gfx_off_push(g, off + LM_W, 0); face_stopwatch(c, g); lm_gfx_off_pop(g);
    for (int i = 0; i < 2; i++) lm_pixel(g, LM_W / 2.f - 3 + (float)i * 6, 62);
    lm_rbox(g, LM_W / 2.f - 3 + c->pager.value * 6 - 1.5f + 0.5f, 61, 4, 3, 1);
}

static const lm_spring_t *probe(lm_page_t *p, const char **label)
{
    *label = "pager  carousel";
    return &((clock_t_ *)p)->pager;
}

static const lm_page_ops_t OPS = { enter, 0, 0, key, update, render, 0, probe };
static clock_t_ s_clock = { { &OPS, "Clock", 0, 0 } };

lm_page_t *app_clock(void) { return &s_clock.base; }
