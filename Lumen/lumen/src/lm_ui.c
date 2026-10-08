/*
 * Core runtime. One frame:
 *   input -> dispatch -> update (motion-scaled dt) -> render -> post -> present
 */
#include "lm_ui.h"

#include <string.h>

#include "lm_math.h"

#define PAGE_OF(ui, i) ((ui)->stack[(i)].page)

static void settings_changed(void *ctx, int id, int32_t old_v, int32_t new_v);

/* ======================================================================= init */
void lm_ui_init(lm_ui_t *ui, const lm_port_t *port, const int32_t *defaults, int ndefaults)
{
    memset(ui, 0, sizeof(*ui));
    ui->port = *port;
    ui->keys.layout = port->key_layout;
    lm_gfx_init(&ui->gfx, port->u8g2);
    lm_settings_init(&ui->settings, defaults, ndefaults);
    if (port->settings_load) {
        int32_t v[LM_MAX_SETTINGS];
        memcpy(v, ui->settings.v, sizeof(v));
        if (port->settings_load(port->ctx, v, LM_MAX_SETTINGS))
            memcpy(ui->settings.v, v, sizeof(v));
    }
    ui->settings.dirty = 0;
    lm_subscribe(&ui->settings, settings_changed, ui);

    float b = (float)lm_get(&ui->settings, LM_S_BRIGHTNESS) / 100.f;
    lm_spring_init(&ui->bright, LM_MOTION_GENTLE, b, 500.f);
    lm_spring_init(&ui->dim, LM_MOTION_GENTLE, 1.f, 500.f);
    ui->power = LM_PWR_OFF;
    lm_tween_start(&ui->power_tw, 1, 1, 0.01f, LM_CURVE_LINEAR, 0);
    ui->contrast_sent = 0xFF;
    ui->force_full = true;
    ui->haptic_t = -9.f;
    ui->clock_base.year = 2025; ui->clock_base.month = 6; ui->clock_base.day = 18;
    ui->clock_base.wday = 2; ui->clock_base.hour = 12; ui->clock_base.min = 59;
    u8g2_SetPowerSave(port->u8g2, 1);
}

void lm_ui_start(lm_ui_t *ui, lm_page_t *root)
{
    lm_push(ui, root, LM_TR_NONE);
    lm_wake(ui);
}

/* ======================================================================= services */
void lm_haptic(lm_ui_t *ui)
{
    if (!lm_get(&ui->settings, LM_S_VIBRATE)) return;
    ui->haptic_t = ui->time;
    if (ui->port.haptic) ui->port.haptic(ui->port.ctx, 18);
}

void lm_sleep(lm_ui_t *ui)
{
    if (ui->power == LM_PWR_ON || ui->power == LM_PWR_TURNING_ON) {
        ui->power = LM_PWR_TURNING_OFF;
        lm_tween_start(&ui->power_tw, 0, 1, 0.55f, LM_CURVE_LINEAR, 0);
    }
}

void lm_wake(lm_ui_t *ui)
{
    if (ui->power == LM_PWR_OFF || ui->power == LM_PWR_TURNING_OFF) {
        ui->power = LM_PWR_TURNING_ON;
        lm_tween_start(&ui->power_tw, 1, 0, 0.6f, LM_CURVE_LINEAR, 0);
        ui->idle = 0;
        u8g2_SetPowerSave(ui->port.u8g2, 0);
        ui->force_full = true;
    }
}

static bool leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

void lm_now(const lm_ui_t *ui, lm_datetime_t *dt)
{
    if (ui->port.rtc_get && ui->port.rtc_get(ui->port.ctx, dt))
        return;
    static const uint8_t mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    lm_datetime_t d = ui->clock_base;
    uint32_t t = ui->ms + d.ms;
    d.ms = (uint16_t)(t % 1000); t /= 1000;
    t += d.sec + 60u * (d.min + 60u * d.hour);
    uint32_t days = t / 86400u; t %= 86400u;
    d.hour = (uint8_t)(t / 3600); d.min = (uint8_t)(t / 60 % 60); d.sec = (uint8_t)(t % 60);
    while (days--) {
        d.wday = (uint8_t)((d.wday + 1) % 7);
        int md = mdays[d.month - 1] + (d.month == 2 && leap(d.year));
        if (++d.day > md) { d.day = 1; if (++d.month > 12) { d.month = 1; d.year++; } }
    }
    *dt = d;
}

float lm_hold_progress(const lm_ui_t *ui, lm_key_t k) { return lm_keys_hold(&ui->keys, k, ui->ms); }

bool lm_modal_open(const lm_ui_t *ui)
{
    for (int i = 0; i < ui->nov; i++)
        if (ui->ov[i]->modal && !ui->ov[i]->closing) return true;
    return false;
}

lm_overlay_t *lm_show(lm_ui_t *ui, lm_overlay_t *o)
{
    o->ui = ui;
    for (int i = 0; i < ui->nov; i++)
        if (ui->ov[i] == o) return o;               /* singleton already up */
    if (ui->nov < LM_MAX_OVERLAYS) ui->ov[ui->nov++] = o;
    return o;
}

static void settings_changed(void *ctx, int id, int32_t old_v, int32_t new_v)
{
    lm_ui_t *ui = (lm_ui_t *)ctx;
    if (id == LM_S_BRIGHTNESS) {
        lm_spring_set(&ui->bright, (float)new_v / 100.f);
    } else if (id == LM_S_INVERT) {
        ui->inv_from = old_v != 0;
        ui->inv_active = true;
        lm_tween_start(&ui->inv_tw, 0, 1, 0.6f, LM_CURVE_INOUT, 0);
    }
}

/* ======================================================================= transitions */
static void drive_init(lm_drive_t *d, lm_tr_kind_t kind, bool forward)
{
    memset(d, 0, sizeof(*d));
    if (kind == LM_TR_ZOOM) {
        d->use_spring = true;
        /* progress 0..1 spans the screen width: rest within ~0.35 px */
        lm_spring_init(&d->s, forward ? LM_MOTION_ZOOM : LM_MOTION_CAROUSEL, 0.f, (float)LM_W);
        lm_spring_set(&d->s, 1.f);
    } else if (kind == LM_TR_FADE) {
        lm_tween_start(&d->tw, 0, 1, 0.55f, LM_CURVE_INOUT, 0);
    } else {
        lm_tween_start(&d->tw, 0, 1, 0.42f, LM_CURVE_STANDARD, 0);
    }
}

static float drive_value(const lm_drive_t *d) { return d->use_spring ? d->s.value : lm_tween_value(&d->tw); }
static bool drive_done(const lm_drive_t *d) { return d->use_spring ? d->s.rest : lm_tween_done(&d->tw); }

static void drive_reverse(lm_drive_t *d)
{
    if (d->use_spring) {
        /* heading home should not wind up again: positive zero */
        lm_spring_motion(&d->s, LM_MOTION_CAROUSEL);
        lm_spring_set(&d->s, 0.f);
    } else {
        float v = lm_tween_value(&d->tw);
        float dur = d->tw.dur * lm_absf(v);
        lm_tween_start(&d->tw, v, 0.f, dur < 0.16f ? 0.16f : dur, LM_CURVE_STANDARD, 0);
    }
}

static void tr_begin(lm_ui_t *ui, lm_tr_kind_t kind, lm_page_t *old_p, lm_page_t *new_p, bool forward)
{
    lm_transition_t *t = &ui->tr;
    if (kind == LM_TR_NONE || !old_p) { t->active = false; return; }
    t->active = true;
    t->kind = kind;
    t->old_p = old_p;
    t->new_p = new_p;
    t->forward = forward;
    t->reversed = false;
    t->rect[0] = LM_W / 2 - 12; t->rect[1] = LM_H / 2 - 12; t->rect[2] = 24; t->rect[3] = 24;
    lm_page_t *anchor = forward ? old_p : new_p;       /* the page owning the icon */
    if (anchor->ops->focus) anchor->ops->focus(anchor, t->rect);
    drive_init(&t->d, kind, forward);
}

/* Strategy: each kind composes old (a) and new (b) into the screen. */
static void compose_slide(lm_transition_t *t, float e, uint8_t *out, uint8_t *a, uint8_t *b)
{
    uint8_t *under, *over;
    float ux, ox, uf;
    if (t->forward) { under = a; over = b; ux = -40.f * e; ox = LM_W * (1 - e); uf = 1 - 0.75f * e; }
    else            { under = b; over = a; ux = -40.f * (1 - e); ox = LM_W * e; uf = 0.25f + 0.75f * e; }
    memset(out, 0, LM_FB_BYTES);
    lm_buf_blit(out, under, lm_roundi(ux), 0, uf);
    int oxi = lm_roundi(ox);
    for (int p = 0; p < LM_PAGES; p++)                 /* the incoming sheet is opaque */
        for (int x = lm_maxi(0, oxi); x < LM_W; x++) out[p * LM_W + x] = 0;
    lm_buf_blit(out, over, oxi, 0, 1.f);
    if (oxi > 0 && oxi < LM_W)
        for (int y = oxi & 1; y < LM_H; y += 2) lm_buf_put(out, oxi - 1, y, true);
}

static void compose_zoom(lm_transition_t *t, float e, uint8_t *out, uint8_t *a, uint8_t *b)
{
    /* e may leave [0,1]: below 0 is the anticipation squeeze, above 1 the
     * page landing a little past full screen */
    uint8_t *bg = a, *fg = b;
    if (!t->forward) { e = 1 - e; bg = b; fg = a; }
    float rx = t->rect[0], ry = t->rect[1], rw = t->rect[2], rh = t->rect[3];
    float pcx = rx + rw / 2, pcy = ry + rh / 2;
    float s = 1 + 0.45f * e;
    memset(out, 0, LM_FB_BYTES);
    /* camera flies into the icon: background scales about the icon centre */
    lm_buf_blit_scaled(out, bg, pcx - pcx * s, pcy - pcy * s, LM_W * s, LM_H * s, lm_clamp01(1 - e * 1.4f));
    float x = lm_lerpf(rx, 0, e), y = lm_lerpf(ry, 0, e);
    float w = lm_lerpf(rw, LM_W, e), h = lm_lerpf(rh, LM_H, e);
    float r = lm_lerpf(6, 0, lm_clamp01(e * 1.15f));
    float lvl = lm_clamp01(e * 3.f);
    if (lvl > 0 && w >= 1 && h >= 1)
        lm_buf_blit_region(out, fg, 0, 0, LM_W, LM_H, x, y, w, h, r, lvl, true);
    if (e < 0.97f && w >= 1 && h >= 1) {
        lm_gfx_t *g = &t->new_p->ui->gfx;
        lm_gfx_target(g, out);
        lm_rframe(g, (float)lm_roundi(x), (float)lm_roundi(y), (float)lm_roundi(w), (float)lm_roundi(h), r);
    }
}

static void compose_fade(lm_transition_t *t, float e, uint8_t *out, uint8_t *a, uint8_t *b)
{
    (void)t;
    /* ordered dissolve: new page where the dither mask is on, old elsewhere */
    uint8_t m[4];
    for (int i = 0; i < 4; i++) m[i] = lm_dither_col(i, e);
    for (int p = 0; p < LM_PAGES; p++)
        for (int x = 0; x < LM_W; x++) {
            int k = p * LM_W + x;
            out[k] = (uint8_t)((b[k] & m[x & 3]) | (a[k] & (uint8_t)~m[x & 3]));
        }
}

typedef void (*compose_fn)(lm_transition_t *, float, uint8_t *, uint8_t *, uint8_t *);
static const compose_fn COMPOSE[] = { 0, compose_slide, compose_zoom, compose_fade };

/* ======================================================================= navigation */
static void attach(lm_ui_t *ui, lm_page_t *p) { p->ui = ui; p->age = 0; }

void lm_push(lm_ui_t *ui, lm_page_t *p, lm_tr_kind_t how)
{
    if (ui->depth >= LM_MAX_STACK) return;
    lm_page_t *old = lm_top(ui);
    attach(ui, p);
    if (p->ops->enter) p->ops->enter(p);
    ui->stack[ui->depth].page = p;
    ui->stack[ui->depth].how = how;
    ui->depth++;
    if (old) tr_begin(ui, how, old, p, true);
}

void lm_replace(lm_ui_t *ui, lm_page_t *p, lm_tr_kind_t how)
{
    lm_page_t *old = lm_top(ui);
    attach(ui, p);
    if (p->ops->enter) p->ops->enter(p);
    if (!ui->depth) { lm_push(ui, p, LM_TR_NONE); return; }
    ui->stack[ui->depth - 1].page = p;
    ui->stack[ui->depth - 1].how = LM_TR_NONE;
    if (old->ops->leave) old->ops->leave(old);
    tr_begin(ui, how, old, p, true);
}

void lm_pop(lm_ui_t *ui)
{
    if (ui->depth <= 1) return;
    ui->depth--;
    lm_page_t *old = PAGE_OF(ui, ui->depth);
    lm_tr_kind_t how = ui->stack[ui->depth].how;
    lm_page_t *nw = lm_top(ui);
    if (nw->ops->resume) nw->ops->resume(nw);
    tr_begin(ui, how ? how : LM_TR_SLIDE, old, nw, false);
    if (old->ops->leave) old->ops->leave(old);
}

void lm_home(lm_ui_t *ui)
{
    if (ui->depth <= 1) return;
    lm_page_t *old = lm_top(ui);
    lm_tr_kind_t how = ui->stack[1].how;
    for (int i = ui->depth - 1; i >= 1; i--) {
        lm_page_t *p = PAGE_OF(ui, i);
        if (p->ops->leave) p->ops->leave(p);
    }
    ui->depth = 1;
    lm_page_t *nw = lm_top(ui);
    if (nw->ops->resume) nw->ops->resume(nw);
    tr_begin(ui, how ? how : LM_TR_SLIDE, old, nw, false);
}

/* ======================================================================= input */
static void dispatch(lm_ui_t *ui, lm_event_t e)
{
    for (int i = ui->nov - 1; i >= 0; i--) {                /* 1. modal overlay */
        lm_overlay_t *o = ui->ov[i];
        if (o->modal && !o->closing) {
            if (o->ops->key) o->ops->key(o, e);
            return;
        }
    }
    if (e.key == LM_KEY_BACK && e.kind == LM_LONG) {        /* 2. global gesture */
        if (ui->depth > 1) lm_home(ui); else lm_sleep(ui);
        return;
    }
    lm_page_t *top = lm_top(ui);                            /* 3. page */
    if (top && top->ops->key && top->ops->key(top, e)) return;
    if (e.key == LM_KEY_BACK && e.kind == LM_PRESS && ui->depth > 1)   /* 4. default */
        lm_pop(ui);
}

static void handle_key(lm_ui_t *ui, lm_event_t e)
{
    ui->idle = 0;
    if (ui->power == LM_PWR_OFF || ui->power == LM_PWR_TURNING_OFF) { lm_wake(ui); return; }
    if (ui->dimmed) {                        /* first key only wakes the panel */
        ui->dimmed = false;
        lm_spring_set(&ui->dim, 1.f);
        return;
    }
    lm_transition_t *t = &ui->tr;
    if (t->active) {
        if (e.key == LM_KEY_BACK && e.kind == LM_PRESS && t->forward && !t->reversed &&
            t->new_p == lm_top(ui) && ui->depth > 1 && !lm_modal_open(ui)) {
            /* changed their mind mid-flight: run the same motion backwards
             * from where it is, with its current velocity */
            ui->depth--;
            if (t->old_p->ops->resume) t->old_p->ops->resume(t->old_p);
            if (t->new_p->ops->leave) t->new_p->ops->leave(t->new_p);
            t->reversed = true;
            drive_reverse(&t->d);
            ui->deferred.head = ui->deferred.tail = 0;
            return;
        }
        lm_evq_push(&ui->deferred, e);       /* never dropped: replayed on landing */
        return;
    }
    dispatch(ui, e);
}

void lm_ui_post(lm_ui_t *ui, lm_key_t key, lm_kind_t kind)
{
    lm_event_t e = { (uint8_t)key, (uint8_t)kind };
    lm_evq_push(&ui->evq, e);
}

/* ======================================================================= update */
static void page_update(lm_page_t *p, float dt)
{
    p->age += dt;
    if (p->age > 3600.f) p->age -= 3000.f;   /* keep float phases precise */
    if (p->ops->update) p->ops->update(p, dt);
}

static void update(lm_ui_t *ui, float dt)
{
    float adt = dt * (float)lm_get(&ui->settings, LM_S_MOTION) / 100.f;

    lm_tween_update(&ui->power_tw, dt);
    if (ui->power == LM_PWR_TURNING_OFF && lm_tween_done(&ui->power_tw)) {
        ui->power = LM_PWR_OFF;
        u8g2_SetPowerSave(ui->port.u8g2, 1);
    } else if (ui->power == LM_PWR_TURNING_ON && lm_tween_done(&ui->power_tw)) {
        ui->power = LM_PWR_ON;
    }

    if (ui->power == LM_PWR_ON) {
        ui->idle += dt;
        float lim = (float)lm_get(&ui->settings, LM_S_AUTOSLEEP);
        if (lim > 0 && !ui->dimmed && ui->idle > lm_maxf(lim - 5.f, lim * 0.6f)) {
            ui->dimmed = true;
            lm_spring_set(&ui->dim, 0.25f);
        }
        if (lim > 0 && ui->idle > lim) {
            ui->dimmed = false;
            lm_spring_set(&ui->dim, 1.f);
            lm_sleep(ui);
        }
    }
    if (ui->power == LM_PWR_OFF) return;           /* frozen while off */

    lm_transition_t *t = &ui->tr;
    lm_page_t *top = lm_top(ui);
    if (t->active) {
        if (t->d.use_spring) lm_spring_update(&t->d.s, adt);
        else lm_tween_update(&t->d.tw, adt);
        if (t->old_p != top) page_update(t->old_p, adt);  /* leaving pages keep moving */
        if (t->new_p != top) page_update(t->new_p, adt);
    }
    if (top) page_update(top, adt);

    for (int i = 0; i < ui->nov; i++) {
        lm_overlay_t *o = ui->ov[i];
        o->age += adt;
        if (o->ops->update) o->ops->update(o, adt);
    }
    int n = 0;
    for (int i = 0; i < ui->nov; i++) if (!ui->ov[i]->dead) ui->ov[n++] = ui->ov[i];
    ui->nov = (uint8_t)n;

    if (t->active && drive_done(&t->d)) {
        t->active = false;
        lm_event_t e;
        while (!t->active && lm_evq_pop(&ui->deferred, &e)) dispatch(ui, e);
    }

    lm_spring_update(&ui->bright, dt);
    lm_spring_update(&ui->dim, dt);
    if (ui->inv_active) {
        lm_tween_update(&ui->inv_tw, dt);
        if (lm_tween_done(&ui->inv_tw)) ui->inv_active = false;
    }
    if (ui->settings.dirty) {
        ui->save_timer += dt;
        if (ui->save_timer > 1.0f) {             /* debounce flash writes */
            ui->settings.dirty = 0;
            ui->save_timer = 0;
            if (ui->port.settings_save) ui->port.settings_save(ui->port.ctx, ui->settings.v, LM_MAX_SETTINGS);
        }
    }
}

/* ======================================================================= post effects */
static void xor_span(uint8_t *buf, int y, int x0, int x1)
{
    x0 = lm_maxi(0, x0); x1 = lm_mini(LM_W, x1);
    uint8_t bit = (uint8_t)(1u << (y & 7));
    uint8_t *row = buf + (y >> 3) * LM_W;
    for (int x = x0; x < x1; x++) row[x] ^= bit;
}

/* invert, with a circular wipe from the centre while it changes */
static void post_invert(lm_ui_t *ui, uint8_t *buf)
{
    bool inv = lm_get(&ui->settings, LM_S_INVERT) != 0;
    if (!ui->inv_active) {
        if (inv) lm_buf_xor_all(buf);
        return;
    }
    float p = lm_tween_value(&ui->inv_tw);
    float R = sqrtf(64.f * 64.f + 32.f * 32.f) + 2.f;
    float r = p * R, r2 = r + 2.5f;
    bool ring = p < 0.98f;
    for (int y = 0; y < LM_H; y++) {
        float dy = (float)y + 0.5f - LM_H / 2.f;
        /* outside: inv_from, inside: inv, ring: flipped */
        if (ui->inv_from) xor_span(buf, y, 0, LM_W);
        float d = r * r - dy * dy;
        int a = LM_W / 2, b = LM_W / 2;
        if (d > 0) { float hw = sqrtf(d); a = (int)ceilf(LM_W / 2.f - hw - 0.5f); b = (int)floorf(LM_W / 2.f + hw - 0.5f) + 1; }
        if (inv != ui->inv_from && b > a) xor_span(buf, y, a, b);
        if (ring) {
            float d2 = r2 * r2 - dy * dy;
            if (d2 > 0) {
                float hw = sqrtf(d2);
                int a2 = (int)ceilf(LM_W / 2.f - hw - 0.5f), b2 = (int)floorf(LM_W / 2.f + hw - 0.5f) + 1;
                if (b > a) { xor_span(buf, y, a2, a); xor_span(buf, y, b, b2); }
                else xor_span(buf, y, a2, b2);
            }
        }
    }
}

static float in_cubic(float t) { return t * t * t; }
static float inout_cubic(float t) { return t < .5f ? 4 * t * t * t : 1 - ((-2 * t + 2) * (-2 * t + 2) * (-2 * t + 2)) / 2; }

/* CRT power-off/on: squash to a line, shrink to a dot, fade. c: 0 on .. 1 off */
static float post_crt(uint8_t *buf, uint8_t *tmp, float c)
{
    if (c <= 0.f) return 1.f;
    memcpy(tmp, buf, LM_FB_BYTES);
    memset(buf, 0, LM_FB_BYTES);
    int cy = LM_H / 2;
    if (c < 0.5f) {
        float s = 1 - in_cubic(c / 0.5f);
        int h = lm_maxi(1, lm_roundi(LM_H * s));
        int y0 = cy - h / 2;
        /* the collapsing beam saturates into a solid bright bar */
        float fill = s < 0.35f ? lm_remap(s, 0.35f, 0.05f, 0, 1) : 0.f;
        uint8_t fm[4];
        for (int i = 0; i < 4; i++) fm[i] = lm_dither_col(i, fill);
        for (int i = 0; i < h; i++) {
            int sy = lm_mini(LM_H - 1, (int)(((float)i + 0.5f) * LM_H / (float)h));
            int y = y0 + i;
            if (y < 0 || y >= LM_H) continue;
            for (int x = 0; x < LM_W; x++) {
                bool on = lm_buf_get(tmp, x, sy) || ((fm[x & 3] >> (y & 7)) & 1u);
                if (on) lm_buf_put(buf, x, y, true);
            }
        }
        return 1.f;
    }
    if (c < 0.82f) {
        float e = inout_cubic((c - 0.5f) / 0.32f);
        int w = lm_maxi(2, lm_roundi(LM_W * (1 - e)));
        int x0 = LM_W / 2 - w / 2;
        for (int x = x0; x < x0 + w; x++) { lm_buf_put(buf, x, cy - 1, true); lm_buf_put(buf, x, cy, true); }
        return 1.f;
    }
    float e = (c - 0.82f) / 0.18f;
    if (e < 1.f) {
        float r = 2.2f * (1 - e) + 0.6f;
        for (int y = cy - 4; y <= cy + 4; y++)
            for (int x = LM_W / 2 - 4; x <= LM_W / 2 + 4; x++) {
                float dx = (float)x + 0.5f - LM_W / 2.f, dy = (float)y + 0.5f - (float)cy;
                if (dx * dx + dy * dy <= r * r) lm_buf_put(buf, x, y, true);
            }
        return 1.f - e * 0.6f;
    }
    return 0.f;
}

/* ======================================================================= render/present */
static uint32_t fnv(const uint8_t *p, int n)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

static void render(lm_ui_t *ui)
{
    lm_gfx_t *g = &ui->gfx;
    uint8_t *screen = lm_gfx_screen(g);
    lm_transition_t *t = &ui->tr;
    if (t->active) {
        lm_gfx_target(g, ui->buf_a); lm_gfx_clear(g);
        if (t->old_p->ops->render) t->old_p->ops->render(t->old_p, g);
        lm_gfx_target(g, ui->buf_b); lm_gfx_clear(g);
        if (t->new_p->ops->render) t->new_p->ops->render(t->new_p, g);
        COMPOSE[t->kind](t, drive_value(&t->d), screen, ui->buf_a, ui->buf_b);
        lm_gfx_target(g, screen);
    } else {
        lm_gfx_target(g, screen);
        lm_gfx_clear(g);
        lm_page_t *top = lm_top(ui);
        if (top && top->ops->render) top->ops->render(top, g);
    }
    for (int i = 0; i < ui->nov; i++) {
        lm_overlay_t *o = ui->ov[i];
        lm_gfx_target(g, screen);
        if (o->ops->render) o->ops->render(o, g);
    }
    lm_gfx_target(g, screen);
    /* hold hint: a bar grows from the centre of the bottom edge while a
     * hold-sensitive key is down. With an intermediate stage (3-key ENTER:
     * release here = BACK) tick marks show where it begins, so the user can
     * see which action letting go will trigger. */
    float hp = lm_maxf(lm_hold_progress(ui, LM_KEY_BACK), lm_hold_progress(ui, LM_KEY_OK));
    float mark = lm_keys_hold_mark(&ui->keys, LM_KEY_OK);
    float show = mark > 0 ? mark * 0.5f : 0.15f;     /* don't flash on quick taps */
    if (hp > show) {
        float w = LM_W * lm_curve(LM_CURVE_STANDARD, (hp - show) / (1.f - show));
        lm_gfx_mode(g, LM_XOR);
        lm_hline(g, LM_W / 2 - w / 2, LM_H - 1, w);
        if (mark > 0) {
            float mw = LM_W * lm_curve(LM_CURVE_STANDARD, (mark - show) / (1.f - show));
            lm_vline(g, LM_W / 2 - mw / 2, LM_H - 3, 2);
            lm_vline(g, LM_W / 2 + mw / 2, LM_H - 3, 2);
        }
        lm_gfx_mode(g, LM_SET);
    }
}

static void present(lm_ui_t *ui, float level)
{
    u8g2_t *u = ui->port.u8g2;
    int c = lm_roundi(lm_clamp01(level) * 255.f);
    if (c != ui->contrast_sent) {
        u8g2_SetContrast(u, (uint8_t)c);
        ui->contrast_sent = (uint8_t)c;
    }
    const uint8_t *buf = u8g2_GetBufferPtr(u);
    bool changed = false;
    for (int p = 0; p < LM_PAGES; p++) {
        uint32_t h = fnv(buf + p * LM_W, LM_W);
        if (ui->force_full || h != ui->page_hash[p]) {
            ui->page_hash[p] = h;
            u8g2_UpdateDisplayArea(u, 0, (uint8_t)p, LM_W / 8, 1);   /* only dirty pages */
            changed = true;
        }
    }
    ui->force_full = false;
    ui->still_frames = changed ? 0 : (uint16_t)lm_mini(ui->still_frames + 1, 60000);
}

void lm_ui_step(lm_ui_t *ui, float dt)
{
    if (dt > LM_MAX_DT) dt = LM_MAX_DT;
    if (dt < 0) dt = 0;
    ui->ms += (uint32_t)(dt * 1000.f + 0.5f);
    ui->time += dt;
    if (ui->time > 3600.f) { ui->time -= 3600.f; ui->haptic_t -= 3600.f; }
    ui->frames++;
    if (dt > 0) ui->fps = ui->fps * 0.95f + 0.05f / dt;

    if (ui->port.read_keys) lm_keys_scan(&ui->keys, ui->port.read_keys(ui->port.ctx), ui->ms, &ui->evq);
    lm_event_t e;
    while (lm_evq_pop(&ui->evq, &e)) handle_key(ui, e);

    update(ui, dt);
    if (ui->power == LM_PWR_OFF) { ui->still_frames = (uint16_t)lm_mini(ui->still_frames + 1, 60000); return; }

    render(ui);
    uint8_t *screen = lm_gfx_screen(&ui->gfx);
    post_invert(ui, screen);
    float lvl = 1.f;
    if (ui->power != LM_PWR_ON) lvl = post_crt(screen, ui->buf_a, lm_tween_value(&ui->power_tw));
    present(ui, lm_clamp01(ui->bright.value * ui->dim.value) * lvl);
}

void lm_ui_tick(lm_ui_t *ui)
{
    uint32_t now = ui->port.millis(ui->port.ctx);
    if (!ui->started) { ui->started = true; ui->last_ms = now; }
    float dt = (float)(now - ui->last_ms) / 1000.f;
    ui->last_ms = now;
    lm_ui_step(ui, dt);
}

bool lm_ui_idle(const lm_ui_t *ui)
{
    return !ui->tr.active && ui->still_frames > 30 && ui->power != LM_PWR_TURNING_ON &&
           ui->power != LM_PWR_TURNING_OFF;
}

const void *lm_probe(lm_ui_t *ui, const char **label, float *value, float *target)
{
    static const char *tr_names[] = { "", "slide  bezier(.2,0,0,1)", "zoom  2nd f=2.3 z=.78 r=-.8",
                                      "fade  bezier(.65,0,.35,1)" };
    const lm_spring_t *s = 0;
    *label = "";
    if (ui->tr.active) {                         /* the driver itself is the identity */
        *label = ui->tr.reversed ? "transition reversed" : tr_names[ui->tr.kind];
        *value = drive_value(&ui->tr.d);
        *target = ui->tr.d.use_spring ? ui->tr.d.s.target : ui->tr.d.tw.b;
        return &ui->tr;
    }
    for (int i = ui->nov - 1; i >= 0; i--) {
        lm_overlay_t *o = ui->ov[i];
        if (o->modal && o->ops->probe) { s = o->ops->probe(o, label); break; }
    }
    lm_page_t *top = lm_top(ui);
    if (!s && top && top->ops->probe) s = top->ops->probe(top, label);
    if (s) { *value = s->value; *target = s->target; }
    return s;
}
