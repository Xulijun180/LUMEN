/*
 * Lumen UI - compile-time configuration.
 *
 * Every limit is a macro so a port can shrink or grow RAM usage without
 * touching the framework. Override by defining LM_USER_CONFIG and providing
 * "lm_user_config.h" on the include path.
 */
#ifndef LM_CONFIG_H
#define LM_CONFIG_H

#ifdef LM_USER_CONFIG
#include "lm_user_config.h"
#endif

/* ---- panel ----------------------------------------------------------- */
#define LM_W        128
#define LM_H        64
#define LM_PAGES    (LM_H / 8)
#define LM_FB_BYTES (LM_W * LM_PAGES)          /* 1024: SSD1306 page format */

/* ---- containers (all static, no malloc) ------------------------------ */
#ifndef LM_MAX_STACK
#define LM_MAX_STACK      6      /* page stack depth                       */
#endif
#ifndef LM_MAX_OVERLAYS
#define LM_MAX_OVERLAYS   4      /* toast + dialog/sheet + headroom        */
#endif
#ifndef LM_EVQ_SIZE
#define LM_EVQ_SIZE       16     /* key events, power of two               */
#endif
#ifndef LM_MAX_SUBSCRIBERS
#define LM_MAX_SUBSCRIBERS 8     /* settings observers                     */
#endif
#ifndef LM_MAX_SETTINGS
#define LM_MAX_SETTINGS   12
#endif
#ifndef LM_MENU_MAX_ITEMS
#define LM_MENU_MAX_ITEMS 10
#endif
#ifndef LM_ROLL_MAX
#define LM_ROLL_MAX       8      /* chars per rolling-text widget          */
#endif

/* ---- input timing (ms) ----------------------------------------------- */
#ifndef LM_KEY_DEBOUNCE_MS
#define LM_KEY_DEBOUNCE_MS 12
#endif
#ifndef LM_KEY_LONG_MS
#define LM_KEY_LONG_MS     600
#endif
#ifndef LM_KEY_REPEAT_MS
#define LM_KEY_REPEAT_MS   420   /* first auto-repeat after this hold      */
#endif
#ifndef LM_KEY_REPEAT_RATE_MS
#define LM_KEY_REPEAT_RATE_MS 110
#endif
/* 3-key layout: ENTER hold stages (see lm_input.h) */
#ifndef LM_KEY3_BACK_MS
#define LM_KEY3_BACK_MS    400   /* release after this: BACK              */
#endif
#ifndef LM_KEY3_HOME_MS
#define LM_KEY3_HOME_MS    1200  /* reach this: BACK LONG (home / sleep)  */
#endif

/* ---- motion ------------------------------------------------------------ */
#ifndef LM_SPRING_HZ
#define LM_SPRING_HZ 240         /* integrator sub-step rate               */
#endif
#ifndef LM_MAX_DT
#define LM_MAX_DT    0.05f       /* clamp frame dt (s): survive stalls     */
#endif

#endif /* LM_CONFIG_H */
