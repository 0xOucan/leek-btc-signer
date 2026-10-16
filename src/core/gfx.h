/*
 * Drawing into the framebuffer.
 *
 * The framebuffer holds palette indices, 4 bits per pixel, never colors.
 * What an index looks like is the panel's business: the OLEDs light it or
 * not, the TFTs look it up in a SeedSigner-like palette (palette.c). Screens
 * therefore name roles - "this is a warning", "this row is selected" - and
 * the same screen code draws correctly on every profile.
 *
 * The framebuffer can hold rendered seed words, so it is static, in internal
 * RAM, and wiped by gfx_wipe() when a secret screen is left.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "profile.h"

typedef enum {
    C_BG = 0,       /* background */
    C_FG,           /* body text */
    C_MUTED,        /* labels, hints */
    C_ACCENT,       /* active / highlighted */
    C_SEL_BG,       /* selected row background */
    C_SEL_FG,       /* selected row text */
    C_BUTTON_BG,    /* unselected button background */
    C_INACTIVE,     /* disabled */
    C_OK,           /* success */
    C_WARN,         /* warning */
    C_DIRE,         /* dire warning */
    C_ERROR,        /* error */
    C_INFO,         /* informational */
    C_TESTNET,      /* testnet indicator */
    C_REGTEST,      /* regtest indicator */
    C_QR_LIGHT,     /* QR light modules and quiet zone */
    C_COUNT
} gfx_color_t;

/*
 * A bitmap font stored as columns, bit 0 the top row (LeekWallet's 5x7
 * layout). `scale` multiplies every pixel, which gives the color profiles a
 * readable size until the anti-aliased fonts of T2b exist.
 */
typedef struct {
    const uint8_t *cols;    /* `w` bytes per glyph, from `first` to `last` */
    uint8_t first, last;
    uint8_t w, h;           /* glyph cell, unscaled */
    uint8_t advance;        /* unscaled, including the gap */
    uint8_t scale;
} gfx_font_t;

extern const gfx_font_t FONT_5X7;
extern const gfx_font_t FONT_5X7_X2;
extern const gfx_font_t FONT_5X7_X3;

void gfx_clear(gfx_color_t c);
void gfx_pixel(int x, int y, gfx_color_t c);
gfx_color_t gfx_get(int x, int y);
void gfx_fill_rect(int x, int y, int w, int h, gfx_color_t c);
void gfx_rect(int x, int y, int w, int h, gfx_color_t c);
void gfx_hline(int x, int y, int w, gfx_color_t c);
void gfx_vline(int x, int y, int h, gfx_color_t c);

/* Height of one line of text in this font, scaled. */
int gfx_font_height(const gfx_font_t *f);
int gfx_text_width(const char *s, const gfx_font_t *f);
/* Draws transparently; returns the x just past the last glyph. */
int gfx_text(int x, int y, const char *s, const gfx_font_t *f, gfx_color_t c);
/* Centred within [x0, x0 + w). */
void gfx_text_centered(int x0, int w, int y, const char *s,
                       const gfx_font_t *f, gfx_color_t c);
/* Characters of `s` that fit in `max_w` pixels. */
int gfx_text_fit(const char *s, const gfx_font_t *f, int max_w);

/* The raw frame and a counter that moves whenever it is drawn on, so a
 * flush can tell an unchanged frame from a new one without comparing it. */
const uint8_t *gfx_framebuffer(void);
uint32_t gfx_generation(void);

/* Zero the framebuffer, for leaving a screen that showed a secret. */
void gfx_wipe(void);

/* Role to colour, for the panels and the simulator (palette.c). */
uint32_t gfx_role_rgb(gfx_color_t c);   /* 0xRRGGBB */
bool gfx_role_lit(gfx_color_t c);       /* monochrome panels */
