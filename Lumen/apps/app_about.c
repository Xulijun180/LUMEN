/* About: spring-scrolled sheet with rubber-band ends and typewriter rows. */
#include <stdio.h>
#include <string.h>

#include "apps.h"
#include "lm_fonts.h"
#include "lm_math.h"
#include "lm_widgets.h"

#define LOGO_H 23.f                  /* logisoso22 cap height */
#define TAG_Y  (3.f + LOGO_H + 4.f)
#define ROW0   (TAG_Y + 11.f)
#define ROW_H 11.f
#define STEP 12.f
#define NROWS 7

typedef struct {
    lm_page_t base;
    lm_spring_t scroll;
    float target;
} about_t;

static void row(lm_ui_t *ui, int i, const char **k, char *v, int n)
{
    static const char *keys[NROWS] = { "Display", "Buffer", "Refresh", "Motion", "Uptime", "Stack", "Core RAM" };
    *k = keys[i];
    uint32_t up = ui->ms / 1000;
    switch (i) {
    case 0: snprintf(v, (size_t)n, "SSD1306"); break;
    case 1: snprintf(v, (size_t)n, "1-bit 1KB"); break;
    case 2: snprintf(v, (size_t)n, "%d FPS", lm_roundi(ui->fps)); break;
    case 3: snprintf(v, (size_t)n, "2nd-order"); break;
    case 4: snprintf(v, (size_t)n, "%02u:%02u", (unsigned)(up / 60 % 100), (unsigned)(up % 60)); break;
    case 5: snprintf(v, (size_t)n, "%d pages", ui->depth); break;
    default: snprintf(v, (size_t)n, "%u B", (unsigned)sizeof(lm_ui_t)); break;
    }
}

static float max_scroll(void) { return ROW0 + NROWS * ROW_H + 2 - LM_H; }

static void enter(lm_page_t *p)
{
    about_t *a = (about_t *)p;
    lm_spring_init(&a->scroll, LM_MOTION_SMOOTH, 0, 1);
    a->target = 0;
}

static bool key(lm_page_t *p, lm_event_t e)
{
    about_t *a = (about_t *)p;
    if ((e.key == LM_KEY_PREV || e.key == LM_KEY_NEXT) && lm_ev_step(e)) {
        float d = e.key == LM_KEY_PREV ? -1.f : 1.f, n = a->target + d * STEP;
        if (n < 0 || n > max_scroll()) {
            n = lm_clampf(n, 0, max_scroll());
            lm_spring_kick(&a->scroll, d * 110.f);         /* rubber band */
            lm_haptic(p->ui);
        }
        a->target = n;
        lm_spring_set(&a->scroll, n);
        return true;
    }
    return false;
}

static void update(lm_page_t *p, float dt) { lm_spring_update(&((about_t *)p)->scroll, dt); }

static void render(lm_page_t *p, lm_gfx_t *g)
{
    about_t *a = (about_t *)p;
    float age = p->age;
    lm_gfx_off_push(g, 0, -a->scroll.value);
    lm_font(g, LM_FONT_LOGO);
    /* caps-only wordmark: its visual height is the ascent, not ascent+descent */
    float lw = (float)lm_text_w(g, "LUMEN"), lx = LM_W / 2.f - lw / 2, fh = LOGO_H;
    float ly = 3 - 20 * (1 - lm_curve(LM_CURVE_OVERSHOOT, (age - 0.1f) / 0.5f));
    lm_text(g, lx, ly, "LUMEN");
    float ph = age - 0.8f;
    if (ph > 0) {
        ph -= 3.2f * floorf(ph / 3.2f);
        if (ph < 0.6f) {                               /* periodic shine */
            float cx = lm_lerpf(lx - 14, lx + lw + 14, lm_curve(LM_CURVE_INOUT, ph / 0.6f));
            lm_gfx_clip_push(g, 0, ly, LM_W, fh);
            lm_gfx_mode(g, LM_CLEAR);
            for (int k = 0; k < 4; k++) lm_line(g, cx + k - 7, ly + fh, cx + k + 7, ly);
            lm_gfx_mode(g, LM_SET);
            lm_gfx_clip_pop(g);
        }
    }
    lm_font(g, LM_FONT_TINY);
    lm_text_center(g, LM_W / 2.f, TAG_Y, "V1.0  C / U8G2 PORT");
    for (int i = 0; i < NROWS; i++) {
        const char *k;
        char v[16];
        row(p->ui, i, &k, v, sizeof(v));
        float y = ROW0 + (float)i * ROW_H;
        float t = lm_clamp01((age - 0.35f - (float)i * 0.08f) / 0.35f);
        size_t n = (size_t)((float)strlen(v) * t);
        v[n] = 0;
        lm_font(g, LM_FONT_UI);
        lm_text(g, 4, y, k);
        lm_text_right(g, LM_W - 7, y, v);
        if (t < 1) lm_box(g, LM_W - 6, y, 3, 7);
        lm_dotted_hline(g, 4, y + 9, LM_W - 11, 3);
    }
    lm_gfx_off_pop(g);
    lm_draw_scrollbar(g, LM_W - 3, 2, LM_H - 4, a->scroll.value, LM_H, LM_H + max_scroll());
}

static const lm_spring_t *probe(lm_page_t *p, const char **label)
{
    *label = "scroll  smooth";
    return &((about_t *)p)->scroll;
}

static const lm_page_ops_t OPS = { enter, 0, 0, key, update, render, 0, probe };
static about_t s_about = { { &OPS, "About", 0, 0 } };

lm_page_t *app_about(void) { return &s_about.base; }
