/*
 * Layout metrics, one set per display profile. See theme.h.
 */
#include "theme.h"

#if defined(LBS_DISPLAY_OLED128)
/* 9 rows of 12 px under a 12 px title. */
const theme_t THEME = {
    .title = &FONT_5X7, .body = &FONT_5X7,
    .pad = 2, .top_h = 12, .title_y = 2,
    .row_y0 = 14, .row_h = 12, .row_box_h = 11, .line_h = 10,
    .row_style = ROW_INVERT,
};
#elif defined(LBS_DISPLAY_OLED64)
/* 5 rows of 9 px under a 10 px title: LeekWallet's density. */
const theme_t THEME = {
    .title = &FONT_5X7, .body = &FONT_5X7,
    .pad = 2, .top_h = 10, .title_y = 1,
    .row_y0 = 11, .row_h = 9, .row_box_h = 9, .line_h = 9,
    .row_style = ROW_INVERT,
};
#elif defined(LBS_DISPLAY_TFT240) || defined(LBS_DISPLAY_TFT320)
/* SeedSigner's own numbers: 48 px top nav, 32 px buttons, 8 px padding. */
const theme_t THEME = {
    .title = &FONT_5X7_X2, .body = &FONT_5X7_X2,
    .pad = 8, .top_h = 48, .title_y = 17,
    .row_y0 = 52, .row_h = 40, .row_box_h = 32, .line_h = 20,
    .row_style = ROW_BUTTON,
};
#else /* TFT160 */
const theme_t THEME = {
    .title = &FONT_5X7, .body = &FONT_5X7,
    .pad = 6, .top_h = 24, .title_y = 8,
    .row_y0 = 26, .row_h = 22, .row_box_h = 18, .line_h = 11,
    .row_style = ROW_BUTTON,
};
#endif
