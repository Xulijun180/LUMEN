#include <stdio.h>

#include "apps.h"
#include "lm_fonts.h"
#include "lm_widgets.h"

const int32_t app_defaults[S_COUNT] = {
    [LM_S_BRIGHTNESS] = 80,
    [LM_S_INVERT]     = 0,
    [LM_S_AUTOSLEEP]  = 30,
    [LM_S_MOTION]     = 100,
    [LM_S_VIBRATE]    = 1,
    [S_SOUND]         = 1,
    [S_WIFI]          = 0,
};

/* time on the left, radios + battery on the right; 7 px tall */
void app_status_bar(lm_ui_t *ui, lm_gfx_t *g, float y)
{
    lm_datetime_t now;
    lm_now(ui, &now);
    char t[8];
    snprintf(t, sizeof(t), "%02d:%02d", now.hour, now.min);
    lm_font(g, LM_FONT_TINY);
    lm_text(g, 2, y + 1, t);
    float x = LM_W - 2 - 13;
    lm_draw_battery(g, x, y + 1, 0.72f);
    x -= 10;
    lm_draw_wifi(g, x, y + 3, lm_setting(ui, S_WIFI) != 0);
    if (!lm_setting(ui, S_SOUND)) {                   /* muted speaker */
        x -= 8;
        lm_box(g, x, y + 2, 2, 3);
        lm_line(g, x + 2, y + 2, x + 4, y);
        lm_line(g, x + 2, y + 4, x + 4, y + 6);
        lm_vline(g, x + 4, y + 1, 5);
        lm_gfx_mode(g, LM_XOR);
        lm_line(g, x + 2, y, x + 6, y + 6);
        lm_gfx_mode(g, LM_SET);
    }
}
