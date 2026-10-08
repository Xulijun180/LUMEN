/* Boot splash: CRT flash -> letters thrown along arcs -> underline ->
 * typewriter subtitle -> shine -> loader -> dissolve to home. */
#include <string.h>

#include "apps.h"
#include "lm_fonts.h"
#include "lm_math.h"

/* The choreography below is authored on a 3.35 s timeline; APP_BOOT_SPEED
 * plays it faster (1.6 -> ~2.1 s to home). Any key skips it. */
#ifndef APP_BOOT_SPEED
#define APP_BOOT_SPEED 0.8f
#endif
#define DONE_AT 3.35f
static const char WORD[] = "LUMEN";
static const char SUB[] = "MOTION UI FRAMEWORK";

typedef struct { lm_page_t base; bool handed; } boot_t;

static void hand_off(lm_page_t *p)
{
    boot_t *b = (boot_t *)p;
    if (b->handed) return;
    b->handed = true;
    lm_replace(p->ui, app_home(true), LM_TR_FADE);
}

static void enter(lm_page_t *p) { ((boot_t *)p)->handed = false; }

static bool key(lm_page_t *p, lm_event_t e)
{
    if (e.kind == LM_PRESS) hand_off(p);    /* skip the splash */
    return true;
}

static void update(lm_page_t *p, float dt)
{
    (void)dt;
    if (p->age * APP_BOOT_SPEED >= DONE_AT) hand_off(p);
}

static void render(lm_page_t *p, lm_gfx_t *g)
{
    float t = p->age * APP_BOOT_SPEED;
    if (t < 0.95f) {                                  /* flash dissolving away */
        lm_box(g, 0, 0, LM_W, LM_H);
        lm_buf_fade(g->target, lm_remap(t, 0.55f, 0.95f, 1.f, 0.f));
        if (t < 0.6f) return;
    }
    lm_font(g, LM_FONT_LOGO);
    int fh = lm_font_h(g);
    int widths[5], total = 0;
    for (int i = 0; i < 5; i++) { widths[i] = lm_char_w(g, WORD[i]); total += widths[i]; }
    float x = LM_W / 2.f - (float)total / 2.f;
    const float base_y = 11;
    char s[2] = {0, 0};
    for (int i = 0; i < 5; i++) {
        /* thrown from below-centre along a quadratic Bezier that crests
         * above its slot, lands with a small settle hop */
        float q = lm_clamp01((t - 0.72f - (float)i * 0.07f) / 0.55f);
        if (q > 0) {
            float x0 = LM_W / 2.f - (float)widths[i] / 2.f, y0 = LM_H + 2;
            float cx = lm_lerpf(x0, x, 1.25f), cy = base_y - 22;
            float px, py;
            lm_qbez(x0, y0, cx, cy, x, base_y, lm_curve(LM_CURVE_DECEL, q), &px, &py);
            py += -2.5f * lm_sinf(LM_PI * lm_clamp01((q - 0.7f) / 0.3f));
            s[0] = WORD[i];
            lm_text(g, px, py, s);
        }
        x += (float)widths[i];
    }
    float ly = base_y + (float)fh + 3;
    float u = lm_curve(LM_CURVE_STANDARD, (t - 1.35f) / 0.5f);
    if (u > 0) lm_hline(g, LM_W / 2.f - (float)total / 2.f * u, ly, (float)total * u);
    if (t > 1.6f) {
        lm_font(g, LM_FONT_TINY);
        int k = (int)(lm_clamp01((t - 1.65f) / 0.55f) * (float)(sizeof(SUB) - 1));
        char buf[sizeof(SUB)];
        memcpy(buf, SUB, (size_t)k); buf[k] = 0;
        float sx = LM_W / 2.f - (float)lm_text_w(g, SUB) / 2.f;
        int ex = lm_text(g, sx, ly + 5, buf);
        if ((k < (int)sizeof(SUB) - 1 || (int)(t * 4) % 2 == 0) && t < 2.8f)
            lm_box(g, (float)ex, ly + 5, 3, (float)lm_font_h(g));
    }
    float sh = lm_clamp01((t - 2.05f) / 0.45f);        /* diagonal glint through the letters */
    if (sh > 0 && sh < 1) {
        float cx = lm_lerpf(LM_W / 2.f - total / 2.f - 20, LM_W / 2.f + total / 2.f + 20, lm_curve(LM_CURVE_INOUT, sh));
        lm_gfx_clip_push(g, 0, base_y, LM_W, (float)fh);
        lm_gfx_mode(g, LM_CLEAR);   /* XOR would light the background too at this glyph size */
        for (int k = 0; k < 5; k++) lm_line(g, cx + k - 8, base_y + fh, cx + k + 8, base_y);
        lm_gfx_mode(g, LM_SET);
        lm_gfx_clip_pop(g);
    }
    float lp = lm_clamp01((t - 2.25f) / 0.85f);
    if (lp > 0) {
        const float bw = 40, bx = LM_W / 2.f - bw / 2.f;
        float e = lp < .5f ? 4 * lp * lp * lp : 1 - (-2 * lp + 2) * (-2 * lp + 2) * (-2 * lp + 2) / 2;
        lm_rframe(g, bx, 56, bw, 4, 2);
        lm_hline(g, bx + 1, 57, (bw - 2) * e);
        lm_hline(g, bx + 1, 58, (bw - 2) * e);
    }
}

static const lm_page_ops_t OPS = { enter, 0, 0, key, update, render, 0, 0 };
static boot_t s_boot = { { &OPS, "Boot", 0, 0 }, false };

lm_page_t *app_boot(void) { return &s_boot.base; }
