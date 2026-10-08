/*
 * Rasteriser. Every primitive is reduced to horizontal spans in screen
 * space; span() applies clip, mode and ordered dither, so behaviour is
 * identical for shapes, text (via the u8g2 hook) and u8g2 primitives.
 */
#include "lm_gfx.h"

#include <string.h>

#include "lm_math.h"

/* 4x4 Bayer thresholds 0..15; a pixel is kept when BAYER < alpha (0..16). */
static const uint8_t BAYER[4][4] = {
    { 0,  8,  2, 10},
    {12,  4, 14,  6},
    { 3, 11,  1,  9},
    {15,  7, 13,  5},
};

/* u8g2's ll_hvline callback carries no user context (u8x8.user_ptr only
 * exists with U8X8_WITH_USER_PTR, off by default). One canvas draws at a
 * time, so the active one is tracked here; set whenever text is drawn. */
static lm_gfx_t *s_active;

/* ---------------------------------------------------------------- pixels */
static inline void put(uint8_t *buf, int x, int y, lm_mode_t m)
{
    uint8_t *p = &buf[(y >> 3) * LM_W + x];
    uint8_t bit = (uint8_t)(1u << (y & 7));
    if (m == LM_SET) *p |= bit;
    else if (m == LM_CLEAR) *p &= (uint8_t)~bit;
    else *p ^= bit;
}

bool lm_buf_get(const uint8_t *buf, int x, int y)
{
    if ((unsigned)x >= LM_W || (unsigned)y >= LM_H) return false;
    return (buf[(y >> 3) * LM_W + x] >> (y & 7)) & 1u;
}

void lm_buf_put(uint8_t *buf, int x, int y, bool on)
{
    if ((unsigned)x >= LM_W || (unsigned)y >= LM_H) return;
    put(buf, x, y, on ? LM_SET : LM_CLEAR);
}

/* span [x0, x1) on screen row y through clip/mode/alpha */
static void span(lm_gfx_t *g, int x0, int x1, int y)
{
    const lm_clip_t *c = &g->clip;
    if (y < c->y0 || y >= c->y1) return;
    if (x0 < c->x0) x0 = c->x0;
    if (x1 > c->x1) x1 = c->x1;
    if (x0 >= x1 || g->alpha == 0) return;
    uint8_t *row = g->target + (y >> 3) * LM_W;
    uint8_t bit = (uint8_t)(1u << (y & 7));
    if (g->alpha >= 16) {
        if (g->mode == LM_SET)        for (int x = x0; x < x1; x++) row[x] |= bit;
        else if (g->mode == LM_CLEAR) for (int x = x0; x < x1; x++) row[x] &= (uint8_t)~bit;
        else                          for (int x = x0; x < x1; x++) row[x] ^= bit;
        return;
    }
    const uint8_t *b = BAYER[y & 3];
    for (int x = x0; x < x1; x++) {
        if (b[x & 3] >= g->alpha) continue;
        if (g->mode == LM_SET) row[x] |= bit;
        else if (g->mode == LM_CLEAR) row[x] &= (uint8_t)~bit;
        else row[x] ^= bit;
    }
}

static inline void plot(lm_gfx_t *g, int x, int y) { span(g, x, x + 1, y); }

/* u8g2 -> Lumen bridge: coordinates arrive clipped to the u8g2 window */
static void hook_hvline(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t len, uint8_t dir)
{
    lm_gfx_t *g = s_active;
    if (!g) return;
    lm_mode_t save = g->mode;
    if (u8g2->draw_color == 0) g->mode = LM_CLEAR;       /* solid-font background */
    if (dir == 0) {
        span(g, (int)x, (int)x + (int)len, (int)y);
    } else {
        for (int i = 0; i < (int)len; i++) plot(g, (int)x, (int)y + i);
    }
    g->mode = save;
}

/* ---------------------------------------------------------------- context */
void lm_gfx_init(lm_gfx_t *g, u8g2_t *u8g2)
{
    memset(g, 0, sizeof(*g));
    g->u8g2 = u8g2;
    u8g2->ll_hvline = hook_hvline;
    u8g2_SetMaxClipWindow(u8g2);
    u8g2_SetDrawColor(u8g2, 1);
    u8g2_SetFontMode(u8g2, 1);          /* transparent glyph background */
    u8g2_SetFontPosTop(u8g2);
    lm_gfx_target(g, u8g2_GetBufferPtr(u8g2));
    s_active = g;
}

void lm_gfx_target(lm_gfx_t *g, uint8_t *buf)
{
    g->target = buf;
    g->mode = LM_SET;
    g->alpha = 16;
    g->ox = g->oy = 0;
    g->clip.x0 = 0; g->clip.y0 = 0; g->clip.x1 = LM_W; g->clip.y1 = LM_H;
    g->clip_sp = g->off_sp = 0;
    s_active = g;
}

void lm_gfx_clear(lm_gfx_t *g) { memset(g->target, 0, LM_FB_BYTES); }

void lm_gfx_alpha(lm_gfx_t *g, float a)
{
    int v = lm_roundi(a * 16.f);
    g->alpha = (uint8_t)(v < 0 ? 0 : v > 16 ? 16 : v);
}

void lm_gfx_clip_push(lm_gfx_t *g, float x, float y, float w, float h)
{
    if (g->clip_sp < LM_GFX_CLIP_DEPTH) g->clip_stack[g->clip_sp] = g->clip;
    g->clip_sp++;
    int x0 = lm_roundi(x) + g->ox, y0 = lm_roundi(y) + g->oy;
    int x1 = x0 + lm_roundi(w), y1 = y0 + lm_roundi(h);
    lm_clip_t *c = &g->clip;
    c->x0 = (int16_t)lm_maxi(c->x0, x0);
    c->y0 = (int16_t)lm_maxi(c->y0, y0);
    c->x1 = (int16_t)lm_mini(c->x1, x1);
    c->y1 = (int16_t)lm_mini(c->y1, y1);
    if (c->x1 < c->x0) c->x1 = c->x0;
    if (c->y1 < c->y0) c->y1 = c->y0;
}

void lm_gfx_clip_pop(lm_gfx_t *g)
{
    if (!g->clip_sp) return;
    g->clip_sp--;
    if (g->clip_sp < LM_GFX_CLIP_DEPTH) g->clip = g->clip_stack[g->clip_sp];
}

void lm_gfx_off_push(lm_gfx_t *g, float dx, float dy)
{
    if (g->off_sp < LM_GFX_OFF_DEPTH) {
        g->off_stack[g->off_sp][0] = g->ox;
        g->off_stack[g->off_sp][1] = g->oy;
    }
    g->off_sp++;
    g->ox = (int16_t)(g->ox + lm_roundi(dx));
    g->oy = (int16_t)(g->oy + lm_roundi(dy));
}

void lm_gfx_off_pop(lm_gfx_t *g)
{
    if (!g->off_sp) return;
    g->off_sp--;
    if (g->off_sp < LM_GFX_OFF_DEPTH) {
        g->ox = g->off_stack[g->off_sp][0];
        g->oy = g->off_stack[g->off_sp][1];
    }
}

/* ---------------------------------------------------------------- rects */
void lm_pixel(lm_gfx_t *g, float x, float y) { plot(g, lm_roundi(x) + g->ox, lm_roundi(y) + g->oy); }

void lm_hline(lm_gfx_t *g, float x, float y, float w)
{
    int xi = lm_roundi(x) + g->ox, wi = lm_roundi(w);
    if (wi > 0) span(g, xi, xi + wi, lm_roundi(y) + g->oy);
}

void lm_vline(lm_gfx_t *g, float x, float y, float h)
{
    int xi = lm_roundi(x) + g->ox, yi = lm_roundi(y) + g->oy, hi = lm_roundi(h);
    for (int i = 0; i < hi; i++) plot(g, xi, yi + i);
}

void lm_box(lm_gfx_t *g, float x, float y, float w, float h)
{
    int xi = lm_roundi(x) + g->ox, yi = lm_roundi(y) + g->oy;
    int wi = lm_roundi(w), hi = lm_roundi(h);
    if (wi <= 0 || hi <= 0) return;
    for (int r = 0; r < hi; r++) span(g, xi, xi + wi, yi + r);
}

void lm_frame(lm_gfx_t *g, float x, float y, float w, float h)
{
    int xi = lm_roundi(x), yi = lm_roundi(y), wi = lm_roundi(w), hi = lm_roundi(h);
    if (wi <= 0 || hi <= 0) return;
    lm_hline(g, (float)xi, (float)yi, (float)wi);
    if (hi > 1) lm_hline(g, (float)xi, (float)(yi + hi - 1), (float)wi);
    if (hi > 2) {
        lm_vline(g, (float)xi, (float)(yi + 1), (float)(hi - 2));
        if (wi > 1) lm_vline(g, (float)(xi + wi - 1), (float)(yi + 1), (float)(hi - 2));
    }
}

/* Left inset of row `ry` of a w*h rounded rect with radius r: same corner
 * test as the reference (pixel centre inside the quarter disc). */
static int rr_inset(int w, int h, int r, int ry)
{
    if (r <= 0) return 0;
    int cy;
    if (ry < r) cy = ry;
    else if (ry >= h - r) cy = h - 1 - ry;
    else return 0;
    float dy = (float)cy + 0.5f - (float)r;
    float lim = (float)(r * r) + 0.5f;
    for (int xx = 0; xx < r; xx++) {
        float dx = (float)xx + 0.5f - (float)r;
        if (dx * dx + dy * dy <= lim) return xx;
    }
    return r;
}

static int rr_clamp(int w, int h, float r)
{
    int ri = lm_roundi(r);
    ri = lm_mini(ri, lm_mini(w / 2, h / 2));
    return ri < 0 ? 0 : ri;
}

void lm_rbox(lm_gfx_t *g, float x, float y, float w, float h, float r)
{
    int xi = lm_roundi(x) + g->ox, yi = lm_roundi(y) + g->oy;
    int wi = lm_roundi(w), hi = lm_roundi(h);
    if (wi <= 0 || hi <= 0) return;
    int ri = rr_clamp(wi, hi, r);
    for (int ry = 0; ry < hi; ry++) {
        int in = rr_inset(wi, hi, ri, ry);
        span(g, xi + in, xi + wi - in, yi + ry);
    }
}

void lm_rframe(lm_gfx_t *g, float x, float y, float w, float h, float r)
{
    int xi = lm_roundi(x) + g->ox, yi = lm_roundi(y) + g->oy;
    int wi = lm_roundi(w), hi = lm_roundi(h);
    if (wi <= 0 || hi <= 0) return;
    int ro = rr_clamp(wi, hi, r);
    int iw = wi - 2, ih = hi - 2;
    int rin = (iw > 0 && ih > 0) ? rr_clamp(iw, ih, (float)(ro - 1)) : 0;
    for (int ry = 0; ry < hi; ry++) {
        int a = rr_inset(wi, hi, ro, ry);
        int x0 = xi + a, x1 = xi + wi - a;
        int iry = ry - 1;
        if (iw > 0 && ih > 0 && iry >= 0 && iry < ih) {
            int b = rr_inset(iw, ih, rin, iry);
            int i0 = xi + 1 + b, i1 = xi + 1 + iw - b;
            if (i0 < i1) {
                span(g, x0, i0, yi + ry);
                span(g, i1, x1, yi + ry);
                continue;
            }
        }
        span(g, x0, x1, yi + ry);
    }
}

/* ---------------------------------------------------------------- round */
/* pixels whose centre lies within the half-width hw of centre c */
static inline void row_extent(float c, float hw, int *a, int *b)
{
    *a = (int)ceilf(c - hw - 0.5f);
    *b = (int)floorf(c + hw - 0.5f) + 1;   /* exclusive */
}

void lm_disc(lm_gfx_t *g, float cx, float cy, float r)
{
    if (r <= 0.f) return;
    float sx = cx + g->ox, sy = cy + g->oy;
    int y0 = (int)floorf(sy - r), y1 = (int)ceilf(sy + r);
    for (int y = y0; y <= y1; y++) {
        float dy = (float)y + 0.5f - sy;
        float d = r * r - dy * dy;
        if (d < 0.f) continue;
        int a, b;
        row_extent(sx, sqrtf(d), &a, &b);
        span(g, a, b, y);
    }
}

void lm_ring(lm_gfx_t *g, float cx, float cy, float r, float thick)
{
    if (r <= 0.f) return;
    float ri = r - thick;
    float sx = cx + g->ox, sy = cy + g->oy;
    int y0 = (int)floorf(sy - r), y1 = (int)ceilf(sy + r);
    for (int y = y0; y <= y1; y++) {
        float dy = (float)y + 0.5f - sy;
        float d = r * r - dy * dy;
        if (d < 0.f) continue;
        int a, b;
        row_extent(sx, sqrtf(d), &a, &b);
        float di = ri * ri - dy * dy;
        if (ri > 0.f && di > 0.f) {
            int ia, ib;
            row_extent(sx, sqrtf(di), &ia, &ib);
            span(g, a, ia, y);
            span(g, ib, b, y);
        } else {
            span(g, a, b, y);
        }
    }
}

void lm_arc(lm_gfx_t *g, float cx, float cy, float r, float a0, float a1, float thick)
{
    if (r <= 0.f || a1 <= a0) return;
    float span_a = a1 - a0;
    bool full = span_a >= LM_TAU;
    float ri = r - thick;
    float sx = cx + g->ox, sy = cy + g->oy;
    int x0 = (int)floorf(sx - r), x1 = (int)ceilf(sx + r);
    int y0 = (int)floorf(sy - r), y1 = (int)ceilf(sy + r);
    a0 = a0 - LM_TAU * floorf(a0 / LM_TAU);
    for (int y = y0; y <= y1; y++) {
        float dy = (float)y + 0.5f - sy;
        for (int x = x0; x <= x1; x++) {
            float dx = (float)x + 0.5f - sx;
            float d2 = dx * dx + dy * dy;
            if (d2 > r * r || (ri > 0.f && d2 <= ri * ri)) continue;
            if (!full) {
                float ang = lm_atan2f(dx, -dy);          /* clockwise from 12 */
                if (ang < 0) ang += LM_TAU;
                float rel = ang - a0;
                if (rel < 0) rel += LM_TAU;
                if (rel > span_a) continue;
            }
            plot(g, x, y);
        }
    }
}

/* ---------------------------------------------------------------- lines */
void lm_line(lm_gfx_t *g, float fx0, float fy0, float fx1, float fy1)
{
    int x0 = lm_roundi(fx0) + g->ox, y0 = lm_roundi(fy0) + g->oy;
    int x1 = lm_roundi(fx1) + g->ox, y1 = lm_roundi(fy1) + g->oy;
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        plot(g, x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void lm_capsule(lm_gfx_t *g, float x0, float y0, float x1, float y1, float thick)
{
    if (thick <= 1.05f) { lm_line(g, x0, y0, x1, y1); return; }
    float r = thick * 0.5f;
    x0 += g->ox; x1 += g->ox; y0 += g->oy; y1 += g->oy;
    int bx0 = (int)floorf(lm_minf(x0, x1) - r) - 1, bx1 = (int)ceilf(lm_maxf(x0, x1) + r) + 1;
    int by0 = (int)floorf(lm_minf(y0, y1) - r) - 1, by1 = (int)ceilf(lm_maxf(y0, y1) + r) + 1;
    float vx = x1 - x0, vy = y1 - y0, L2 = vx * vx + vy * vy;
    for (int y = by0; y < by1; y++) {
        float py = (float)y + 0.5f;
        int run = -1;
        for (int x = bx0; x <= bx1; x++) {
            bool in = false;
            if (x < bx1) {
                float px = (float)x + 0.5f;
                float t = L2 < 1e-9f ? 0.f : lm_clamp01(((px - x0) * vx + (py - y0) * vy) / L2);
                float qx = x0 + t * vx - px, qy = y0 + t * vy - py;
                in = qx * qx + qy * qy <= r * r;
            }
            if (in && run < 0) run = x;
            if (!in && run >= 0) { span(g, run, x, y); run = -1; }
        }
    }
}

void lm_poly(lm_gfx_t *g, const float *xy, int n)
{
    if (n < 3) return;
    float miny = 1e9f, maxy = -1e9f;
    for (int i = 0; i < n; i++) {
        miny = lm_minf(miny, xy[2 * i + 1]);
        maxy = lm_maxf(maxy, xy[2 * i + 1]);
    }
    int y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
    float xs[48];
    for (int y = y0; y <= y1; y++) {
        float py = (float)y + 0.5f;
        int k = 0;
        for (int i = 0; i < n && k < 48; i++) {
            float xa = xy[2 * i], ya = xy[2 * i + 1];
            float xb = xy[2 * ((i + 1) % n)], yb = xy[2 * ((i + 1) % n) + 1];
            if (ya == yb || ((ya > py) == (yb > py))) continue;
            xs[k++] = xa + (py - ya) * (xb - xa) / (yb - ya);
        }
        for (int i = 1; i < k; i++) {                /* insertion sort */
            float v = xs[i]; int j = i - 1;
            while (j >= 0 && xs[j] > v) { xs[j + 1] = xs[j]; j--; }
            xs[j + 1] = v;
        }
        for (int i = 0; i + 1 < k; i += 2) {
            /* pixel centre px+0.5 in (xs[i], xs[i+1]] -> even-odd inside */
            int a = (int)ceilf(xs[i] - 0.5f), b = (int)ceilf(xs[i + 1] - 0.5f);
            span(g, a + g->ox, b + g->ox, y + g->oy);
        }
    }
}

void lm_dotted_hline(lm_gfx_t *g, float x, float y, float w, int step)
{
    int xi = lm_roundi(x), yi = lm_roundi(y), wi = lm_roundi(w);
    for (int i = 0; i < wi; i += step) lm_pixel(g, (float)(xi + i), (float)yi);
}

void lm_dotted_vline(lm_gfx_t *g, float x, float y, float h, int step)
{
    int xi = lm_roundi(x), yi = lm_roundi(y), hi = lm_roundi(h);
    for (int i = 0; i < hi; i += step) lm_pixel(g, (float)xi, (float)(yi + i));
}

/* ---------------------------------------------------------------- text */
void lm_font(lm_gfx_t *g, const uint8_t *font)
{
    u8g2_SetFont(g->u8g2, font);
    u8g2_SetFontPosTop(g->u8g2);
}

int lm_text(lm_gfx_t *g, float x, float y, const char *s)
{
    int lx = lm_roundi(x), ly = lm_roundi(y);
    int sx = lx + g->ox, sy = ly + g->oy;
    s_active = g;
    u8g2_SetDrawColor(g->u8g2, 1);
    /* 16-bit u8g2 coordinates wrap; its clipper handles "negative" starts */
    u8g2_uint_t w = u8g2_DrawStr(g->u8g2, (u8g2_uint_t)(int16_t)sx, (u8g2_uint_t)(int16_t)sy, s);
    return lx + (int)w;
}

int lm_text_w(lm_gfx_t *g, const char *s)
{
    int w = 0;
    for (; *s; s++) w += lm_char_w(g, *s);
    return w;
}

int lm_char_w(lm_gfx_t *g, char c) { return u8g2_GetGlyphWidth(g->u8g2, (uint8_t)c); }

int lm_text_center(lm_gfx_t *g, float cx, float y, const char *s)
{
    return lm_text(g, cx - (float)lm_text_w(g, s) * 0.5f, y, s);
}

int lm_text_right(lm_gfx_t *g, float rx, float y, const char *s)
{
    return lm_text(g, rx - (float)lm_text_w(g, s), y, s);
}

int lm_font_h(lm_gfx_t *g) { return u8g2_GetAscent(g->u8g2) - u8g2_GetDescent(g->u8g2); }

/* ---------------------------------------------------------------- buffers */
static uint8_t col_mask(int xmod4, int a16)
{
    uint8_t m = 0;
    for (int r = 0; r < 8; r++)
        if (BAYER[r & 3][xmod4] < a16) m |= (uint8_t)(1u << r);
    return m;
}

static int alpha16(float a)
{
    int v = lm_roundi(a * 16.f);
    return v < 0 ? 0 : v > 16 ? 16 : v;
}

uint8_t lm_dither_col(int xmod4, float a) { return col_mask(xmod4 & 3, alpha16(a)); }

void lm_buf_fade(uint8_t *buf, float keep)
{
    int a = alpha16(keep);
    if (a >= 16) return;
    uint8_t m[4];
    for (int i = 0; i < 4; i++) m[i] = col_mask(i, a);
    for (int p = 0; p < LM_PAGES; p++)
        for (int x = 0; x < LM_W; x++) buf[p * LM_W + x] &= m[x & 3];
}

void lm_buf_xor_all(uint8_t *buf)
{
    for (int i = 0; i < LM_FB_BYTES; i++) buf[i] ^= 0xFF;
}

void lm_buf_blit(uint8_t *dst, const uint8_t *src, int dx, int dy, float alpha)
{
    int a = alpha16(alpha);
    if (a <= 0) return;
    uint8_t m[4];
    for (int i = 0; i < 4; i++) m[i] = a >= 16 ? 0xFF : col_mask(i, a);
    if (dy == 0) {
        /* fast path (horizontal slides): whole bytes, dither anchored to
         * the source so it travels with the content instead of shimmering */
        int x0 = lm_maxi(0, dx), x1 = lm_mini(LM_W, LM_W + dx);
        for (int p = 0; p < LM_PAGES; p++)
            for (int x = x0; x < x1; x++) {
                int sx = x - dx;
                dst[p * LM_W + x] |= src[p * LM_W + sx] & m[sx & 3];
            }
        return;
    }
    for (int y = lm_maxi(0, dy); y < lm_mini(LM_H, LM_H + dy); y++)
        for (int x = lm_maxi(0, dx); x < lm_mini(LM_W, LM_W + dx); x++) {
            int sx = x - dx, sy = y - dy;
            if (lm_buf_get(src, sx, sy) && BAYER[sy & 3][sx & 3] < a) put(dst, x, y, LM_SET);
        }
}

void lm_buf_blit_region(uint8_t *dst, const uint8_t *src,
                        float sx, float sy, float sw, float sh,
                        float dx, float dy, float dw, float dh,
                        float r, float alpha, bool opaque)
{
    int X0 = lm_roundi(dx), Y0 = lm_roundi(dy), DW = lm_roundi(dw), DH = lm_roundi(dh);
    if (DW <= 0 || DH <= 0) return;
    int a = alpha16(alpha);
    if (a <= 0 && !opaque) return;
    int ri = rr_clamp(DW, DH, r);
    float kx = sw / (float)DW, ky = sh / (float)DH;
    for (int py = lm_maxi(0, Y0); py < lm_mini(LM_H, Y0 + DH); py++) {
        int ry = py - Y0;
        int syi = (int)floorf(sy + ((float)ry + 0.5f) * ky);
        int in = rr_inset(DW, DH, ri, ry);
        for (int px = lm_maxi(0, X0 + in); px < lm_mini(LM_W, X0 + DW - in); px++) {
            int sxi = (int)floorf(sx + ((float)(px - X0) + 0.5f) * kx);
            bool on = lm_buf_get(src, sxi, syi) && BAYER[py & 3][px & 3] < a;
            if (opaque) put(dst, px, py, on ? LM_SET : LM_CLEAR);
            else if (on) put(dst, px, py, LM_SET);
        }
    }
}
