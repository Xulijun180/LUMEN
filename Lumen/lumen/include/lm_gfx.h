/*
 * Canvas on top of u8g2.
 *
 * u8g2 owns fonts, the display driver and the transport. Lumen adds what an
 * animated UI needs and u8g2 lacks: float (sub-pixel) geometry, a clip and
 * translation stack, SET/CLEAR/XOR modes, ordered-dither alpha, and buffer
 * compositing for transitions.
 *
 * Integration point: u8g2's ll_hvline callback is replaced, so text and
 * every u8g2 primitive also go through Lumen's mode/dither/target logic.
 * All drawing lands in `target`, which can be the screen buffer or a
 * scratch buffer (pages are rendered off-screen during transitions).
 *
 * Buffers use the SSD1306 page layout: byte = 8 vertical pixels, LSB on top,
 * index = page * 128 + x. Only U8G2_R0 is supported; mount a flipped panel
 * with u8g2_SetFlipMode().
 */
#ifndef LM_GFX_H
#define LM_GFX_H

#include <stdbool.h>
#include <stdint.h>

#include "lm_config.h"
#include "u8g2.h"

typedef enum { LM_CLEAR = 0, LM_SET = 1, LM_XOR = 2 } lm_mode_t;

typedef struct { int16_t x0, y0, x1, y1; } lm_clip_t;   /* x1/y1 exclusive */

#define LM_GFX_CLIP_DEPTH 8
#define LM_GFX_OFF_DEPTH  8

typedef struct lm_gfx {
    u8g2_t  *u8g2;
    uint8_t *target;              /* LM_FB_BYTES buffer being drawn into  */
    lm_mode_t mode;
    uint8_t  alpha;               /* 0..16 ordered-dither coverage         */
    int16_t  ox, oy;              /* translation                           */
    lm_clip_t clip;
    lm_clip_t clip_stack[LM_GFX_CLIP_DEPTH];
    uint8_t   clip_sp;
    int16_t   off_stack[LM_GFX_OFF_DEPTH][2];
    uint8_t   off_sp;
} lm_gfx_t;

/* ---- context ----------------------------------------------------------- */
void lm_gfx_init(lm_gfx_t *g, u8g2_t *u8g2);
void lm_gfx_target(lm_gfx_t *g, uint8_t *buf);           /* also resets clip/offset */
void lm_gfx_clear(lm_gfx_t *g);
static inline void lm_gfx_mode(lm_gfx_t *g, lm_mode_t m) { g->mode = m; }
void lm_gfx_alpha(lm_gfx_t *g, float a);                  /* 1 = opaque   */

void lm_gfx_clip_push(lm_gfx_t *g, float x, float y, float w, float h);
void lm_gfx_clip_pop(lm_gfx_t *g);
void lm_gfx_off_push(lm_gfx_t *g, float dx, float dy);
void lm_gfx_off_pop(lm_gfx_t *g);

/* ---- primitives: local float coordinates ----------------------------- */
void lm_pixel(lm_gfx_t *g, float x, float y);
void lm_hline(lm_gfx_t *g, float x, float y, float w);
void lm_vline(lm_gfx_t *g, float x, float y, float h);
void lm_box(lm_gfx_t *g, float x, float y, float w, float h);
void lm_frame(lm_gfx_t *g, float x, float y, float w, float h);
void lm_rbox(lm_gfx_t *g, float x, float y, float w, float h, float r);
void lm_rframe(lm_gfx_t *g, float x, float y, float w, float h, float r);
void lm_disc(lm_gfx_t *g, float cx, float cy, float r);
void lm_ring(lm_gfx_t *g, float cx, float cy, float r, float thick);
/* a0/a1: radians clockwise from 12 o'clock */
void lm_arc(lm_gfx_t *g, float cx, float cy, float r, float a0, float a1, float thick);
void lm_line(lm_gfx_t *g, float x0, float y0, float x1, float y1);
void lm_capsule(lm_gfx_t *g, float x0, float y0, float x1, float y1, float thick);
void lm_poly(lm_gfx_t *g, const float *xy, int n);       /* even-odd fill */
void lm_dotted_hline(lm_gfx_t *g, float x, float y, float w, int step);
void lm_dotted_vline(lm_gfx_t *g, float x, float y, float h, int step);

/* ---- text (u8g2 fonts, top-aligned) ------------------------------------ */
void lm_font(lm_gfx_t *g, const uint8_t *font);
int  lm_text(lm_gfx_t *g, float x, float y, const char *s);        /* -> end x */
int  lm_text_center(lm_gfx_t *g, float cx, float y, const char *s);
int  lm_text_right(lm_gfx_t *g, float rx, float y, const char *s);
int  lm_text_w(lm_gfx_t *g, const char *s);
int  lm_char_w(lm_gfx_t *g, char c);
int  lm_font_h(lm_gfx_t *g);                                       /* cap height */

/* ---- whole-buffer operations (screen coords, ignore clip/offset) ------ */
void lm_buf_fade(uint8_t *buf, float keep);   /* keep `keep` of lit pixels */
/* page-format column mask of the ordered dither at coverage a (0..1) for
 * a column with x % 4 == xmod4: AND a page byte with it to dither it */
uint8_t lm_dither_col(int xmod4, float a);
void lm_buf_xor_all(uint8_t *buf);
bool lm_buf_get(const uint8_t *buf, int x, int y);
void lm_buf_put(uint8_t *buf, int x, int y, bool on);
/* OR `src` shifted by (dx, dy) into dst; `alpha` dithers it (1 = opaque) */
void lm_buf_blit(uint8_t *dst, const uint8_t *src, int dx, int dy, float alpha);
/* Nearest-neighbour scale the source rect (sx,sy,sw,sh) of `src` into the
 * destination rect (dx,dy,dw,dh), clipped to a rounded rect of radius r.
 * opaque: destination pixels inside the rect are replaced (lit where the
 * dithered source is lit, cleared elsewhere); otherwise lit pixels are ORed. */
void lm_buf_blit_region(uint8_t *dst, const uint8_t *src,
                        float sx, float sy, float sw, float sh,
                        float dx, float dy, float dw, float dh,
                        float r, float alpha, bool opaque);
/* whole-screen convenience */
static inline void lm_buf_blit_scaled(uint8_t *dst, const uint8_t *src,
                                      float x, float y, float w, float h, float alpha)
{
    lm_buf_blit_region(dst, src, 0, 0, LM_W, LM_H, x, y, w, h, 0, alpha, false);
}

static inline uint8_t *lm_gfx_screen(lm_gfx_t *g) { return u8g2_GetBufferPtr(g->u8g2); }

#endif
