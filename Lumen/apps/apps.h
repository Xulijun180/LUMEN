/*
 * Demo application built on Lumen. Nothing here is platform specific:
 * the same files compile for the simulator and for the MCU.
 */
#ifndef APPS_H
#define APPS_H

#include "lm_ui.h"

/* app-level settings extend the framework ids */
enum { S_SOUND = LM_S_USER, S_WIFI, S_COUNT };
extern const int32_t app_defaults[S_COUNT];

/* page factories (each page is a static singleton) */
lm_page_t *app_boot(void);
lm_page_t *app_home(bool intro);
lm_page_t *app_clock(void);
lm_page_t *app_music(void);
lm_page_t *app_monitor(void);
lm_page_t *app_settings(void);
lm_page_t *app_about(void);

void app_status_bar(lm_ui_t *ui, lm_gfx_t *g, float y);

/* boot splash -> home */
static inline void app_start(lm_ui_t *ui) { lm_ui_start(ui, app_boot()); }

#endif
