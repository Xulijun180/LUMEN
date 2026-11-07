#include "lm_widgets.h"

#include <string.h>

#include "lm_math.h"

/* ---------------------------------------------------------------- rolling */
void lm_roll_init(lm_roll_t *r, const uint8_t *font, const char *text, float dur, float stagger)
{
    memset(r, 0, sizeof(*r));
    r->font = font;
    r->dur = dur;
    r->stagger = stagger;
    lm_roll_reset(r, text);
}

void lm_roll_reset(lm_roll_t *r, const char *text)
{
    size_t n = strlen(text);
    if (n > LM_ROLL_MAX) n = LM_ROLL_MAX;
    memcpy(r->text, text, n);
    r->text[n] = 0;
    r->n = (uint8_t)n;
    for (size_t i = 0; i < n; i++) {
        r->cell[i].from = r->cell[i].to = text[i];
        r->cell[i].t = 1e3f;
        r->cell[i].dir = 1;
    }
}

void lm_roll_set(lm_roll_t *r, const char *text, int dir)
{
    size_t n = strlen(text);
    if (n > LM_ROLL_MAX) n = LM_ROLL_MAX;
    if (n == r->n && memcmp(text, r->text, n) == 0) return;
    if (n != r->n) { lm_roll_reset(r, text); return; }
    int rank = 0;
    for (int i = (int)n - 1; i >= 0; i--) {     /* rightmost rolls first */
        if (r->cell[i].to == text[i]) continue;
        r->cell[i].from = r->cell[i].to;
        r->cell[i].to = text[i];
        r->cell[i].dir = (int8_t)(dir < 0 ? -1 : 1);
        r->cell[i].t = -(float)rank * r->stagger;
        rank++;
    }
    memcpy(r->text, text, n);
}

void lm_roll_update(lm_roll_t *r, float dt)
{
    for (int i = 0; i < r->n; i++)
        if (r->cell[i].t < r->dur + 1.f) r->cell[i].t += dt;
}

bool lm_roll_busy(const lm_roll_t *r)
{
    for (int i = 0; i < r->n; i++)
        if (r->cell[i].from != r->cell[i].to && r->cell[i].t < r->dur) return true;
    return false;
}

int lm_roll_width(lm_roll_t *r, lm_gfx_t *g)
{
    lm_font(g, r->font);
    return lm_text_w(g, r->text);
}

int lm_roll_draw(lm_roll_t *r, lm_gfx_t *g, float x, float y)
{
    lm_font(g, r->font);
    int fh = lm_font_h(g);
    float step = (float)fh + 2.f;
    float cx = (float)lm_roundi(x);
    y = (float)lm_roundi(y);
    lm_gfx_clip_push(g, cx - 1, y, 999, (float)fh);
    char s[2] = {0, 0};
    for (int i = 0; i < r->n; i++) {
        lm_roll_cell_t *c = &r->cell[i];
        if (c->from == c->to || c->t >= r->dur) {
            s[0] = c->to;
            lm_text(g, cx, y, s);
        } else {
            float p = c->t <= 0 ? 0.f : lm_curve(LM_CURVE_OVERSHOOT, c->t / r->dur);
            float off = p * step * c->dir;
            s[0] = c->from; lm_text(g, cx, y - off, s);
            s[0] = c->to;   lm_text(g, cx, y + step * c->dir - off, s);
        }
        cx += (float)lm_char_w(g, c->to);
    }
    lm_gfx_clip_pop(g);
    return (int)cx;
}

/* ---------------------------------------------------------------- marquee */
void lm_marquee_init(lm_marquee_t *m, const uint8_t *font, const char *text, float width)
{
    m->font = font; m->width = width; m->speed = 22.f; m->hold = 1.2f; m->gap = 18.f;
    lm_marquee_set(m, text);
}

void lm_marquee_set(lm_marquee_t *m, const char *text) { m->text = text; m->t = 0; }

void lm_marquee_draw(lm_marquee_t *m, lm_gfx_t *g, float x, float y)
{
    lm_font(g, m->font);
    float tw = (float)lm_text_w(g, m->text);
    lm_gfx_clip_push(g, x, y - 1, m->width, (float)lm_font_h(g) + 3);
    if (tw <= m->width) {
        lm_text(g, x, y, m->text);
    } else {
        float period = (tw + m->gap) / m->speed;
        float t = lm_maxf(0.f, m->t - m->hold);
        t = t - (period + m->hold) * floorf(t / (period + m->hold));
        float off = lm_minf(t, period) * m->speed;
        lm_text(g, x - off, y, m->text);
        lm_text(g, x - off + tw + m->gap, y, m->text);
    }
    lm_gfx_clip_pop(g);
}

/* ---------------------------------------------------------------- toggle */
void lm_draw_toggle(lm_gfx_t *g, float x, float y, float p, float vel)
{
    const float w = 15, h = 9;
    float pc = lm_clamp01(p);
    lm_gfx_mode(g, LM_SET);
    lm_rframe(g, x, y, w, h, 4);
    float kx = x + 4.5f + 6.f * p, cy = y + 4.5f;
    if (pc > 0.04f) lm_rbox(g, x + 1, y + 1, kx - x, h - 2, 3);
    /* squash & stretch: the knob becomes a capsule trailing its motion */
    float tail = lm_clampf(-vel * 6.f / 60.f * 1.4f, -3.f, 3.f);
    lm_gfx_mode(g, LM_CLEAR);
    if (lm_absf(tail) > 0.4f) lm_capsule(g, kx, cy, kx + tail, cy, 6.f);
    lm_disc(g, kx, cy, 3.2f);
    lm_gfx_mode(g, LM_SET);
    float core = 2.3f * (1 - pc);
    if (core > 0.6f) {
        if (lm_absf(tail) > 0.4f) lm_capsule(g, kx, cy, kx + tail * 0.7f, cy, core * 2);
        lm_disc(g, kx, cy, core);
    }
}

void lm_draw_spinner(lm_gfx_t *g, float cx, float cy, float r, float t, float thick)
{
    /* rotating arc whose length breathes (material indeterminate style) */
    float rot = t * LM_TAU * 0.9f;
    float ln = 1.3f + 3.2f * (0.5f - 0.5f * lm_cosf(t * LM_TAU * 0.7f));
    float a0 = rot - LM_TAU * floorf(rot / LM_TAU);
    lm_arc(g, cx, cy, r, a0, a0 + ln, thick);
}

void lm_draw_scrollbar(lm_gfx_t *g, float x, float y, float h, float pos, float visible, float total)
{
    if (total <= visible) return;
    lm_dotted_vline(g, x + 1, y, h, 2);
    float th = lm_maxf(6.f, h * visible / total);
    float ty = y + (h - th) * lm_clampf(pos / lm_maxf(1.f, total - visible), -0.3f, 1.3f);
    if (ty < y) { th -= (y - ty); ty = y; }          /* rubber band compresses */
    if (ty + th > y + h) th = y + h - ty;
    th = lm_maxf(3.f, th);
    lm_rbox(g, x, ty, 3, th, 1);
}

void lm_draw_chevron(lm_gfx_t *g, float x, float cy, bool right, int s)
{
    for (int i = 0; i <= s; i++) {
        float dx = right ? (float)i : (float)(s - i);
        lm_pixel(g, x + dx, cy - (float)s + (float)i);
        lm_pixel(g, x + dx, cy + (float)s - (float)i);
    }
}

void lm_draw_battery(lm_gfx_t *g, float x, float y, float level)
{
    lm_frame(g, x, y, 11, 6);
    lm_vline(g, x + 11, y + 2, 2);
    lm_box(g, x + 2, y + 2, (float)lm_roundi(7 * level), 2);
}

void lm_draw_wifi(lm_gfx_t *g, float x, float cy, bool on)
{
    lm_pixel(g, x + 3, cy + 2);
    if (!on) { lm_line(g, x + 1, cy - 2, x + 5, cy + 2); return; }
    lm_hline(g, x + 2, cy, 3);
    lm_pixel(g, x + 1, cy + 1); lm_pixel(g, x + 5, cy + 1);
    lm_hline(g, x + 1, cy - 2, 5);
    lm_pixel(g, x, cy - 1); lm_pixel(g, x + 6, cy - 1);
}
