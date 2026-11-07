/*
 * Stateless draw helpers + small stateful widgets. Widgets own their motion
 * state and expose update()/draw(); they never touch global UI state.
 */
#ifndef LM_WIDGETS_H
#define LM_WIDGETS_H

#include <stdbool.h>
#include <stdint.h>

#include "lm_anim.h"
#include "lm_config.h"
#include "lm_gfx.h"

/* ---- rolling text (odometer) --------------------------------------------
 * Changed characters roll vertically; with several changes the rightmost
 * rolls first and carries ripple left, like a mechanical counter. */
typedef struct {
    char  from, to;
    int8_t dir;
    float t;              /* <0: waiting for its stagger slot */
} lm_roll_cell_t;

typedef struct {
    const uint8_t *font;
    char  text[LM_ROLL_MAX + 1];
    lm_roll_cell_t cell[LM_ROLL_MAX];
    uint8_t n;
    float dur, stagger;
} lm_roll_t;

void lm_roll_init(lm_roll_t *r, const uint8_t *font, const char *text, float dur, float stagger);
void lm_roll_reset(lm_roll_t *r, const char *text);            /* jump, no motion */
void lm_roll_set(lm_roll_t *r, const char *text, int dir);     /* dir: +1 up, -1 down */
void lm_roll_update(lm_roll_t *r, float dt);
int  lm_roll_draw(lm_roll_t *r, lm_gfx_t *g, float x, float y); /* -> end x */
int  lm_roll_width(lm_roll_t *r, lm_gfx_t *g);
bool lm_roll_busy(const lm_roll_t *r);

/* ---- marquee -------------------------------------------------------------- */
typedef struct {
    const char *text;
    const uint8_t *font;
    float width, t, speed, hold, gap;
} lm_marquee_t;

void lm_marquee_init(lm_marquee_t *m, const uint8_t *font, const char *text, float width);
void lm_marquee_set(lm_marquee_t *m, const char *text);
static inline void lm_marquee_update(lm_marquee_t *m, float dt) { m->t += dt; }
void lm_marquee_draw(lm_marquee_t *m, lm_gfx_t *g, float x, float y);

/* ---- draw helpers ------------------------------------------------------ */
/* switch 15x9; p 0..1 (springs may overshoot), vel in p-units/s for stretch */
void lm_draw_toggle(lm_gfx_t *g, float x, float y, float p, float vel);
void lm_draw_spinner(lm_gfx_t *g, float cx, float cy, float r, float t, float thick);
void lm_draw_scrollbar(lm_gfx_t *g, float x, float y, float h, float pos, float visible, float total);
void lm_draw_chevron(lm_gfx_t *g, float x, float cy, bool right, int s);
void lm_draw_battery(lm_gfx_t *g, float x, float y, float level);
void lm_draw_wifi(lm_gfx_t *g, float x, float cy, bool on);

#endif
