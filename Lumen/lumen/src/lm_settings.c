#include "lm_settings.h"

#include <string.h>

void lm_settings_init(lm_settings_t *s, const int32_t *defaults, int n)
{
    memset(s, 0, sizeof(*s));
    if (n > LM_MAX_SETTINGS) n = LM_MAX_SETTINGS;
    for (int i = 0; i < n; i++) s->v[i] = defaults[i];
}

int32_t lm_get(const lm_settings_t *s, int id)
{
    return (id >= 0 && id < LM_MAX_SETTINGS) ? s->v[id] : 0;
}

void lm_set(lm_settings_t *s, int id, int32_t v)
{
    if (id < 0 || id >= LM_MAX_SETTINGS || s->v[id] == v)
        return;
    int32_t old = s->v[id];
    s->v[id] = v;
    s->dirty = 1;
    for (int i = 0; i < s->nsub; i++)
        s->sub[i].cb(s->sub[i].ctx, id, old, v);
}

int lm_subscribe(lm_settings_t *s, lm_setting_cb cb, void *ctx)
{
    if (s->nsub >= LM_MAX_SUBSCRIBERS)
        return -1;
    s->sub[s->nsub].cb = cb;
    s->sub[s->nsub].ctx = ctx;
    return s->nsub++;
}
