/* Music player: spinning record with inertia, morphing play/pause, VU-style
 * spectrum (fast attack, slow release) with falling peak caps. */
#include <stdio.h>

#include "apps.h"
#include "lm_fonts.h"
#include "lm_math.h"
#include "lm_widgets.h"

#define NB 13

typedef struct { const char *title, *artist; int dur; } track_t;
static const track_t TRACKS[] = {
    { "Neon Horizon", "SYNTHWAVE FM", 214 },
    { "Midnight Circuit", "PIXEL DRIVE", 187 },
    { "Glass Garden", "LOFI ROOM", 243 },
};
#define NT ((int)(sizeof(TRACKS) / sizeof(TRACKS[0])))

typedef struct {
    lm_page_t base;
    int idx, dir, prev_idx;
    bool playing;
    float pos, angle, tick, peaks[NB], peak_v[NB];
    lm_spring_t spin, morph, slide, flash[3], bars[NB];
    const lm_spring_t *probe_s;
    const char *probe_l;
    lm_marquee_t title;
    lm_rng_t rng;
} music_t;

static void enter(lm_page_t *p)
{
    music_t *m = (music_t *)p;
    m->idx = 0; m->dir = 1; m->prev_idx = -1;
    m->playing = false;
    m->pos = m->angle = m->tick = 0;
    lm_spring_init(&m->spin, LM_MOTION_PLATTER, 0, 100);
    lm_spring_init(&m->morph, LM_MOTION_TOGGLE, 0, 8);
    lm_spring_init(&m->slide, LM_MOTION_CAROUSEL, 0, 40);
    for (int i = 0; i < 3; i++) lm_spring_init(&m->flash[i], LM_MOTION_PRESS, 0, 4);
    for (int i = 0; i < NB; i++) { lm_spring_init(&m->bars[i], LM_MOTION_VU_ATTACK, 0, 16); m->peaks[i] = m->peak_v[i] = 0; }
    lm_marquee_init(&m->title, LM_FONT_UI, TRACKS[0].title, 78);
    m->rng.s = 7;
    m->probe_s = &m->morph;
    m->probe_l = "play/pause morph  toggle";
}

static void change(music_t *m, int d)
{
    m->prev_idx = m->idx;
    m->idx = (m->idx + d + NT) % NT;
    lm_marquee_set(&m->title, TRACKS[m->idx].title);
    m->dir = d;
    lm_spring_snap(&m->slide, 1.f);
    lm_spring_set(&m->slide, 0.f);
    m->pos = 0;
    lm_spring_kick(&m->spin, (float)d * 6.f);     /* scratch */
    lm_spring_poke(&m->flash[d < 0 ? 0 : 2], 1.f);
    m->probe_s = &m->slide;
    m->probe_l = "title slide  carousel";
}

static bool key(lm_page_t *p, lm_event_t e)
{
    music_t *m = (music_t *)p;
    if (e.key == LM_KEY_OK && e.kind == LM_PRESS) {
        m->playing = !m->playing;
        lm_spring_set(&m->morph, m->playing ? 1.f : 0.f);
        lm_spring_poke(&m->flash[1], 1.f);
        m->probe_s = &m->morph;
        m->probe_l = "play/pause morph  toggle";
        return true;
    }
    if ((e.key == LM_KEY_PREV || e.key == LM_KEY_NEXT) && lm_ev_step(e)) {
        change(m, e.key == LM_KEY_PREV ? -1 : 1);
        return true;
    }
    return false;
}

static void update(lm_page_t *p, float dt)
{
    music_t *m = (music_t *)p;
    lm_spring_set(&m->spin, m->playing ? 1.2f : 0.f);
    lm_spring_update(&m->spin, dt);
    m->angle += m->spin.value * dt * LM_TAU * 0.55f;
    if (m->angle > 1000.f) m->angle -= LM_TAU * 150.f;
    if (m->playing) m->pos = lm_minf((float)TRACKS[m->idx].dur, m->pos + dt * 7.f);  /* demo fast-forward */
    lm_spring_update(&m->morph, dt);
    lm_spring_update(&m->slide, dt);
    lm_marquee_update(&m->title, dt);
    for (int i = 0; i < 3; i++) lm_spring_update(&m->flash[i], dt);
    m->tick += dt;
    if (m->tick > 0.09f) {
        m->tick = 0;
        float beat = 0.5f + 0.5f * lm_sinf(p->age * LM_TAU * 2.1f);
        for (int i = 0; i < NB; i++) {
            float v = 0;
            if (m->playing) {
                float shape = 0.9f - 0.45f * (float)i / NB + 0.15f * lm_sinf((float)i * 1.3f + p->age * 3);
                v = lm_clampf(shape * (0.35f + 0.65f * lm_rng_f(&m->rng)) * (0.7f + 0.3f * beat), 0.05f, 1);
            }
            lm_spring_set(&m->bars[i], v);
        }
    }
    for (int i = 0; i < NB; i++) {
        lm_spring_t *b = &m->bars[i];
        lm_spring_motion(b, b->target > b->value ? LM_MOTION_VU_ATTACK : LM_MOTION_VU_RELEASE);
        lm_spring_update(b, dt);
        float v = lm_maxf(0, b->value);
        if (v >= m->peaks[i]) { m->peaks[i] = v; m->peak_v[i] = 0; }
        else { m->peak_v[i] += dt * 2.2f; m->peaks[i] = lm_maxf(v, m->peaks[i] - m->peak_v[i] * dt); }
    }
}

static void disc(music_t *m, lm_gfx_t *g, float cx, float cy, float r)
{
    lm_disc(g, cx, cy, r);
    lm_gfx_mode(g, LM_CLEAR);
    lm_ring(g, cx, cy, r - 1.5f, 1);
    lm_ring(g, cx, cy, r - 3.5f, 1);                 /* grooves */
    lm_ring(g, cx, cy, r - 6, 1);
    for (int k = 0; k < 2; k++) {                    /* rotating light reflection */
        float a = m->angle + (float)k * LM_PI;
        for (int j = 0; j < 3; j++) {
            float aa = a + (float)(j - 1) * 0.12f;
            lm_line(g, cx + lm_sinf(aa) * r * 0.42f, cy - lm_cosf(aa) * r * 0.42f,
                    cx + lm_sinf(aa) * (r - 2.5f), cy - lm_cosf(aa) * (r - 2.5f));
        }
    }
    lm_disc(g, cx, cy, r * 0.34f);
    lm_gfx_mode(g, LM_SET);
    lm_ring(g, cx, cy, r * 0.34f, 1);
    lm_disc(g, cx, cy, 1.2f);
    lm_disc(g, cx + lm_sinf(m->angle) * r * 0.2f, cy - lm_cosf(m->angle) * r * 0.2f, 0.9f);
}

/* play triangle's halves slide/stretch into the two pause bars */
static void play_pause(lm_gfx_t *g, float cx, float cy, float mo, float flash)
{
    float s = 1 + 0.25f * flash, h = 4.5f * s, k = lm_clampf(mo, -0.2f, 1.2f);
    float l[8] = { cx - 3.5f * s, cy - h, lm_lerpf(cx + 0.25f, cx - 1.f * s, k), lm_lerpf(cy - h / 2, cy - h, k),
                   lm_lerpf(cx + 0.25f, cx - 1.f * s, k), lm_lerpf(cy + h / 2, cy + h, k), cx - 3.5f * s, cy + h };
    float r[8] = { lm_lerpf(cx - 0.25f, cx + 1.f * s, k), lm_lerpf(cy - h / 2, cy - h, k),
                   lm_lerpf(cx + 4.5f * s, cx + 3.5f * s, k), lm_lerpf(cy, cy - h, k),
                   lm_lerpf(cx + 4.5f * s, cx + 3.5f * s, k), lm_lerpf(cy, cy + h, k),
                   lm_lerpf(cx - 0.25f, cx + 1.f * s, k), lm_lerpf(cy + h / 2, cy + h, k) };
    lm_poly(g, l, 4);
    lm_poly(g, r, 4);
}

static void skip(lm_gfx_t *g, float cx, float cy, float d, float flash)
{
    float s = 1 + 0.35f * flash, dx = d * 1.5f * flash;
    for (int k = 0; k < 2; k++) {
        float ox = cx + dx + d * ((float)k * 3 - 1.5f) * s;
        float t[6] = { ox - d * 1.5f * s, cy - 2.5f * s, ox + d * 2 * s, cy, ox - d * 1.5f * s, cy + 2.5f * s };
        lm_poly(g, t, 3);
    }
}

static void render(lm_page_t *p, lm_gfx_t *g)
{
    music_t *m = (music_t *)p;
    const track_t *t = &TRACKS[m->idx];
    const float cx = 22, cy = 24, r = 18;
    disc(m, g, cx, cy, r);
    skip(g, cx - 13, 55, -1, m->flash[0].value);
    play_pause(g, cx, 55, m->morph.value, m->flash[1].value);
    skip(g, cx + 13, 55, 1, m->flash[2].value);

    const float rx = 48;
    float sl = m->slide.value;
    lm_gfx_clip_push(g, rx, 0, LM_W - rx, 22);
    lm_gfx_off_push(g, sl * 40.f * (float)m->dir, 0);
    lm_marquee_draw(&m->title, g, rx, 2);
    lm_font(g, LM_FONT_TINY);
    lm_text(g, rx, 13, t->artist);
    lm_gfx_off_pop(g);
    if (m->prev_idx >= 0 && sl > 0.01f) {
        lm_gfx_off_push(g, (sl - 1) * 40.f * (float)m->dir, 0);
        lm_font(g, LM_FONT_UI);
        lm_text(g, rx, 2, TRACKS[m->prev_idx].title);
        lm_gfx_off_pop(g);
    }
    lm_gfx_clip_pop(g);

    const float base = 42, bw = 5;
    for (int i = 0; i < NB; i++) {
        int h = lm_roundi(lm_clampf(m->bars[i].value, 0, 1.1f) * 16);
        float x = rx + (float)i * bw;
        if (h > 0) lm_box(g, x, base - (float)h, bw - 1, (float)h);
        lm_hline(g, x, base - (float)lm_roundi(m->peaks[i] * 16) - 2, bw - 1);
    }
    const float py = 47;
    lm_hline(g, rx, py + 1, LM_W - rx - 2);
    float px = rx + (LM_W - rx - 2) * m->pos / (float)t->dur;
    lm_box(g, rx, py, px - rx, 3);
    lm_disc(g, px, py + 1.5f, 2.2f);
    int e = (int)m->pos, rem = t->dur - e;
    char b[24];
    lm_font(g, LM_FONT_TINY);
    snprintf(b, sizeof(b), "%d:%02d", e / 60, e % 60);   lm_text(g, rx, 53, b);
    snprintf(b, sizeof(b), "-%d:%02d", rem / 60, rem % 60); lm_text_right(g, LM_W - 2, 53, b);
}

static const lm_spring_t *probe(lm_page_t *p, const char **label)
{
    music_t *m = (music_t *)p;
    *label = m->probe_l;
    return m->probe_s;
}

static const lm_page_ops_t OPS = { enter, 0, 0, key, update, render, 0, probe };
static music_t s_music = { { &OPS, "Music", 0, 0 } };

lm_page_t *app_music(void) { return &s_music.base; }
