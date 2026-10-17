/*
 * SeedSigner's power views: the options (Restart, Power off), the restart,
 * which wipes everything in memory before the platform restarts, and the
 * power-off screen, which only says it is safe to unplug: the device keeps
 * nothing to lose.
 *
 * SeedSigner's main menu has a power button where other screens have BACK;
 * with the encoder, BACK on the main menu is that button.
 */
#include "views-power.h"

#include "screens-common.h"
#include "seed-storage.h"
#include "theme.h"
#include "views.h"

static void (*restart_hook)(void);

void ui_set_restart_hook(void (*fn)(void))
{
    restart_hook = fn;
}

/* ---- RestartView ----------------------------------------------------------- */

static void restart_render(const screen_t *s)
{
    (void)s;
    ui_draw_title("Restarting");
    ui_draw_wrapped(THEME.top_h + THEME.pad,
                    "Leek BTC Signer is restarting.\n\nAll in-memory data will be wiped.", C_FG);
}

static bool restart_input(const screen_t *s, input_ev_t ev)
{
    (void)s;
    (void)ev;
    return true;    /* nothing to do but wait */
}

static const screen_t RESTART = {
    .name = "RestartView", .render = restart_render, .input = restart_input,
};

static void RestartView(void)
{
    ui_push(&RESTART);
    ui_note("restart");
    /* Show the message (flushed to the panel), wipe, then restart. */
    ui_render();
    ui_flush();
    storage_wipe_all();
    if (restart_hook) {
        restart_hook();
    }
}

/* ---- PowerOffView ------------------------------------------------------------ */

static void power_off_select(int i)
{
    (void)i;
    ui_pop();       /* Destination(BackStackView) */
}

static void PowerOffView(void)
{
    const view_spec_t v = { .name = "PowerOffView", .title = "Just Unplug It",
                            .text = "It is safe to disconnect power at any time.", .buttons = { "OK" },
                            .n_buttons = 1, .select = power_off_select };
    view_push(&v);
}

/* ---- PowerOptionsView -------------------------------------------------------- */

static void power_select(int i)
{
    if (i == 0) {
        RestartView();
    } else {
        PowerOffView();
    }
}

void PowerOptionsView(void)
{
    const view_spec_t v = { .name = "PowerOptionsView", .title = "Reset / Power",
                            .buttons = { "Restart", "Power off" }, .n_buttons = 2, .select = power_select };
    view_push(&v);
}
