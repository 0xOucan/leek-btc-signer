/*
 * Framebuffer drawing. See gfx.h.
 */
#include "gfx.h"

#include <string.h>

static uint8_t fb[GFX_FB_BYTES];
static uint32_t generation;

static inline void put(int x, int y, uint8_t c)
{
    uint8_t *p = &fb[(y * GFX_W + x) >> 1];
    if (x & 1) {
        *p = (uint8_t)((*p & 0x0F) | (c << 4));
    } else {
        *p = (uint8_t)((*p & 0xF0) | c);
    }
}

void gfx_clear(gfx_color_t c)
{
    memset(fb, (int)((c << 4) | c), sizeof fb);
    generation++;
}

void gfx_wipe(void)
{
    /* volatile so the compiler cannot decide a buffer about to be redrawn
     * does not need clearing first. */
    volatile uint8_t *p = fb;
    for (size_t i = 0; i < sizeof fb; i++) {
        p[i] = 0;
    }
    generation++;
}

void gfx_pixel(int x, int y, gfx_color_t c)
{
    if (x < 0 || y < 0 || x >= GFX_W || y >= GFX_H) {
        return;
    }
    put(x, y, (uint8_t)c);
    generation++;
}

gfx_color_t gfx_get(int x, int y)
{
    if (x < 0 || y < 0 || x >= GFX_W || y >= GFX_H) {
        return C_BG;
    }
    uint8_t b = fb[(y * GFX_W + x) >> 1];
    return (gfx_color_t)((x & 1) ? (b >> 4) : (b & 0x0F));
}

void gfx_fill_rect(int x, int y, int w, int h, gfx_color_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > GFX_W) { w = GFX_W - x; }
    if (y + h > GFX_H) { h = GFX_H - y; }
    if (w <= 0 || h <= 0) {
        return;
    }
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            put(xx, yy, (uint8_t)c);
        }
    }
    generation++;
}

void gfx_hline(int x, int y, int w, gfx_color_t c) { gfx_fill_rect(x, y, w, 1, c); }
void gfx_vline(int x, int y, int h, gfx_color_t c) { gfx_fill_rect(x, y, 1, h, c); }

void gfx_rect(int x, int y, int w, int h, gfx_color_t c)
{
    gfx_hline(x, y, w, c);
    gfx_hline(x, y + h - 1, w, c);
    gfx_vline(x, y, h, c);
    gfx_vline(x + w - 1, y, h, c);
}

int gfx_font_height(const gfx_font_t *f)
{
    return f->h * f->scale;
}

int gfx_text_width(const char *s, const gfx_font_t *f)
{
    int n = (int)strlen(s);
    if (n == 0) {
        return 0;
    }
    /* The gap after the last glyph is not part of the text. */
    return (n * f->advance - (f->advance - f->w)) * f->scale;
}

int gfx_text_fit(const char *s, const gfx_font_t *f, int max_w)
{
    int n = 0;
    while (s[n] != '\0') {
        int w = ((n + 1) * f->advance - (f->advance - f->w)) * f->scale;
        if (w > max_w) {
            break;
        }
        n++;
    }
    return n;
}

int gfx_text(int x, int y, const char *s, const gfx_font_t *f, gfx_color_t c)
{
    const int sc = f->scale;
    for (; *s != '\0'; s++) {
        unsigned ch = (unsigned char)*s;
        if (ch < f->first || ch > f->last) {
            ch = '?';
        }
        const uint8_t *g = &f->cols[(ch - f->first) * f->w];
        for (int col = 0; col < f->w; col++) {
            for (int row = 0; row < f->h; row++) {
                if ((g[col] >> row) & 1) {
                    gfx_fill_rect(x + col * sc, y + row * sc, sc, sc, c);
                }
            }
        }
        x += f->advance * sc;
    }
    return x;
}

void gfx_text_centered(int x0, int w, int y, const char *s,
                       const gfx_font_t *f, gfx_color_t c)
{
    gfx_text(x0 + (w - gfx_text_width(s, f)) / 2, y, s, f, c);
}

const uint8_t *gfx_framebuffer(void) { return fb; }
uint32_t gfx_generation(void) { return generation; }
