/*
 * Lumen UI core: page stack, overlays, transitions, power, display output.
 *
 * Architecture
 *   - Hardware is injected through lm_port_t (dependency inversion): the core
 *     never includes a platform header. A port supplies a u8g2 instance that
 *     is already set up, plus time / keys / optional haptics, RTC, storage.
 *   - Pages and overlays are objects with a const ops table (a vtable in
 *     flash). Concrete types embed lm_page_t / lm_overlay_t as the first
 *     member, so they are upcast by address. Every op is optional.
 *   - Instances are static; nothing here allocates.
 *   - Key events flow down a chain of responsibility:
 *        power/dim wake -> transition (queue or reverse) -> top modal overlay
 *        -> global gestures (hold BACK) -> top page -> default BACK = pop.
 *   - Transitions are strategies (slide/zoom/fade), each driven either by a
 *     bezier curve or a second-order system, reversible mid-flight.
 */
#ifndef LM_UI_H
#define LM_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "lm_anim.h"
#include "lm_config.h"
#include "lm_gfx.h"
#include "lm_input.h"
#include "lm_settings.h"

typedef struct lm_ui lm_ui_t;

/* ======================================================================= port */
typedef struct {
    uint16_t year;
    uint8_t  month, day, wday;      /* wday: 0 = Monday                     */
    uint8_t  hour, min, sec;
    uint16_t ms;
} lm_datetime_t;

typedef struct {
    void *ctx;
    u8g2_t *u8g2;                                   /* set up by the port   */
    uint32_t (*millis)(void *ctx);                  /* monotonic ms         */
    uint8_t  (*read_keys)(void *ctx);               /* LM_KEYBIT mask, NULL if the port pushes events */
    void     (*haptic)(void *ctx, uint16_t ms);     /* optional             */
    bool     (*rtc_get)(void *ctx, lm_datetime_t *dt);   /* optional        */
    bool     (*settings_load)(void *ctx, int32_t *v, int n);     /* optional */
    void     (*settings_save)(void *ctx, const int32_t *v, int n); /* optional */
    uint8_t  key_layout;                            /* lm_layout_t, 0 = 4 keys */
} lm_port_t;

/* ======================================================================= page */
typedef struct lm_page lm_page_t;

typedef struct {
    void (*enter)(lm_page_t *p);                     /* pushed: reset + start intro */
    void (*resume)(lm_page_t *p);                    /* page above was popped       */
    void (*leave)(lm_page_t *p);                     /* removed from the stack      */
    bool (*key)(lm_page_t *p, lm_event_t e);         /* true = consumed             */
    void (*update)(lm_page_t *p, float dt);
    void (*render)(lm_page_t *p, lm_gfx_t *g);
    /* rect a zoom transition grows from / shrinks back into */
    void (*focus)(lm_page_t *p, float r[4]);
    /* optional: expose the dominant animation for tooling */
    const lm_spring_t *(*probe)(lm_page_t *p, const char **label);
} lm_page_ops_t;

struct lm_page {
    const lm_page_ops_t *ops;
    const char *title;
    lm_ui_t *ui;
    float age;                                       /* seconds since enter        */
};

/* ======================================================================= overlay */
typedef struct lm_overlay lm_overlay_t;

typedef struct {
    void (*key)(lm_overlay_t *o, lm_event_t e);
    void (*update)(lm_overlay_t *o, float dt);
    void (*render)(lm_overlay_t *o, lm_gfx_t *g);
    void (*close)(lm_overlay_t *o);
    const lm_spring_t *(*probe)(lm_overlay_t *o, const char **label);
} lm_overlay_ops_t;

struct lm_overlay {
    const lm_overlay_ops_t *ops;
    lm_ui_t *ui;
    bool modal;         /* owns input while open                        */
    bool closing;       /* animating out                                */
    bool dead;          /* remove at end of frame                       */
    float age;
};

/* ======================================================================= transition */
typedef enum { LM_TR_NONE, LM_TR_SLIDE, LM_TR_ZOOM, LM_TR_FADE } lm_tr_kind_t;

typedef struct {
    bool use_spring;
    lm_spring_t s;       /* second-order driver   */
    lm_tween_t  tw;      /* bezier driver         */
} lm_drive_t;

typedef struct {
    bool active, forward, reversed;
    lm_tr_kind_t kind;
    lm_page_t *old_p, *new_p;
    float rect[4];
    lm_drive_t d;
} lm_transition_t;

/* ======================================================================= power */
typedef enum { LM_PWR_ON, LM_PWR_TURNING_OFF, LM_PWR_OFF, LM_PWR_TURNING_ON } lm_power_t;

/* ======================================================================= ui */
struct lm_ui {
    lm_port_t port;
    lm_gfx_t  gfx;
    lm_settings_t settings;

    /* page stack: page + how it was entered (popping reverses that) */
    struct { lm_page_t *page; lm_tr_kind_t how; } stack[LM_MAX_STACK];
    uint8_t depth;
    lm_overlay_t *ov[LM_MAX_OVERLAYS];
    uint8_t nov;
    lm_transition_t tr;

    lm_evq_t  evq;          /* from keys FSM or port ISR          */
    lm_evq_t  deferred;     /* keys that arrived mid-transition   */
    lm_keys_t keys;

    /* time */
    float    time;          /* s since start, wraps hourly (phases only) */
    uint32_t ms;            /* ms since start (exact, for timers/keys)    */
    uint32_t last_ms;
    bool     started;
    uint32_t frames;
    float    fps;
    uint16_t still_frames;  /* consecutive frames with no pixel change    */
    bool     force_full;    /* resend every page next frame               */

    /* display state */
    lm_power_t power;
    lm_tween_t power_tw;
    float    idle;
    bool     dimmed;
    lm_spring_t bright, dim;
    lm_tween_t  inv_tw;
    bool     inv_active, inv_from;
    uint8_t  contrast_sent;
    uint32_t page_hash[LM_PAGES];
    float    save_timer;
    float    haptic_t;      /* ui->time of last haptic pulse      */
    lm_datetime_t clock_base;   /* used when the port has no RTC  */

    /* scratch buffers for transitions / scaled overlays */
    uint8_t buf_a[LM_FB_BYTES];
    uint8_t buf_b[LM_FB_BYTES];
};

/* ---- lifecycle ------------------------------------------------------ */
void lm_ui_init(lm_ui_t *ui, const lm_port_t *port, const int32_t *defaults, int ndefaults);
void lm_ui_start(lm_ui_t *ui, lm_page_t *root);           /* push root + wake */
void lm_ui_tick(lm_ui_t *ui);                             /* dt from port->millis */
void lm_ui_step(lm_ui_t *ui, float dt);                   /* explicit dt (tests/sim) */
bool lm_ui_idle(const lm_ui_t *ui);                       /* nothing moving: port may sleep */
void lm_ui_post(lm_ui_t *ui, lm_key_t key, lm_kind_t kind);

/* ---- navigation ------------------------------------------------------ */
void lm_push(lm_ui_t *ui, lm_page_t *p, lm_tr_kind_t how);
void lm_replace(lm_ui_t *ui, lm_page_t *p, lm_tr_kind_t how);
void lm_pop(lm_ui_t *ui);
void lm_home(lm_ui_t *ui);
static inline lm_page_t *lm_top(lm_ui_t *ui) { return ui->depth ? ui->stack[ui->depth - 1].page : 0; }

/* ---- overlays --------------------------------------------------------- */
lm_overlay_t *lm_show(lm_ui_t *ui, lm_overlay_t *o);
bool lm_modal_open(const lm_ui_t *ui);

/* ---- services ---------------------------------------------------------- */
void lm_haptic(lm_ui_t *ui);
void lm_sleep(lm_ui_t *ui);
void lm_wake(lm_ui_t *ui);
void lm_now(const lm_ui_t *ui, lm_datetime_t *dt);
static inline int32_t lm_setting(const lm_ui_t *ui, int id) { return lm_get(&ui->settings, id); }
static inline void lm_setting_set(lm_ui_t *ui, int id, int32_t v) { lm_set(&ui->settings, id, v); }
/* key currently held (0..1 progress to LONG), for hold hints */
float lm_hold_progress(const lm_ui_t *ui, lm_key_t k);
/* Dominant animation right now (transition > modal overlay > page), for
 * tooling such as the simulator's motion scope. Returns an identity token
 * (changes when the probed animation changes) or NULL. */
const void *lm_probe(lm_ui_t *ui, const char **label, float *value, float *target);

/* the page's view of the UI */
static inline lm_ui_t *lm_ui(const lm_page_t *p) { return p->ui; }

#endif
