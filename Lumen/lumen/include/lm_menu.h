/*
 * Declarative list menu (data-driven: the item table is const, in flash).
 *
 *   static const lm_menu_item_t items[] = {
 *       LM_ITEM_TOGGLE("Sound", S_SOUND),
 *       LM_ITEM_SHEET ("Brightness", open_brightness, fmt_percent),
 *       LM_ITEM_SUBMENU("Display", &display_menu.base),
 *   };
 *
 * The page renders a jelly cursor (snappy leading edge, trailing edge on a
 * "follow" system), spring scroll with wrap-around, staggered row entrance
 * and inline widgets bound to settings ids.
 */
#ifndef LM_MENU_H
#define LM_MENU_H

#include "lm_ui.h"
#include "lm_widgets.h"

typedef enum {
    LM_MI_ACTION,     /* callback                                     */
    LM_MI_TOGGLE,     /* bool setting; optional async busy phase      */
    LM_MI_OPTION,     /* cycles through a value/label list            */
    LM_MI_SHEET,      /* callback opens an editor, shows fmt() value  */
    LM_MI_SUBMENU     /* pushes a page                                */
} lm_mi_kind_t;

typedef struct { int32_t value; const char *label; } lm_option_t;

typedef struct lm_menu lm_menu_t;
typedef void (*lm_menu_action)(lm_menu_t *m, int index);
typedef void (*lm_menu_fmt)(lm_ui_t *ui, char *out, int n);
typedef void (*lm_menu_done)(lm_ui_t *ui, int32_t value);

typedef struct {
    const char *label;
    lm_mi_kind_t kind;
    int  setting;                    /* TOGGLE / OPTION                  */
    const lm_option_t *options;      /* OPTION                           */
    uint8_t noptions;
    lm_menu_action action;           /* ACTION / SHEET                   */
    lm_menu_fmt fmt;                 /* SHEET value text                 */
    lm_page_t *page;                 /* SUBMENU                          */
    uint16_t busy_ms;                /* TOGGLE: async phase before ON    */
    lm_menu_done done;               /* TOGGLE: after value committed    */
} lm_menu_item_t;

#define LM_ITEM_ACTION(L, fn)             { (L), LM_MI_ACTION, -1, 0, 0, (fn), 0, 0, 0, 0 }
#define LM_ITEM_TOGGLE(L, id)             { (L), LM_MI_TOGGLE, (id), 0, 0, 0, 0, 0, 0, 0 }
#define LM_ITEM_TOGGLE_ASYNC(L, id, ms, d){ (L), LM_MI_TOGGLE, (id), 0, 0, 0, 0, 0, (ms), (d) }
#define LM_ITEM_OPTION(L, id, opts)       { (L), LM_MI_OPTION, (id), (opts), \
                                            (uint8_t)(sizeof(opts) / sizeof((opts)[0])), 0, 0, 0, 0, 0 }
#define LM_ITEM_SHEET(L, fn, f)           { (L), LM_MI_SHEET, -1, 0, 0, (fn), (f), 0, 0, 0 }
#define LM_ITEM_SUBMENU(L, pg)            { (L), LM_MI_SUBMENU, -1, 0, 0, 0, 0, (pg), 0, 0 }

/* per-row runtime state (RAM) */
typedef struct {
    lm_spring_t sw;      /* toggle knob                     */
    lm_roll_t   val;     /* value text                      */
    float busy;          /* >0: async phase remaining (s)   */
} lm_menu_row_t;

struct lm_menu {
    lm_page_t base;
    const lm_menu_item_t *items;
    uint8_t n;
    uint8_t sel, top;
    lm_spring_t cur, trail, cur_w, scroll, press;
    lm_roll_t counter;
    lm_menu_row_t row[LM_MENU_MAX_ITEMS];
};

extern const lm_page_ops_t lm_menu_ops;

#define LM_MENU_DEFINE(name, title_, items_) \
    lm_menu_t name = { { &lm_menu_ops, (title_), 0, 0 }, (items_), \
                       (uint8_t)(sizeof(items_) / sizeof((items_)[0])) }

/* focus rect of the selected row (dialogs pop out of it) */
void lm_menu_focus(lm_menu_t *m, float r[4]);

#endif
