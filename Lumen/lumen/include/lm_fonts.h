/*
 * Font roles. Widgets refer to roles, not concrete fonts, so a product can
 * restyle (or drop Flash-heavy fonts) in one place.
 */
#ifndef LM_FONTS_H
#define LM_FONTS_H

#include "u8g2.h"

#ifndef LM_FONT_UI
#define LM_FONT_UI     u8g2_font_haxrcorp4089_tr   /* body text, ~7 px caps  */
#endif
#ifndef LM_FONT_TINY
#define LM_FONT_TINY   u8g2_font_4x6_tr            /* status / labels        */
#endif
#ifndef LM_FONT_MID
#define LM_FONT_MID    u8g2_font_logisoso16_tr     /* values, percentages    */
#endif
#ifndef LM_FONT_BIG
#define LM_FONT_BIG    u8g2_font_logisoso28_tn     /* clock digits           */
#endif
#ifndef LM_FONT_LOGO
#define LM_FONT_LOGO   u8g2_font_logisoso22_tr     /* wordmark               */
#endif

#endif
