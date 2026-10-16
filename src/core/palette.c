/*
 * What each role looks like. The colours are SeedSigner's GUIConstants
 * (src/seedsigner/gui/components.py), so the colour profiles read like
 * SeedSigner. Monochrome panels light every role but the dark ones.
 */
#include "gfx.h"

#if !GFX_MONO
static const uint32_t rgb[C_COUNT] = {
    [C_BG]        = 0x000000,
    [C_FG]        = 0xFCFCFC,   /* BODY_FONT_COLOR */
    [C_MUTED]     = 0x777777,   /* LABEL_FONT_COLOR */
    [C_ACCENT]    = 0xFF9F0A,   /* ACCENT_COLOR */
    [C_SEL_BG]    = 0xFF9F0A,   /* selected button: accent */
    [C_SEL_FG]    = 0x000000,   /* BUTTON_SELECTED_FONT_COLOR */
    [C_BUTTON_BG] = 0x2C2C2C,   /* BUTTON_BACKGROUND_COLOR */
    [C_INACTIVE]  = 0x414141,   /* INACTIVE_COLOR */
    [C_OK]        = 0x30D158,   /* SUCCESS_COLOR */
    [C_WARN]      = 0xFFD60A,   /* WARNING_COLOR */
    [C_DIRE]      = 0xFF5700,   /* DIRE_WARNING_COLOR */
    [C_ERROR]     = 0xFF1B0A,   /* ERROR_COLOR */
    [C_INFO]      = 0x409CFF,   /* INFO_COLOR */
    [C_TESTNET]   = 0x00F100,   /* TESTNET_COLOR */
    [C_REGTEST]   = 0x00CAF1,   /* REGTEST_COLOR */
    [C_QR_LIGHT]  = 0xFFFFFF,
};
#endif

bool gfx_role_lit(gfx_color_t c)
{
    return !(c == C_BG || c == C_SEL_FG || c == C_BUTTON_BG);
}

uint32_t gfx_role_rgb(gfx_color_t c)
{
    if ((unsigned)c >= C_COUNT) {
        c = C_BG;
    }
#if GFX_MONO
    /* What the simulator shows for an OLED: lit or not. */
    return gfx_role_lit(c) ? 0xFFFFFF : 0x000000;
#else
    return rgb[c];
#endif
}
