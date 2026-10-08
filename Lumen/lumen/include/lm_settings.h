/*
 * Settings store with observers (Observer pattern).
 *
 * The store owns values; views read them, and anything that must react to a
 * change (panel contrast, invert wipe, persistence, an app's own state)
 * subscribes instead of being called from the widget that changed it.
 * Framework ids come first; apps extend with ids from LM_S_USER upwards.
 */
#ifndef LM_SETTINGS_H
#define LM_SETTINGS_H

#include <stdint.h>

#include "lm_config.h"

enum {
    LM_S_BRIGHTNESS,   /* 10..100 %                                */
    LM_S_INVERT,       /* 0/1                                      */
    LM_S_AUTOSLEEP,    /* seconds, 0 = never                       */
    LM_S_MOTION,       /* animation speed, % (100 = normal)        */
    LM_S_VIBRATE,      /* 0/1                                      */
    LM_S_USER          /* first app-defined id                     */
};

typedef void (*lm_setting_cb)(void *ctx, int id, int32_t old_v, int32_t new_v);

typedef struct {
    int32_t v[LM_MAX_SETTINGS];
    struct { lm_setting_cb cb; void *ctx; } sub[LM_MAX_SUBSCRIBERS];
    uint8_t nsub;
    uint8_t dirty;           /* set on change; the UI flushes to storage */
} lm_settings_t;

void    lm_settings_init(lm_settings_t *s, const int32_t *defaults, int n);
int32_t lm_get(const lm_settings_t *s, int id);
void    lm_set(lm_settings_t *s, int id, int32_t v);          /* notifies on change */
int     lm_subscribe(lm_settings_t *s, lm_setting_cb cb, void *ctx);

#endif
