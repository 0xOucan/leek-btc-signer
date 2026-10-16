/* gfx.c: nibble packing, clipping, text metrics, wipe. */
#include <string.h>

#include "check.h"
#include "gfx.h"

int main(void)
{
    gfx_clear(C_BG);
    gfx_pixel(0, 0, C_ACCENT);
    gfx_pixel(1, 0, C_WARN);
    CHECK(gfx_get(0, 0) == C_ACCENT, "even pixel");
    CHECK(gfx_get(1, 0) == C_WARN, "odd pixel next to it kept apart");
    CHECK(gfx_get(2, 0) == C_BG, "neighbour untouched");

    gfx_pixel(-1, 0, C_FG);
    gfx_pixel(GFX_W, GFX_H, C_FG);
    gfx_fill_rect(GFX_W - 2, GFX_H - 2, 10, 10, C_OK);
    CHECK(gfx_get(GFX_W - 1, GFX_H - 1) == C_OK, "clipped rect still draws inside");
    gfx_fill_rect(-5, -5, 6, 6, C_INFO);
    CHECK(gfx_get(0, 0) == C_INFO, "rect clipped at the origin");

    CHECK(gfx_text_width("", &FONT_5X7) == 0, "empty text");
    CHECK(gfx_text_width("A", &FONT_5X7) == 5, "one glyph is 5 wide");
    CHECK(gfx_text_width("AB", &FONT_5X7) == 11, "two glyphs: 5 + gap + 5");
    CHECK(gfx_text_width("AB", &FONT_5X7_X2) == 22, "scaled");
    CHECK(gfx_text_fit("ABCDEFGH", &FONT_5X7, 17) == 3, "3 glyphs fit 17 px");

    gfx_clear(C_BG);
    int end = gfx_text(0, 0, "I", &FONT_5X7, C_FG);
    CHECK(end == 6, "advance");
    int lit = 0;
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 6; x++)
            lit += gfx_get(x, y) != C_BG;
    CHECK(lit > 0 && lit < 30, "glyph drew some, not all, of its cell (%d)", lit);

    uint32_t g = gfx_generation();
    gfx_fill_rect(0, 0, GFX_W, GFX_H, C_FG);
    CHECK(gfx_generation() != g, "drawing moves the generation");
    gfx_wipe();
    const uint8_t *fb = gfx_framebuffer();
    int nonzero = 0;
    for (int i = 0; i < GFX_FB_BYTES; i++) nonzero += fb[i] != 0;
    CHECK(nonzero == 0, "wipe zeroes every byte");

    CHECK(gfx_role_lit(C_FG) && !gfx_role_lit(C_BG) && !gfx_role_lit(C_SEL_FG),
          "monochrome mapping");
    return CHECK_DONE("test_gfx [" GFX_PROFILE_NAME "]");
}
