/*
 * SeedSigner's settings flow tests (tests/test_flows_settings.py), ported,
 * and every setting driven through its own screen. The menus are checked
 * against SeedSigner's definition (sim/vectors/seedsigner_settings.h):
 * each level lists exactly the settings SeedSigner shows there, in order,
 * and choosing each option leaves the value SeedSigner's option names.
 */
#include "btc-init.h"
#include "flow.h"
#include "views-power.h"
#include "views-settings.h"
#include "views-splash.h"
#include "vectors/seedsigner_settings.h"

#define N(a) (sizeof(a) / sizeof((a)[0]))

static void scan_text(const char *text)
{
    scan((const uint8_t *)text, strlen(text));
}

/* The labels of the menu on screen, in order. */
static int menu_labels(const char *out[], int max)
{
    const menu_t *m = ui_top()->render == menu_render ? ui_top()->ctx : NULL;
    int n = 0;
    for (int i = 0; m && i < m->n && n < max; i++) {
        out[n++] = m->items[i].label;
    }
    return n;
}

static void open_settings(void)
{
    choose("Settings");
    STEP("SettingsMenuView");
}

/* Each level of the menu lists the settings SeedSigner shows there, then
   the level's own entries. */
static void test_menus_match_definition(void)
{
    printf("== the settings menus, level by level, as SeedSigner's definition\n");
    static const struct { const char *vis; const char *path; const char *extra[4]; } levels[] = {
        { "general", NULL, { "Advanced", "I/O test", "Donate", "Version" } },
        { "advanced", "Advanced", { "Hardware" } },
        { "hardware", "Hardware", { NULL } },
    };
    fresh();
    open_settings();
    for (size_t l = 0; l < N(levels); l++) {
        if (levels[l].path) {
            choose(levels[l].path);
            STEP("SettingsMenuView");
        }
        const char *got[40];
        const int n = menu_labels(got, 40);
        int k = 0;
        for (size_t i = 0; i < N(VEC_SETTINGS); i++) {
            if (strcmp(VEC_SETTINGS[i].visibility, levels[l].vis) != 0) continue;
            CHECK(k < n && strcmp(got[k], VEC_SETTINGS[i].display_name) == 0, "%s menu item %d: %s, want %s",
                  levels[l].vis, k, k < n ? got[k] : "-", VEC_SETTINGS[i].display_name);
            k++;
        }
        for (int e = 0; e < 4 && levels[l].extra[e]; e++, k++) {
            CHECK(k < n && strcmp(got[k], levels[l].extra[e]) == 0, "%s menu item %d: %s, want %s",
                  levels[l].vis, k, k < n ? got[k] : "-", levels[l].extra[e]);
        }
        CHECK(k == n, "%s menu: %d items, want %d", levels[l].vis, n, k);
    }
    /* BACK climbs the levels to home. */
    key(IN_BACK);
    key(IN_BACK);
    STEP("SettingsMenuView");
    key(IN_BACK);
    STEP("MainMenuView");
}

/* Every visible selection setting: each option chosen on its screen. */
static void test_every_setting(void)
{
    printf("== every setting, every option, through its screen\n");
    unsigned options = 0;
    for (size_t i = 0; i < N(VEC_SETTINGS); i++) {
        const vec_setting_t *v = &VEC_SETTINGS[i];
        if (strcmp(v->visibility, "hidden") == 0 || strcmp(v->attr_name, "locale") == 0) continue;
        const bool multi = strcmp(v->type, "multiselect") == 0;
        for (unsigned o = 0; o < v->n_options; o++) {
            fresh();
            notes_reset();
            open_settings();
            if (strcmp(v->visibility, "general") != 0) choose("Advanced");
            if (strcmp(v->visibility, "hardware") == 0) choose("Hardware");
            choose(v->display_name);
            STEP("SettingsEntryUpdateSelectionView");
            const int id = setting_by_attr(v->attr_name);
            char before[64], after[64], want[96];
            settings_value_text(id, before, sizeof before);
            choose(v->options[o].display_name);
            settings_value_text(id, after, sizeof after);
            if (multi) {
                /* Toggled, and still on the setting. */
                STEP("SettingsEntryUpdateSelectionView");
                CHECK(strcmp(before, after) != 0, "%s: %s did not toggle", v->attr_name, v->options[o].value);
                key(IN_BACK);
                if (after[0] == '\0') {
                    STEP("SettingsSelectionRequiredWarningView");
                    choose("Return to setting");
                    STEP("SettingsEntryUpdateSelectionView");
                    choose(v->options[o].display_name);
                    key(IN_BACK);
                }
                STEP("SettingsMenuView");
            } else if (strcmp(before, v->options[o].value) == 0) {
                /* Already set: back to the menu, unchanged. */
                STEP("SettingsMenuView");
                CHECK(strcmp(after, before) == 0, "%s changed", v->attr_name);
            } else {
                STEP("SettingsEntryUpdateSelectionView");
                CHECK(strcmp(after, v->options[o].value) == 0, "%s is %s, want %s", v->attr_name, after,
                      v->options[o].value);
                snprintf(want, sizeof want, "setting %s=%s", v->attr_name, v->options[o].value);
                CHECK(noted(want), "no \"%s\"", want);
            }
            options++;
        }
    }
    printf("   %u options chosen\n", options);

    /* The settings take effect: Testnet changes SETTINGS.network. */
    fresh();
    open_settings();
    choose("Advanced");
    choose("Bitcoin network");
    choose("Testnet");
    CHECK(SETTINGS.network == BTC_NET_TEST, "the network did not change");
}

/* tests/test_flows_settings.py ---------------------------------------------- */

/* test_persistent_settings, as SeedSigner runs it with no microSD card:
   Disabled is the only option, and choosing it changes nothing. */
static void test_persistent_settings(void)
{
    printf("== test_persistent_settings (no microSD: Disabled only)\n");
    fresh();
    open_settings();
    choose("Persistent settings");
    STEP("SettingsEntryUpdateSelectionView");
    const char *sel = ui_selected_label();
    CHECK(sel && strcmp(sel, "Disabled") == 0, "selected %s", sel ? sel : "-");
    key(IN_DOWN);
    CHECK(strcmp(ui_selected_label(), "Disabled") == 0, "an option other than Disabled");
    choose("Disabled");
    STEP("SettingsMenuView");
}

static void test_multiselect(void)
{
    printf("== test_multiselect\n");
    fresh();
    SETTINGS.sig_single = SETTINGS.sig_multi = true;
    open_settings();
    choose("Advanced");
    STEP("SettingsMenuView");
    choose("Sig types");
    STEP("SettingsEntryUpdateSelectionView");
    choose("Single Sig");       /* deselect first option */
    choose("Multisig");         /* deselect second option */
    choose("Multisig");         /* select second option */
    choose("Multisig");         /* deselect second option */
    key(IN_BACK);
    /* Both options were deselected: the warning, then the setting again. */
    STEP("SettingsSelectionRequiredWarningView");
    key(IN_BACK);
    STEP("SettingsSelectionRequiredWarningView");      /* no way back */
    choose("Return to setting");
    STEP("SettingsEntryUpdateSelectionView");
    choose("Single Sig");
    key(IN_BACK);
    STEP("SettingsMenuView");
    CHECK(SETTINGS.sig_single && !SETTINGS.sig_multi, "sig types");
}

static void test_io_test(void)
{
    printf("== test_io_test\n");
    fresh();
    open_settings();
    choose("I/O test");
    STEP("IOTestView");
    key(IN_HOLD);
    STEP("MainMenuView");
}

static void test_donate(void)
{
    printf("== test_donate\n");
    fresh();
    open_settings();
    choose("Donate");
    STEP("DonateView");
    choose("OK");
    STEP("SettingsMenuView");
}

static void test_settingsqr(void)
{
    printf("== test_settingsqr\n");
    static const char *const qrs[] = {
        "settings::v1 name=Total_noob_mode persistent=E xpub_qr=urca,sta denom=thr network=M qr_density=M "
        "sigs=ss scripts=nat xpub_details=E passphrase=E camera=0 compact_seedqr=E bip85=D priv_warn=E "
        "dire_warn=E partners=E",
        "settings::v1 name=Ephemeral_noob_mode persistent=D xpub_qr=urca,sta denom=thr network=M "
        "qr_density=M sigs=ss scripts=nat xpub_details=E passphrase=E camera=0 compact_seedqr=E bip85=D "
        "priv_warn=E dire_warn=E partners=E",
    };
    for (size_t i = 0; i < N(qrs); i++) {
        fresh();
        SETTINGS.sig_multi = true;
        choose("Scan");
        STEP("ScanView");
        scan_text(qrs[i]);
        STEP("SettingsIngestSettingsQRView");
        choose("Home");
        STEP("MainMenuView");
        /* Applied; and with no microSD, persistent settings stay Disabled. */
        char v[16];
        settings_value_text(setting_by_attr("persistent_settings"), v, sizeof v);
        CHECK(strcmp(v, "D") == 0, "persistent settings %s", v);
        CHECK(SETTINGS.sig_single && !SETTINGS.sig_multi && SETTINGS.script_types[SCRIPT_NAT] &&
              !SETTINGS.script_types[SCRIPT_NES] && SETTINGS.camera_rotation == 0, "not applied");
    }

    /* One SeedSigner refuses: nothing changes, and the device says so. */
    fresh();
    choose("Scan");
    scan_text("settings::v1 network=fake_value");
    STEP("UnhandledExceptionView");     /* SeedSigner raises InvalidSettingsQRData */
    choose("Back to Main Menu");
    STEP("MainMenuView");
    CHECK(SETTINGS.network == BTC_NET_MAIN, "a refused SettingsQR changed a setting");
}

/* view.py's PowerOptionsView, RestartView and PowerOffView. */
static int restarts;
static void on_restart(void) { restarts++; }

static void test_power(void)
{
    printf("== power options: power off, restart wipes and restarts\n");
    fresh();
    ui_set_restart_hook(on_restart);
    key(IN_BACK);               /* the main menu's power button */
    STEP("PowerOptionsView");
    choose("Power off");
    STEP("PowerOffView");
    choose("OK");
    STEP("PowerOptionsView");
    key(IN_BACK);
    STEP("MainMenuView");

    seed_t s;
    seed_from_mnemonic(&s, "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon "
                           "abandon about", "");
    storage_set_pending(&s);
    seed_clear(&s);
    storage_finalize();
    restarts = 0;
    key(IN_BACK);
    choose("Restart");
    STEP("RestartView");
    CHECK(restarts == 1, "%d restarts", restarts);
    CHECK(storage_count() == 0 && storage_pending() == NULL, "the seeds outlived the restart");
    key(IN_BACK);
    STEP("RestartView");        /* nothing to do but wait */
}

/* screensaver.py's OpeningSplashView. */
static void test_splash(void)
{
    printf("== opening splash: leaves by itself, or on any key\n");
    fresh();
    ui_push(&SCREEN_SPLASH);
    ui_render();
    STEP("OpeningSplashView");
    now += SPLASH_MS / 2;
    ui_tick();
    STEP("OpeningSplashView");
    now += SPLASH_MS;
    ui_tick();
    STEP("MainMenuView");
    ui_push(&SCREEN_SPLASH);
    key(IN_OK);
    STEP("MainMenuView");       /* the key only ends the splash */
    CHECK(ui_depth() == 1, "a key on the splash also chose a menu item");
}

int main(void)
{
    btc_init(rand_fill);
    ui_set_clock(clock_fn);
    ui_set_note_hook(note);
    test_menus_match_definition();
    test_every_setting();
    test_persistent_settings();
    test_multiselect();
    test_io_test();
    test_donate();
    test_settingsqr();
    test_power();
    test_splash();
    fresh();
    return CHECK_DONE("test_settings_flows");
}
