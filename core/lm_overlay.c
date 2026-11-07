/*
 * Toast / dialog / bottom sheet. Each is a static singleton implementing
 * the lm_overlay_ops_t interface.
 */
#include "lm_overlay.h"

#include <stdio.h>
#include <string.h>

#include "lm_fonts.h"
#include "lm_math.h"
#include "lm_widgets.h"

static void dim_backdrop(lm_gfx_t *g, float amount)
{
    if (amount > 0.01f) lm_buf_fade(g->target, 1.f - 0.55f * lm_clamp01(amount));
}

/* ======================================================================= toast */
typedef struct {
    lm_overlay_t base;
    char text[24];
    lm_icon_t icon;
    float life, w;
    lm_spring_t y;
} toast_t;

static toast_t s_toast;

static void toast_close(lm_overlay_t *o)
{
    toast_t *t = (toast_t *)o;
    o->closing = true;
    lm_spring_motion(&t->y, LM_MOTION_EXIT);      /* tiny lift, then away */
    lm_spring_set(&t->y, -18.f);
}

static void toast_update(lm_overlay_t *o, float dt)
{
    toast_t *t = (toast_t *)o;
    lm_spring_update(&t->y, dt);
    if (!o->closing && o->age > t->life) toast_close(o);
    if (o->closing && t->y.value < -15.f) o->dead = true;
}

static void icon_small(lm_gfx_t *g, lm_icon_t k, float x, float y)
{
    if (k == LM_ICON_CHECK) {
        lm_line(g, x, y + 3, x + 2, y + 5);
        lm_line(g, x + 2, y + 5, x + 6, y + 1);
    } else if (k == LM_ICON_WIFI) {
        lm_pixel(g, x + 3, y + 5);
        lm_hline(g, x + 2, y + 3, 3);
        lm_hline(g, x + 1, y + 1, 5);
    } else {
        lm_pixel(g, x + 3, y);
        lm_vline(g, x + 3, y + 2, 4);
    }
}

static void toast_render(lm_overlay_t *o, lm_gfx_t *g)
{
    toast_t *t = (toast_t *)o;
    float x = (float)lm_roundi(LM_W / 2.f - t->w / 2.f);
    float y = (float)lm_roundi(t->y.value);
    float h = 13.f + (float)(int)(lm_clamp01(lm_absf(t->y.vel) / 60.f) * 2.f);  /* velocity stretch */
    lm_gfx_mode(g, LM_CLEAR); lm_rbox(g, x - 1, y - 1, t->w + 2, h + 2, 7);
    lm_gfx_mode(g, LM_SET);   lm_rbox(g, x, y, t->w, h, 6);
    lm_gfx_mode(g, LM_CLEAR); lm_disc(g, x + 8.5f, y + h / 2, 4.5f);
    lm_gfx_mode(g, LM_SET);   icon_small(g, t->icon, x + 5, y + h / 2 - 3);
    lm_gfx_mode(g, LM_CLEAR);
    lm_font(g, LM_FONT_UI);
    lm_text(g, x + 17, y + (h - (float)lm_font_h(g)) / 2.f, t->text);
    lm_gfx_mode(g, LM_SET);
}

static const lm_overlay_ops_t TOAST_OPS = { 0, toast_update, toast_render, toast_close, 0 };

void lm_toast(lm_ui_t *ui, const char *text, lm_icon_t icon)
{
    toast_t *t = &s_toast;
    bool up = false;
    for (int i = 0; i < ui->nov; i++) up |= ui->ov[i] == &t->base;
    t->base.ops = &TOAST_OPS;
    t->base.modal = false;
    t->base.closing = t->base.dead = false;
    t->base.age = 0;
    snprintf(t->text, sizeof(t->text), "%s", text);
    t->icon = icon;
    t->life = 1.8f;
    lm_font(&ui->gfx, LM_FONT_UI);
    t->w = (float)lm_text_w(&ui->gfx, t->text) + 24.f;
    if (!up) lm_spring_init(&t->y, LM_MOTION_BOUNCY, -16.f, 1.f);
    else lm_spring_motion(&t->y, LM_MOTION_BOUNCY);     /* retarget in flight */
    lm_spring_set(&t->y, 3.f);
    lm_show(ui, &t->base);
}

/* ======================================================================= dialog */
typedef struct {
    lm_overlay_t base;
    lm_dialog_desc_t d;
    int sel, result;
    float busy;               /* <0: not busy */
    lm_spring_t scale, px, py, cur_x, cur_w, shake;
} dialog_t;

static dialog_t s_dialog;
#define DW 110
#define DH 46
#define DX ((LM_W - DW) / 2)
#define DY ((LM_H - DH) / 2)

static void btn_rect(int i, float r[4])
{
    const int gap = 4, bw = (DW - 8 - gap) / 2;
    r[0] = (float)(DX + 4 + i * (bw + gap)); r[1] = (float)(DY + DH - 15); r[2] = (float)bw; r[3] = 11;
}

static void dialog_cursor(dialog_t *dl, bool snap)
{
    float r[4];
    btn_rect(dl->sel, r);
    if (snap) { lm_spring_snap(&dl->cur_x, r[0]); lm_spring_snap(&dl->cur_w, r[2] * 0.3f); }
    lm_spring_set(&dl->cur_x, r[0]);
    lm_spring_set(&dl->cur_w, r[2]);
}

static void dialog_close_with(dialog_t *dl, int result)
{
    if (dl->base.closing) return;
    dl->result = result;
    dl->base.closing = true;
    lm_spring_motion(&dl->scale, LM_MOTION_EXIT);    /* swell a touch, then shrink */
    lm_spring_set(&dl->scale, 0.f);
}

static void dialog_close(lm_overlay_t *o) { dialog_close_with((dialog_t *)o, -1); }

static void dialog_key(lm_overlay_t *o, lm_event_t e)
{
    dialog_t *dl = (dialog_t *)o;
    if (dl->busy >= 0) return;
    if ((e.key == LM_KEY_PREV || e.key == LM_KEY_NEXT) && lm_ev_step(e)) {
        int d = e.key == LM_KEY_PREV ? -1 : 1, n = dl->sel + d;
        if (n >= 0 && n < 2) { dl->sel = n; dialog_cursor(dl, false); lm_haptic(o->ui); }
        else lm_spring_kick(&dl->shake, (float)d * 90.f);
    } else if (e.key == LM_KEY_OK && e.kind == LM_PRESS) {
        if (dl->d.busy_btn == dl->sel) dl->busy = 0.f;
        else dialog_close_with(dl, dl->sel);
    } else if (e.key == LM_KEY_BACK && e.kind == LM_PRESS) {
        dialog_close_with(dl, -1);
    }
}

static void dialog_update(lm_overlay_t *o, float dt)
{
    dialog_t *dl = (dialog_t *)o;
    lm_spring_t *ss[] = { &dl->scale, &dl->px, &dl->py, &dl->cur_x, &dl->cur_w, &dl->shake };
    for (unsigned i = 0; i < sizeof(ss) / sizeof(ss[0]); i++) lm_spring_update(ss[i], dt);
    if (dl->busy >= 0) {
        dl->busy += dt;
        if (dl->busy >= dl->d.busy_s && !o->closing) dialog_close_with(dl, dl->sel);
    }
    if (o->closing && dl->scale.value < 0.04f) {
        o->dead = true;
        if (dl->d.cb) dl->d.cb(o->ui, dl->result, dl->d.ctx);
    }
}

static void warn_glyph(lm_gfx_t *g, float x, float y)
{
    float tri[6] = { x + 4.5f, y + 0.5f, x + 9.5f, y + 9.5f, x - 0.5f, y + 9.5f };
    lm_poly(g, tri, 3);
    lm_gfx_mode(g, LM_CLEAR);
    lm_vline(g, x + 4, y + 3, 3);
    lm_pixel(g, x + 4, y + 7);
    lm_gfx_mode(g, LM_SET);
}

static void dialog_render(lm_overlay_t *o, lm_gfx_t *g)
{
    dialog_t *dl = (dialog_t *)o;
    lm_ui_t *ui = o->ui;
    float s = dl->scale.value;
    uint8_t *screen = g->target;
    dim_backdrop(g, s);
    if (s <= 0.02f) return;

    /* content is laid out once at 1:1 into a scratch buffer, then scaled
     * about a pivot that travels from the opener to the centre */
    uint8_t *buf = ui->tr.active ? 0 : ui->buf_b;
    if (buf) { lm_gfx_target(g, buf); lm_gfx_clear(g); }
    float dx = dl->shake.value;
    lm_gfx_off_push(g, dx, 0);
    lm_rframe(g, DX, DY, DW, DH, 5);
    lm_rframe(g, DX + 1, DY + 1, DW - 2, DH - 2, 4);
    lm_font(g, LM_FONT_UI);
    if (dl->busy < 0) {
        warn_glyph(g, DX + 8, DY + 5);
        lm_text(g, DX + 22, DY + 5, dl->d.title);
        lm_text(g, DX + 8, DY + 17, dl->d.message);
        for (int i = 0; i < 2; i++) {
            float r[4];
            btn_rect(i, r);
            lm_rframe(g, r[0], r[1], r[2], r[3], 3);
            lm_text_center(g, r[0] + r[2] / 2, r[1] + 2, dl->d.buttons[i]);
        }
        float r[4];
        btn_rect(dl->sel, r);
        float cw = dl->cur_w.value;
        lm_gfx_mode(g, LM_XOR);
        lm_rbox(g, dl->cur_x.value + (r[2] - cw) / 2, r[1], cw, 11, 3);
        lm_gfx_mode(g, LM_SET);
    } else {
        lm_draw_spinner(g, DX + DW / 2.f, DY + 18, 7, dl->busy, 2.f);
        char t[24];
        int dots = 1 + (int)(dl->busy * 4.f) % 3;
        snprintf(t, sizeof(t), "%s%.*s", dl->d.busy_text, dots, "...");
        lm_text_center(g, DX + DW / 2.f, DY + 30, t);
    }
    lm_gfx_off_pop(g);
    if (!buf) return;

    lm_gfx_target(g, screen);
    float cx = dl->px.value, cy = dl->py.value;
    float bx = DX - 1 + dx, by = DY - 1;
    float w = (DW + 2) * s, h = (DH + 2) * s;
    float x = cx + (bx - cx) * s, y = cy + (by - cy) * s;
    lm_buf_blit_region(screen, buf, bx, by, DW + 2, DH + 2, x, y, w, h, 6.f * s, 1.f, true);
}

static const lm_spring_t *dialog_probe(lm_overlay_t *o, const char **label)
{
    dialog_t *dl = (dialog_t *)o;
    if (o->age < 0.9f || o->closing) { *label = "dialog scale  bouncy"; return &dl->scale; }
    *label = "dialog cursor  snappy";
    return &dl->cur_x;
}

static const lm_overlay_ops_t DIALOG_OPS = { dialog_key, dialog_update, dialog_render, dialog_close, dialog_probe };

void lm_dialog(lm_ui_t *ui, const lm_dialog_desc_t *d)
{
    dialog_t *dl = &s_dialog;
    memset(dl, 0, sizeof(*dl));
    dl->base.ops = &DIALOG_OPS;
    dl->base.modal = true;
    dl->d = *d;
    dl->sel = d->default_btn;
    dl->busy = -1.f;
    dl->result = -1;
    lm_spring_init(&dl->scale, LM_MOTION_BOUNCY, 0.f, LM_W);
    lm_spring_set(&dl->scale, 1.f);
    lm_spring_init(&dl->px, LM_MOTION_CAROUSEL, d->origin_x, 1.f);
    lm_spring_init(&dl->py, LM_MOTION_CAROUSEL, d->origin_y, 1.f);
    lm_spring_set(&dl->px, LM_W / 2.f);
    lm_spring_set(&dl->py, LM_H / 2.f);
    lm_spring_init(&dl->cur_x, LM_MOTION_SNAPPY, 0, 1.f);
    lm_spring_init(&dl->cur_w, LM_MOTION_SNAPPY, 0, 1.f);
    lm_spring_init(&dl->shake, LM_MOTION_WOBBLE, 0, 1.f);
    dialog_cursor(dl, true);
    lm_show(ui, &dl->base);
}

/* ======================================================================= sheet */
typedef struct {
    lm_overlay_t base;
    lm_sheet_desc_t d;
    lm_spring_t y, fill, shake;
    lm_roll_t pct;
} sheet_t;

static sheet_t s_sheet;
#define SH 40

static float sheet_frac(const sheet_t *s, int32_t v)
{
    return s->d.hi > s->d.lo ? (float)(v - s->d.lo) / (float)(s->d.hi - s->d.lo) * 0.9f + 0.1f : 1.f;
}

static void sheet_fmt(char *b, int32_t v) { snprintf(b, 8, "%3d%%", (int)v); }

static void sheet_close(lm_overlay_t *o)
{
    sheet_t *s = (sheet_t *)o;
    o->closing = true;
    lm_spring_motion(&s->y, LM_MOTION_SMOOTH);
    lm_spring_set(&s->y, LM_H + 3.f);
}

static void sheet_key(lm_overlay_t *o, lm_event_t e)
{
    sheet_t *s = (sheet_t *)o;
    if ((e.key == LM_KEY_PREV || e.key == LM_KEY_NEXT) && lm_ev_step(e)) {
        int d = e.key == LM_KEY_PREV ? -1 : 1;
        int32_t nv = s->d.value + d * s->d.step;
        if (nv < s->d.lo) nv = s->d.lo;
        if (nv > s->d.hi) nv = s->d.hi;
        if (nv == s->d.value) {                    /* at the end stop: shake */
            lm_spring_kick(&s->shake, (float)d * 120.f);
            lm_haptic(o->ui);
            return;
        }
        s->d.value = nv;
        lm_spring_set(&s->fill, sheet_frac(s, nv));
        char b[8]; sheet_fmt(b, nv);
        lm_roll_set(&s->pct, b, d);
        if (s->d.on_change) s->d.on_change(o->ui, nv, s->d.ctx);
    } else if ((e.key == LM_KEY_OK || e.key == LM_KEY_BACK) && e.kind == LM_PRESS) {
        sheet_close(o);
    }
}

static void sheet_update(lm_overlay_t *o, float dt)
{
    sheet_t *s = (sheet_t *)o;
    lm_spring_update(&s->y, dt);
    lm_spring_update(&s->fill, dt);
    lm_spring_update(&s->shake, dt);
    lm_roll_update(&s->pct, dt);
    if (o->closing && s->y.value > LM_H + 1.f) o->dead = true;
}

static void sheet_render(lm_overlay_t *o, lm_gfx_t *g)
{
    sheet_t *s = (sheet_t *)o;
    float yv = s->y.value;
    dim_backdrop(g, lm_remap(yv, LM_H, LM_H - SH, 0, 1));
    float y = (float)lm_roundi(yv);
    const float x0 = 2, w = LM_W - 4;
    lm_gfx_mode(g, LM_CLEAR); lm_rbox(g, x0 - 1, y - 1, w + 2, SH + 10, 6);
    lm_gfx_mode(g, LM_SET);   lm_rframe(g, x0, y, w, SH + 8, 5);
    lm_dotted_hline(g, LM_W / 2.f - 6, y + 3, 13, 2);
    /* sun glyph whose rays grow with the value */
    float sx = x0 + 11, sy = y + 14, v = s->fill.value;
    lm_disc(g, sx, sy, 2.2f + 0.8f * v);
    for (int i = 0; i < 8; i++) {
        float a = (float)i * LM_PI / 4.f, r0 = 4.5f, r1 = 4.5f + 2.2f * lm_clamp01(v);
        lm_line(g, sx + lm_sinf(a) * r0, sy - lm_cosf(a) * r0, sx + lm_sinf(a) * r1, sy - lm_cosf(a) * r1);
    }
    lm_font(g, LM_FONT_UI);
    lm_text(g, x0 + 22, y + 10, s->d.title);
    lm_font(g, LM_FONT_MID);
    int pw = lm_text_w(g, "100%");
    lm_roll_draw(&s->pct, g, x0 + w - 8 - (float)pw, y + 6);
    float bx = x0 + 8 + s->shake.value, by = y + 26, bw = w - 16;
    lm_rframe(g, bx, by, bw, 7, 3);
    lm_rbox(g, bx + 2, by + 2, lm_maxf(3.f, (bw - 4) * lm_clampf(v, 0, 1.05f)), 3, 1);
    for (int i = 0; i < 11; i++) {
        float tx = bx + 2 + (bw - 5) * (float)i / 10.f;
        lm_pixel(g, tx, by + 10);
        if (i % 5 == 0) lm_pixel(g, tx, by + 11);
    }
}

static const lm_spring_t *sheet_probe(lm_overlay_t *o, const char **label)
{
    sheet_t *s = (sheet_t *)o;
    if (o->age < 0.6f || o->closing) { *label = "sheet y  carousel"; return &s->y; }
    *label = "sheet fill  snappy";
    return &s->fill;
}

static const lm_overlay_ops_t SHEET_OPS = { sheet_key, sheet_update, sheet_render, sheet_close, sheet_probe };

void lm_sheet(lm_ui_t *ui, const lm_sheet_desc_t *d)
{
    sheet_t *s = &s_sheet;
    memset(s, 0, sizeof(*s));
    s->base.ops = &SHEET_OPS;
    s->base.modal = true;
    s->d = *d;
    lm_spring_init(&s->y, LM_MOTION_CAROUSEL, LM_H + 2.f, 1.f);
    lm_spring_set(&s->y, (float)(LM_H - SH));
    lm_spring_init(&s->fill, LM_MOTION_SNAPPY, sheet_frac(s, d->value), LM_W);
    lm_spring_init(&s->shake, LM_MOTION_WOBBLE, 0, 1.f);
    char b[8]; sheet_fmt(b, d->value);
    lm_roll_init(&s->pct, LM_FONT_MID, b, 0.35f, 0.05f);
    lm_show(ui, &s->base);
}
