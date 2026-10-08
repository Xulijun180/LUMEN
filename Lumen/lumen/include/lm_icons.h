/*
 * Vector app icons: procedural so they scale smoothly in the carousel and
 * animate while focused (`phase` only advances for the active icon).
 */
#ifndef LM_ICONS_H
#define LM_ICONS_H

#include "lm_gfx.h"

typedef void (*lm_icon_fn)(lm_gfx_t *g, float cx, float cy, float size, float phase);

void lm_icon_clock(lm_gfx_t *g, float cx, float cy, float s, float phase);
void lm_icon_music(lm_gfx_t *g, float cx, float cy, float s, float phase);
void lm_icon_monitor(lm_gfx_t *g, float cx, float cy, float s, float phase);
void lm_icon_settings(lm_gfx_t *g, float cx, float cy, float s, float phase);
void lm_icon_about(lm_gfx_t *g, float cx, float cy, float s, float phase);

#endif
