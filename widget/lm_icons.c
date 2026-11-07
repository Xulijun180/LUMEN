#include "lm_icons.h"

#include "lm_math.h"

static float stroke(float s) { return lm_maxf(1.f, s / 11.f); }

void lm_icon_clock(lm_gfx_t *g, float cx, float cy, float s, float ph)
{
    float r = s * 0.46f;
    lm_ring(g, cx, cy, r, stroke(s) * 1.1f);
    for (int i = 0; i < 12; i++) {
        if (s < 18 && i % 3) continue;
        float a = (float)i * LM_PI / 6.f;
        float r0 = r - (i % 3 == 0 ? s * 0.12f : s * 0.07f);
        lm_pixel(g, cx + lm_sinf(a) * r0 - 0.5f, cy - lm_cosf(a) * r0 - 0.5f);
    }
    float am = 1.05f + ph * LM_TAU * 0.35f;              /* minute hand sweeps */
    float ah = -0.95f + ph * LM_TAU * 0.35f / 12.f;
    lm_capsule(g, cx, cy, cx + lm_sinf(am) * r * 0.72f, cy - lm_cosf(am) * r * 0.72f, stroke(s));
    lm_capsule(g, cx, cy, cx + lm_sinf(ah) * r * 0.48f, cy - lm_cosf(ah) * r * 0.48f, stroke(s) * 1.3f);
    lm_disc(g, cx, cy, lm_maxf(1.f, s * 0.06f));
}

void lm_icon_music(lm_gfx_t *g, float cx, float cy, float s, float ph)
{
    float b1 = lm_absf(lm_sinf(ph * LM_TAU * 0.9f)) * s * 0.08f;         /* notes bob */
    float b2 = lm_absf(lm_sinf(ph * LM_TAU * 0.9f + 1.3f)) * s * 0.08f;
    float hr = s * 0.13f, t = stroke(s);
    float x1 = cx - s * 0.2f, y1 = cy + s * 0.25f - b1;
    float x2 = cx + s * 0.24f, y2 = cy + s * 0.17f - b2;
    lm_disc(g, x1, y1, hr * 1.1f);
    lm_disc(g, x2, y2, hr * 1.1f);
    float t1 = cy - s * 0.3f - b1, t2 = cy - s * 0.38f - b2;
    lm_capsule(g, x1 + hr * 0.9f, y1, x1 + hr * 0.9f, t1, t);
    lm_capsule(g, x2 + hr * 0.9f, y2, x2 + hr * 0.9f, t2, t);
    lm_capsule(g, x1 + hr * 0.9f, t1, x2 + hr * 0.9f, t2, t * 2.f);
}

void lm_icon_monitor(lm_gfx_t *g, float cx, float cy, float s, float ph)
{
    static const float base[4] = { 0.45f, 0.8f, 0.55f, 0.95f };
    float w = s * 0.92f, h = s * 0.78f, x0 = cx - w / 2, y0 = cy - h / 2;
    lm_rframe(g, x0, y0, w, h, lm_maxf(1.f, s * 0.1f));
    float bw = (w - s * 0.28f) / 4.f;
    for (int i = 0; i < 4; i++) {
        float lv = base[i];
        if (ph != 0.f) {
            lv = 0.35f + 0.3f * lm_sinf(ph * 5 + (float)i * 1.7f) + 0.2f * lm_sinf(ph * 2.3f + (float)i) + 0.3f;
            lv = lm_clampf(lv, 0.15f, 0.95f);
        }
        float bh = (h - s * 0.26f) * lv;
        lm_box(g, x0 + s * 0.14f + (float)i * bw + 0.5f, y0 + h - s * 0.13f - bh,
               lm_maxf(1.f, bw - lm_maxf(1.f, s * 0.06f)), bh);
    }
}

void lm_icon_settings(lm_gfx_t *g, float cx, float cy, float s, float ph)
{
    float rot = ph * 1.6f, ro = s * 0.47f, ri = s * 0.35f;
    float pts[64];
    for (int i = 0; i < 32; i++) {
        float a = rot + (float)i * LM_TAU / 32.f;
        int k = i % 4;
        float r = (k == 1 || k == 2) ? ro : ri;
        pts[2 * i] = cx + lm_sinf(a) * r;
        pts[2 * i + 1] = cy - lm_cosf(a) * r;
    }
    lm_poly(g, pts, 32);
    lm_mode_t m = g->mode;
    lm_gfx_mode(g, LM_CLEAR);
    lm_disc(g, cx, cy, s * 0.2f);
    lm_gfx_mode(g, m);
    lm_disc(g, cx, cy, s * 0.08f);
}

void lm_icon_about(lm_gfx_t *g, float cx, float cy, float s, float ph)
{
    float r = s * 0.46f;
    lm_ring(g, cx, cy, r, stroke(s) * 1.1f);
    float bob = lm_sinf(ph * 4) * s * 0.04f;
    lm_disc(g, cx, cy - s * 0.2f + bob, lm_maxf(1.f, s * 0.075f));
    lm_capsule(g, cx, cy - s * 0.02f, cx, cy + s * 0.26f, stroke(s) * 1.5f);
}
