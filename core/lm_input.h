/*
 * Input: raw key mask -> debounced key FSM -> gesture events -> queue.
 *
 * Layout LM_LAYOUT_4KEY (default)
 *   PREV/NEXT  PRESS on key-down (instant), then REPEAT while held (accelerates)
 *   OK/BACK    PRESS on release if short, LONG once held >= LM_KEY_LONG_MS
 *
 * Layout LM_LAYOUT_3KEY (UP / DOWN / ENTER, no BACK key)
 *   UP/DOWN    same as PREV/NEXT
 *   ENTER      resolved on release by how long it was held:
 *                < LM_KEY3_BACK_MS              OK    PRESS
 *                LM_KEY3_BACK_MS..HOME_MS       BACK  PRESS  (one level up)
 *                >= LM_KEY3_HOME_MS (fires while held) BACK LONG (home / sleep)
 *   Resolving on release keeps the stages exclusive: a hold that ends at
 *   "home" never also pops a level on the way. OK LONG does not exist in
 *   this layout; pages must not rely on it.
 *
 * Pages always see the same four logical keys, so app code is layout
 * agnostic; only the gesture mapping differs.
 *
 * The queue is single-producer/single-consumer and lock-free, so a port may
 * push events straight from an encoder ISR with lm_evq_push().
 */
#ifndef LM_INPUT_H
#define LM_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "lm_config.h"

typedef enum { LM_KEY_PREV, LM_KEY_NEXT, LM_KEY_OK, LM_KEY_BACK, LM_KEY_COUNT } lm_key_t;
typedef enum { LM_PRESS, LM_LONG, LM_REPEAT } lm_kind_t;

#define LM_KEYBIT(k) (1u << (k))

typedef struct { uint8_t key, kind; } lm_event_t;

static inline bool lm_ev_is(lm_event_t e, lm_key_t k) { return e.key == k; }
/* PRESS or REPEAT: "move once" semantics for navigation keys */
static inline bool lm_ev_step(lm_event_t e) { return e.kind == LM_PRESS || e.kind == LM_REPEAT; }

typedef struct {
    volatile uint8_t head, tail;
    lm_event_t buf[LM_EVQ_SIZE];
} lm_evq_t;

bool lm_evq_push(lm_evq_t *q, lm_event_t e);   /* false if full (event dropped) */
bool lm_evq_pop(lm_evq_t *q, lm_event_t *e);

typedef enum { LM_LAYOUT_4KEY = 0, LM_LAYOUT_3KEY = 1 } lm_layout_t;

/* 3-key layout: raw bits are UP = LM_KEYBIT(LM_KEY_PREV),
 * DOWN = LM_KEYBIT(LM_KEY_NEXT), ENTER = LM_KEYBIT(LM_KEY_OK). */
#define LM_KEY_UP    LM_KEY_PREV
#define LM_KEY_DOWN  LM_KEY_NEXT
#define LM_KEY_ENTER LM_KEY_OK

typedef struct {
    uint8_t  stable, raw_last;        /* debounced mask, last raw sample   */
    uint32_t raw_since;               /* when raw last changed             */
    uint32_t down_at[LM_KEY_COUNT];
    uint32_t next_rep[LM_KEY_COUNT];
    uint8_t  long_fired;              /* bit per key                       */
    uint8_t  layout;                  /* lm_layout_t                       */
} lm_keys_t;

/* Feed one raw sample; generated events go to `q`. */
void lm_keys_scan(lm_keys_t *k, uint8_t raw, uint32_t now_ms, lm_evq_t *q);
/* 0..1 hold progress of a key that is down, towards its final hold stage
 * (LONG; in the 3-key layout ENTER's "home" stage). For UI hints. */
float lm_keys_hold(const lm_keys_t *k, lm_key_t key, uint32_t now_ms);
/* Progress (0..1) at which an intermediate hold stage begins, or 0 if the
 * key has none (3-key ENTER: where "back" starts). For UI hints. */
float lm_keys_hold_mark(const lm_keys_t *k, lm_key_t key);

#endif
