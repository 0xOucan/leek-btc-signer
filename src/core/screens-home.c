/*
 * Home and its menus. See screens.h.
 *
 * Every entry is in place from the start, so the tree can be walked and
 * reviewed now. Entries whose feature is not built yet open a screen naming
 * the roadmap task that builds it.
 */
#include "screens.h"

#include <stdio.h>

#include "theme.h"
#include "views-psbt.h"
#include "views-seed.h"
#include "views-tools.h"

/* ---- Not built yet ---------------------------------------------------- */

#define PENDING(id_, title_, body_)                                         \
    static const message_t id_##_msg = { (title_), (body_) };              \
    static const screen_t id_ = SCREEN_MESSAGE("NotYetImplementedView", id_##_msg)

PENDING(CAMERA_SEED_PENDING, "New seed",
        "Camera entropy arrives with T52.");
PENDING(EXPLORER_PENDING, "Address explorer", "Arrives with T51.");
PENDING(VERIFY_PENDING, "Verify address", "Arrives with T40.");
PENDING(ADVANCED_PENDING, "Advanced", "Settings arrive with T41.");
PENDING(HARDWARE_PENDING, "Hardware", "Settings arrive with T41.");

/* ---- I/O test --------------------------------------------------------- */

/*
 * Counts every input event, for bring-up: button polarity and encoder
 * direction are the first things a new board gets wrong. BACK is counted
 * too, so leaving is a BACK hold, which goes home from anywhere.
 */
static struct {
    unsigned count[IN_HOLD + 1];
    input_ev_t last;
} io;

static void io_enter(const screen_t *s)
{
    (void)s;
    for (unsigned i = 0; i <= IN_HOLD; i++) {
        io.count[i] = 0;
    }
    io.last = IN_NONE;
}

static void io_render(const screen_t *s)
{
    (void)s;
    static const input_ev_t shown[] = { IN_UP, IN_DOWN, IN_OK, IN_BACK };
    char line[32];
    ui_draw_title("I/O test");
    int y = THEME.top_h + THEME.pad + 2;
    for (unsigned i = 0; i < sizeof shown / sizeof shown[0]; i++) {
        input_ev_t ev = shown[i];
        snprintf(line, sizeof line, "%-5s %u", input_name(ev), io.count[ev]);
        gfx_text(THEME.pad, y, line, THEME.body, ev == io.last ? C_ACCENT : C_FG);
        y += THEME.line_h;
    }
    snprintf(line, sizeof line, "Last: %s", input_name(io.last));
    y += THEME.line_h / 2;
    gfx_text(THEME.pad, y, line, THEME.body, C_FG);
    /* The way out, where there is room for it; a 64-row panel has none. */
    const char *hint = "Hold BACK 3s to exit";
    y += THEME.line_h * 2;
    const int hint_lines =
        gfx_text_width(hint, THEME.body) <= GFX_W - 2 * THEME.pad ? 1 : 2;
    if (y + hint_lines * THEME.line_h <= GFX_H) {
        ui_draw_wrapped(y, hint, C_MUTED);
    }
}

static bool io_input(const screen_t *s, input_ev_t ev)
{
    (void)s;
    if (ev <= IN_HOLD) {
        io.count[ev]++;
        io.last = ev;
    }
    return true;
}

static const screen_t IO_TEST = {
    .name = "IOTestView", .enter = io_enter, .render = io_render, .input = io_input,
};

/* ---- Version ---------------------------------------------------------- */

static char version_text[160] = "Unknown";
static const message_t version_msg = { "Version", version_text };
static const screen_t VERSION = SCREEN_MESSAGE("VersionView", version_msg);

void screens_set_version(const char *version, const char *board,
                         const char *profile, const char *image)
{
    snprintf(version_text, sizeof version_text,
             "Leek BTC Signer\n%s\n\nBoard: %s\nDisplay: %s\nImage: %s",
             version, board, profile, image);
}

/* ---- Menus ------------------------------------------------------------ */

static const menu_item_t tools_items[] = {
    { "New seed (camera)", &CAMERA_SEED_PENDING, NULL },
    { "New seed (dice)", NULL, ToolsDiceEntropyMnemonicLengthView },
    { "Calc 12th/24th word", NULL, ToolsCalcFinalWordNumWordsView },
    { "Address explorer", &EXPLORER_PENDING, NULL },
    { "Verify address", &VERIFY_PENDING, NULL },
};
static menu_t tools_menu = { "Tools", tools_items, 5, 0, 0 };
static const screen_t TOOLS = SCREEN_MENU("ToolsMenuView", tools_menu);

static void enter_12(void) { SeedMnemonicEntryView(12); }
static void enter_24(void) { SeedMnemonicEntryView(24); }

/* LoadSeedView, with Electrum seeds off (SeedSigner's default). */
static const menu_item_t load_items[] = {
    { "Scan a SeedQR", &SCREEN_SCAN_SEED, NULL },
    { "Enter 12-word seed", NULL, enter_12 },
    { "Enter 24-word seed", NULL, enter_24 },
    { "Create a seed", &TOOLS, NULL },
};
static menu_t load_menu = { "Load A Seed", load_items, 4, 0, 0 };
const screen_t SCREEN_LOAD_SEED = SCREEN_MENU("LoadSeedView", load_menu);

static const menu_item_t settings_items[] = {
    { "Advanced", &ADVANCED_PENDING, NULL },
    { "Hardware", &HARDWARE_PENDING, NULL },
    { "I/O test", &IO_TEST, NULL },
    { "Version", &VERSION, NULL },
};
static menu_t settings_menu = { "Settings", settings_items, 4, 0, 0 };
static const screen_t SETTINGS = SCREEN_MENU("SettingsMenuView", settings_menu);

static const menu_item_t home_items[] = {
    { "Scan", &SCREEN_SCAN, NULL },
    { "Seeds", NULL, SeedsMenuView },
    { "Tools", &TOOLS, NULL },
    { "Settings", &SETTINGS, NULL },
};
static menu_t home_menu = { "Home", home_items, 4, 0, 0 };

/* Back at MainMenuView, SeedSigner drops the flow in progress. */
static void home_resume(const screen_t *s)
{
    (void)s;
    psbt_flow_clear();
}

const screen_t SCREEN_HOME = {
    .name = "MainMenuView", .enter = menu_enter, .render = menu_render, .input = menu_input,
    .resume = home_resume, .ctx = (void *)&home_menu,
};
