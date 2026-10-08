/*
 * Built-in overlays. Each is a singleton (static storage inside the
 * module); showing one while it is already up re-initialises it in place.
 *
 *  toast   non-modal, drops in with a bounce, auto-dismisses
 *  dialog  modal confirm: pops out of the element that opened it, spring
 *          cursor between buttons, optional busy phase with spinner
 *  sheet   modal bottom sheet editing an integer value with live callback
 */
#ifndef LM_OVERLAY_H
#define LM_OVERLAY_H

#include "lm_ui.h"

typedef enum { LM_ICON_CHECK, LM_ICON_WIFI, LM_ICON_INFO } lm_icon_t;

void lm_toast(lm_ui_t *ui, const char *text, lm_icon_t icon);

typedef void (*lm_dialog_cb)(lm_ui_t *ui, int choice, void *ctx);  /* -1 = dismissed */

typedef struct {
    const char *title, *message;
    const char *buttons[2];
    int  default_btn;
    int  busy_btn;              /* button that shows a busy phase first, -1 none */
    float busy_s;
    const char *busy_text;
    float origin_x, origin_y;   /* where it pops out from                */
    lm_dialog_cb cb;
    void *ctx;
} lm_dialog_desc_t;

void lm_dialog(lm_ui_t *ui, const lm_dialog_desc_t *d);

typedef void (*lm_sheet_cb)(lm_ui_t *ui, int32_t value, void *ctx);

typedef struct {
    const char *title;
    int32_t value, lo, hi, step;
    lm_sheet_cb on_change;
    void *ctx;
} lm_sheet_desc_t;

void lm_sheet(lm_ui_t *ui, const lm_sheet_desc_t *d);

#endif
