#include "lm_menu.h"

#include <stdio.h>
#include <string.h>

#include "lm_fonts.h"
#include "lm_math.h"

#define ROW_H 12
#define TOP   13
#define VIS   4

static lm_menu_t *M(lm_page_t *p) { return (lm_menu_t *)p; }

static void value_text(lm_menu_t *m, int i, char *out, int n)
{
    const lm_menu_item_t *it = &m->items[i];
    lm_ui_t *ui = m->base.ui;
    out[0] = 0;
    if (it->kind == LM_MI_OPTION) {
        int32_t v = lm_setting(ui, it->setting);
        for (int k = 0; k < it->noptions; k++)
            if (it->options[k].value == v) { snprintf(out, (size_t)n, "%s", it->options[k].label); return; }
        snprintf(out, (size_t)n, "%d", (int)v);
    } else if (it->fmt) {
        it->fmt(ui, out, n);
    }
}

static float label_w(lm_menu_t *m, int i)
{
    lm_gfx_t *g = &m->base.ui->gfx;
    lm_font(g, LM_FONT_UI);
    return (float)lm_text_w(g, m->items[i].label) + 9.f;
}

static void counter_text(lm_menu_t *m, char *b) { snprintf(b, 8, "%d/%d", m->sel + 1, m->n); }

static void menu_enter(lm_page_t *p)
{
    lm_menu_t *m = M(p);
    lm_ui_t *ui = p->ui;
    if (m->n > LM_MENU_MAX_ITEMS) m->n = LM_MENU_MAX_ITEMS;
    m->sel = m->top = 0;
    /* jelly cursor: leading edge snappy, trailing edge follows it */
    lm_spring_init(&m->cur, LM_MOTION_SNAPPY, 0, ROW_H);
    lm_spring_init(&m->trail, LM_MOTION_FOLLOW, 0, ROW_H);
    lm_spring_init(&m->cur_w, LM_MOTION_SNAPPY, 0, 1);
    lm_spring_set(&m->cur_w, label_w(m, 0));
    lm_spring_init(&m->scroll, LM_MOTION_SMOOTH, 0, ROW_H);
    lm_spring_init(&m->press, LM_MOTION_PRESS, 0, 6);
    char b[8]; counter_text(m, b);
    lm_roll_init(&m->counter, LM_FONT_TINY, b, 0.3f, 0.04f);
    for (int i = 0; i < m->n; i++) {
        lm_menu_row_t *r = &m->row[i];
        const lm_menu_item_t *it = &m->items[i];
        float on = (it->kind == LM_MI_TOGGLE && lm_setting(ui, it->setting)) ? 1.f : 0.f;
        lm_spring_init(&r->sw, LM_MOTION_TOGGLE, on, 6);
        char v[LM_ROLL_MAX + 1]; value_text(m, i, v, sizeof(v));
        lm_roll_init(&r->val, LM_FONT_UI, v, 0.32f, 0.03f);
        r->busy = 0;
    }
}

static void move(lm_menu_t *m, int d)
{
    int n = m->sel + d;
    if (n < 0 || n >= m->n) {                     /* wrap: the long ride is the feedback */
        n = (n + m->n) % m->n;
        lm_haptic(m->base.ui);
    }
    m->sel = (uint8_t)n;
    if (n < m->top) m->top = (uint8_t)n;
    else if (n >= m->top + VIS) m->top = (uint8_t)(n - VIS + 1);
    lm_spring_set(&m->cur, (float)n);
    lm_spring_set(&m->cur_w, label_w(m, n));
    lm_spring_set(&m->scroll, (float)m->top);
    char b[8]; counter_text(m, b);
    lm_roll_set(&m->counter, b, d);
}

static void activate(lm_menu_t *m, int i)
{
    const lm_menu_item_t *it = &m->items[i];
    lm_menu_row_t *r = &m->row[i];
    lm_ui_t *ui = m->base.ui;
    switch (it->kind) {
    case LM_MI_TOGGLE:
        if (r->busy > 0) return;
        if (it->busy_ms && !lm_setting(ui, it->setting)) { r->busy = (float)it->busy_ms / 1000.f; return; }
        lm_setting_set(ui, it->setting, !lm_setting(ui, it->setting));
        lm_haptic(ui);
        if (it->done) it->done(ui, lm_setting(ui, it->setting));
        break;
    case LM_MI_OPTION: {
        int32_t v = lm_setting(ui, it->setting);
        int k = 0;
        for (; k < it->noptions; k++) if (it->options[k].value == v) break;
        lm_setting_set(ui, it->setting, it->options[(k + 1) % it->noptions].value);
        break;
    }
    case LM_MI_SHEET:
    case LM_MI_ACTION:
        if (it->action) it->action(m, i);
        break;
    case LM_MI_SUBMENU:
        if (it->page) lm_push(ui, it->page, LM_TR_SLIDE);
        break;
    }
}

static bool menu_key(lm_page_t *p, lm_event_t e)
{
    lm_menu_t *m = M(p);
    if ((e.key == LM_KEY_PREV || e.key == LM_KEY_NEXT) && lm_ev_step(e)) {
        move(m, e.key == LM_KEY_PREV ? -1 : 1);
        return true;
    }
    if (e.key == LM_KEY_OK && e.kind == LM_PRESS) {
        lm_spring_poke(&m->press, 1.f);
        activate(m, m->sel);
        return true;
    }
    return false;
}

static void menu_update(lm_page_t *p, float dt)
{
    lm_menu_t *m = M(p);
    lm_ui_t *ui = p->ui;
    lm_spring_set(&m->trail, m->cur.value);       /* chain: trail follows the output */
    lm_spring_update(&m->cur, dt);
    lm_spring_update(&m->trail, dt);
    lm_spring_update(&m->cur_w, dt);
    lm_spring_update(&m->scroll, dt);
    lm_spring_update(&m->press, dt);
    lm_roll_update(&m->counter, dt);
    for (int i = 0; i < m->n; i++) {
        const lm_menu_item_t *it = &m->items[i];
        lm_menu_row_t *r = &m->row[i];
        if (r->busy > 0) {
            r->busy -= dt;
            if (r->busy <= 0) {
                r->busy = 0;
                lm_setting_set(ui, it->setting, 1);
                lm_haptic(ui);
                if (it->done) it->done(ui, 1);
            }
        }
        if (it->kind == LM_MI_TOGGLE) lm_spring_set(&r->sw, lm_setting(ui, it->setting) ? 1.f : 0.f);
        lm_spring_update(&r->sw, dt);
        char v[LM_ROLL_MAX + 1]; value_text(m, i, v, sizeof(v));
        lm_roll_set(&r->val, v, 1);
        lm_roll_update(&r->val, dt);
    }
}

static void widget(lm_menu_t *m, lm_gfx_t *g, int i, float ry)
{
    const lm_menu_item_t *it = &m->items[i];
    lm_menu_row_t *r = &m->row[i];
    const float rx = LM_W - 7;
    if (it->kind == LM_MI_TOGGLE) {
        if (r->busy > 0) lm_draw_spinner(g, rx - 7, ry + 4.5f, 4.5f, m->base.age, 1.8f);
        else lm_draw_toggle(g, rx - 15, ry + 1, r->sw.value, r->sw.vel);
    } else if (it->kind == LM_MI_OPTION || it->kind == LM_MI_SHEET) {
        lm_roll_draw(&r->val, g, rx - (float)lm_roll_width(&r->val, g), ry + 1);
    } else if (it->kind == LM_MI_SUBMENU) {
        lm_draw_chevron(g, rx - 2, ry + 5, true, 2);
    }
}

static void menu_render(lm_page_t *p, lm_gfx_t *g)
{
    lm_menu_t *m = M(p);
    /* title bar */
    lm_draw_chevron(g, 3, 5, false, 2);
    lm_font(g, LM_FONT_UI);
    lm_text(g, 10, 1, p->title);
    lm_roll_draw(&m->counter, g, LM_W - 3 - (float)lm_roll_width(&m->counter, g), 3);
    lm_dotted_hline(g, 0, 11, LM_W, 2);

    float sc = m->scroll.value;
    lm_gfx_clip_push(g, 0, 12, LM_W, LM_H - 12);
    for (int i = 0; i < m->n; i++) {
        float ry = TOP + ((float)i - sc) * ROW_H;
        if (ry < -ROW_H || ry > LM_H) continue;
        /* staggered entrance: slide in from the right and fade up */
        float e = lm_curve(LM_CURVE_DECEL, (p->age - 0.08f - (float)(i - m->top) * 0.055f) / 0.42f);
        float dx = (1 - e) * 28.f;
        /* focus indent follows the cursor output continuously */
        float ind = 2.f * lm_clamp01(1.f - lm_absf(m->cur.value - (float)i));
        lm_gfx_alpha(g, lm_clamp01(e * 1.3f));
        lm_gfx_off_push(g, dx, 0);
        lm_font(g, LM_FONT_UI);
        lm_text(g, 6 + ind, ry + 1, m->items[i].label);
        widget(m, g, i, ry);
        lm_gfx_off_pop(g);
        lm_gfx_alpha(g, 1.f);
    }
    /* jelly cursor: XOR pill spanning trailing..leading edge */
    float pr = m->press.value;
    float a = lm_minf(m->cur.value, m->trail.value), b = lm_maxf(m->cur.value, m->trail.value);
    float ya = TOP + (a - sc) * ROW_H, yb = TOP + (b - sc) * ROW_H + ROW_H - 1;
    const float cap = ROW_H * 2.2f;                /* long wrap rides stay compact */
    if (yb - ya > cap) {
        if (m->cur.value > m->trail.value) ya = yb - cap; else yb = ya + cap;
    }
    lm_gfx_mode(g, LM_XOR);
    lm_rbox(g, 2 + 1.5f * pr, ya + 0.5f * pr, lm_maxf(0, m->cur_w.value - 3 * pr), yb - ya - pr, 3);
    lm_gfx_mode(g, LM_SET);
    lm_gfx_clip_pop(g);
    lm_draw_scrollbar(g, LM_W - 3, 13, LM_H - 14, sc, VIS, (float)m->n);
}

void lm_menu_focus(lm_menu_t *m, float r[4])
{
    r[0] = 2;
    r[1] = TOP + (m->cur.value - m->scroll.value) * ROW_H;
    r[2] = label_w(m, m->sel);
    r[3] = ROW_H - 1;
}

static void menu_focus(lm_page_t *p, float r[4]) { lm_menu_focus(M(p), r); }

static const lm_spring_t *menu_probe(lm_page_t *p, const char **label)
{
    *label = "cursor  snappy / trail follow";
    return &M(p)->cur;
}

const lm_page_ops_t lm_menu_ops = {
    menu_enter, 0, 0, menu_key, menu_update, menu_render, menu_focus, menu_probe,
};
