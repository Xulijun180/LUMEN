/* Settings tree: two declarative menus + the reset dialog and brightness sheet. */
#include <stdio.h>

#include "apps.h"
#include "lm_menu.h"
#include "lm_overlay.h"

/* ---- brightness sheet ------------------------------------------------ */
static void on_brightness(lm_ui_t *ui, int32_t v, void *ctx) { (void)ctx; lm_setting_set(ui, LM_S_BRIGHTNESS, v); }

static void open_brightness(lm_menu_t *m, int i)
{
    (void)i;
    lm_sheet_desc_t d = { "Brightness", lm_setting(m->base.ui, LM_S_BRIGHTNESS), 10, 100, 10, on_brightness, 0 };
    lm_sheet(m->base.ui, &d);
}

static void fmt_brightness(lm_ui_t *ui, char *out, int n)
{
    snprintf(out, (size_t)n, "%d%%", (int)lm_setting(ui, LM_S_BRIGHTNESS));
}

/* ---- reset dialog ------------------------------------------------------ */
static void reset_done(lm_ui_t *ui, int choice, void *ctx)
{
    (void)ctx;
    if (choice != 1) return;
    for (int k = 0; k < S_COUNT; k++) lm_setting_set(ui, k, app_defaults[k]);   /* observers animate */
    lm_toast(ui, "Settings reset", LM_ICON_CHECK);
}

static void open_reset(lm_menu_t *m, int i)
{
    (void)i;
    float r[4];
    lm_menu_focus(m, r);
    lm_dialog_desc_t d = { "Reset settings", "Restore defaults?", { "Cancel", "Reset" }, 0, 1, 1.1f,
                           "Resetting", r[0] + r[2] / 2, r[1] + r[3] / 2, reset_done, 0 };
    lm_dialog(m->base.ui, &d);
}

static void wifi_done(lm_ui_t *ui, int32_t on)
{
    if (on) lm_toast(ui, "Connected", LM_ICON_WIFI);
}

/* ---- tables (const: live in flash) ---------------------------------- */
static const lm_option_t SLEEP_OPTS[] = { {15, "15s"}, {30, "30s"}, {60, "60s"}, {0, "Never"} };
static const lm_option_t MOTION_OPTS[] = { {100, "Normal"}, {50, "Slow"}, {150, "Fast"} };

static const lm_menu_item_t DISPLAY_ITEMS[] = {
    LM_ITEM_TOGGLE("Invert", LM_S_INVERT),
    LM_ITEM_OPTION("Auto sleep", LM_S_AUTOSLEEP, SLEEP_OPTS),
    LM_ITEM_OPTION("Motion", LM_S_MOTION, MOTION_OPTS),
};
static LM_MENU_DEFINE(s_display, "Display", DISPLAY_ITEMS);

static const lm_menu_item_t SETTINGS_ITEMS[] = {
    LM_ITEM_SHEET("Brightness", open_brightness, fmt_brightness),
    LM_ITEM_TOGGLE("Sound", S_SOUND),
    LM_ITEM_TOGGLE("Vibration", LM_S_VIBRATE),
    LM_ITEM_TOGGLE_ASYNC("Wi-Fi", S_WIFI, 1100, wifi_done),
    LM_ITEM_SUBMENU("Display", &s_display.base),
    LM_ITEM_ACTION("Reset", open_reset),
};
static LM_MENU_DEFINE(s_settings, "Settings", SETTINGS_ITEMS);

lm_page_t *app_settings(void) { return &s_settings.base; }
