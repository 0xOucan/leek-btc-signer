/*
 * SeedSigner's OpeningSplashView: the name and version at boot, for a
 * moment, then the main menu; any key goes straight on.
 *
 * SeedSigner's splash also shows its sponsors' logos ("With support from:").
 * They support SeedSigner, not this device, so here the splash names the
 * project it is built on instead.
 */
#include "views-splash.h"

#include "screens.h"
#include "theme.h"

static uint32_t shown_at;

static void splash_enter(const screen_t *s)
{
    (void)s;
    shown_at = ui_now_ms();
}

static void splash_render(const screen_t *s)
{
    (void)s;
    const int fh = gfx_font_height(THEME.body);
    const int y = (GFX_H - 4 * THEME.line_h) / 2;
    gfx_text_centered(0, GFX_W, y, "Leek BTC Signer", THEME.body, C_ACCENT);
    gfx_text_centered(0, GFX_W, y + THEME.line_h + 2, screens_version(), THEME.body, C_FG);
    gfx_text_centered(0, GFX_W, y + 3 * THEME.line_h, "built on SeedSigner", THEME.body, C_MUTED);
    (void)fh;
}

static bool splash_tick(const screen_t *s)
{
    (void)s;
    if (ui_now_ms() - shown_at >= SPLASH_MS) {
        ui_pop();
        return true;
    }
    return false;
}

static bool splash_input(const screen_t *s, input_ev_t ev)
{
    (void)s;
    (void)ev;
    ui_pop();       /* any key: on to the main menu */
    return true;
}

const screen_t SCREEN_SPLASH = {
    .name = "OpeningSplashView", .enter = splash_enter, .render = splash_render, .input = splash_input,
    .tick = splash_tick,
};
