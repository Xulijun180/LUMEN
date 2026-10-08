#include "lm_input.h"

#include <string.h>

bool lm_evq_push(lm_evq_t *q, lm_event_t e)
{
    uint8_t h = q->head, n = (uint8_t)((h + 1) & (LM_EVQ_SIZE - 1));
    if (n == q->tail)
        return false;
    q->buf[h] = e;
    q->head = n;          /* publish after the payload is written */
    return true;
}

bool lm_evq_pop(lm_evq_t *q, lm_event_t *e)
{
    uint8_t t = q->tail;
    if (t == q->head)
        return false;
    *e = q->buf[t];
    q->tail = (uint8_t)((t + 1) & (LM_EVQ_SIZE - 1));
    return true;
}

static void emit(lm_evq_t *q, int key, lm_kind_t kind)
{
    lm_event_t e = { (uint8_t)key, (uint8_t)kind };
    lm_evq_push(q, e);
}

static bool is_nav(int k) { return k == LM_KEY_PREV || k == LM_KEY_NEXT; }

/* 3-key ENTER: one physical key, three logical gestures by hold length */
static void enter3(lm_keys_t *k, bool was, bool is, uint32_t now, lm_evq_t *q)
{
    const int i = LM_KEY_ENTER;
    const uint8_t bit = (uint8_t)LM_KEYBIT(i);
    if (is && !was) {
        k->down_at[i] = now;
        k->long_fired &= (uint8_t)~bit;
    } else if (is && was) {
        if (!(k->long_fired & bit) && (uint32_t)(now - k->down_at[i]) >= LM_KEY3_HOME_MS) {
            k->long_fired |= bit;
            emit(q, LM_KEY_BACK, LM_LONG);           /* fires while held */
        }
    } else if (!is && was) {
        if (k->long_fired & bit) return;              /* already resolved */
        uint32_t held = now - k->down_at[i];
        emit(q, held >= LM_KEY3_BACK_MS ? LM_KEY_BACK : LM_KEY_OK, LM_PRESS);
    }
}

void lm_keys_scan(lm_keys_t *k, uint8_t raw, uint32_t now, lm_evq_t *q)
{
    if (k->layout == LM_LAYOUT_3KEY)
        raw &= (uint8_t)(LM_KEYBIT(LM_KEY_UP) | LM_KEYBIT(LM_KEY_DOWN) | LM_KEYBIT(LM_KEY_ENTER));
    /* debounce: a raw change must hold for LM_KEY_DEBOUNCE_MS */
    if (raw != k->raw_last) {
        k->raw_last = raw;
        k->raw_since = now;
    }
    uint8_t prev = k->stable;
    if (raw != prev && (uint32_t)(now - k->raw_since) >= LM_KEY_DEBOUNCE_MS)
        k->stable = raw;
    uint8_t cur = k->stable;

    for (int i = 0; i < LM_KEY_COUNT; i++) {
        uint8_t bit = (uint8_t)LM_KEYBIT(i);
        bool was = (prev & bit) != 0, is = (cur & bit) != 0;
        if (k->layout == LM_LAYOUT_3KEY && i == LM_KEY_ENTER) {
            enter3(k, was, is, now, q);
            continue;
        }
        if (is && !was) {                                  /* key down */
            k->down_at[i] = now;
            k->long_fired &= (uint8_t)~bit;
            if (is_nav(i)) {
                emit(q, i, LM_PRESS);
                k->next_rep[i] = now + LM_KEY_REPEAT_MS;
            }
        } else if (is && was) {                            /* held */
            uint32_t held = now - k->down_at[i];
            if (is_nav(i)) {
                if ((int32_t)(now - k->next_rep[i]) >= 0) {
                    emit(q, i, LM_REPEAT);
                    /* accelerate after 1.5 s of holding */
                    uint32_t rate = held > 1500 ? LM_KEY_REPEAT_RATE_MS / 2 : LM_KEY_REPEAT_RATE_MS;
                    k->next_rep[i] = now + rate;
                }
            } else if (!(k->long_fired & bit) && held >= LM_KEY_LONG_MS) {
                k->long_fired |= bit;
                emit(q, i, LM_LONG);
            }
        } else if (!is && was) {                           /* key up */
            if (!is_nav(i) && !(k->long_fired & bit))
                emit(q, i, LM_PRESS);
        }
    }
}

float lm_keys_hold(const lm_keys_t *k, lm_key_t key, uint32_t now)
{
    uint8_t bit = (uint8_t)LM_KEYBIT(key);
    if (!(k->stable & bit) || (k->long_fired & bit))
        return 0.f;
    if (is_nav(key)) return 0.f;
    uint32_t full = (k->layout == LM_LAYOUT_3KEY && key == LM_KEY_ENTER) ? LM_KEY3_HOME_MS : LM_KEY_LONG_MS;
    float p = (float)(now - k->down_at[key]) / (float)full;
    return p > 1.f ? 1.f : p;
}

float lm_keys_hold_mark(const lm_keys_t *k, lm_key_t key)
{
    if (k->layout == LM_LAYOUT_3KEY && key == LM_KEY_ENTER)
        return (float)LM_KEY3_BACK_MS / (float)LM_KEY3_HOME_MS;
    return 0.f;
}
