/*
 * Layout metrics per display profile. Screens ask the theme where a title or
 * a list row goes instead of hard-coding pixels, which is what lets one screen
 * definition serve a 128x64 OLED and a 320x240 TFT.
 */
#pragma once

#include <stdint.h>

#include "gfx.h"

typedef enum {
    ROW_INVERT,     /* monochrome: the selected row is drawn inverted */
    ROW_BUTTON,     /* colour: every row is a button, SeedSigner style */
} row_style_t;

typedef struct {
    const gfx_font_t *title;
    const gfx_font_t *body;
    int16_t pad;        /* edge padding */
    int16_t top_h;      /* title bar height */
    int16_t title_y;    /* title text baseline row */
    int16_t row_y0;     /* first list row */
    int16_t row_h;      /* list row pitch */
    int16_t row_box_h;  /* drawn height of a row */
    int16_t line_h;     /* body text line pitch */
    row_style_t row_style;
} theme_t;

extern const theme_t THEME;

/* List rows that fit below the title bar. */
static inline int theme_rows(void)
{
    return (GFX_H - THEME.row_y0 + (THEME.row_h - THEME.row_box_h)) / THEME.row_h;
}
