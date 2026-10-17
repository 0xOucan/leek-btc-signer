/*
 * SeedSigner's settings views, ported view by view: the menu at its three
 * levels (general, Advanced, Hardware), each setting's selection screen,
 * the warning when a multiselect is left empty, the language list, Donate,
 * and SettingsQR. The menus and screens are built from settings.c's port
 * of SeedSigner's SettingsDefinition, so a setting shows where SeedSigner
 * shows it, with its options and help text.
 *
 * A single select behaves as SeedSigner's: choosing a new option sets it
 * and stays on the screen, choosing the one already set returns to the
 * menu. A multiselect toggles in place until BACK.
 */
#include "views-settings.h"

#include <stdio.h>
#include <string.h>

#include "screens.h"
#include "settings.h"
#include "theme.h"
#include "views.h"

static void no_back(void)
{
}

/* ---- SettingsSelectionRequiredWarningView -------------------------------- */

static void required_select(int i)
{
    (void)i;
    ui_pop();       /* back to the setting, still empty */
}

static void SettingsSelectionRequiredWarningView(int id)
{
    char text[96];
    snprintf(text, sizeof text, "At least one option must be selected for \"%s\".",
             SETTINGS_DEF[id].display_name);
    const view_spec_t v = { .name = "SettingsSelectionRequiredWarningView", .status = true,
                            .kind = NOTICE_WARNING, .title = "Selection Required", .text = text,
                            .buttons = { "Return to setting" }, .n_buttons = 1,
                            .select = required_select, .back = no_back };
    view_push(&v);
}

/* ---- SettingsEntryUpdateSelectionView -------------------------------------- */

static struct {
    int id;
    int sel, top;
    char labels[8][40];
    const char *ptrs[8];
} sel_state;

static int entry_id(const screen_t *s)
{
    return (int)(intptr_t)s->ctx;
}

static void entry_enter(const screen_t *s)
{
    /* Highlight the option set (a multiselect's first). */
    sel_state.id = entry_id(s);
    sel_state.sel = 0;
    sel_state.top = 0;
    const uint32_t m = settings_get_mask(sel_state.id);
    for (int i = 0; i < SETTINGS_DEF[sel_state.id].n_options; i++) {
        if (m & (1u << i)) {
            sel_state.sel = i;
            break;
        }
    }
}

static void entry_render(const screen_t *s)
{
    const int id = entry_id(s);
    const setting_def_t *d = &SETTINGS_DEF[id];
    /* SeedSigner titles this screen "Settings" with the setting's name
       below; on a small panel the name is the title, and the help text
       (when it has one) sits above the options. */
    ui_draw_title(d->display_name);
    int y = THEME.row_y0;
    if (d->help_text) {
        y = ui_draw_wrapped(THEME.top_h + THEME.pad, d->help_text, C_MUTED) + 2;
    }
    const uint32_t m = settings_get_mask(id);
    const bool multi = d->type == STYPE_MULTISELECT;
    for (int i = 0; i < d->n_options && i < 8; i++) {
        const bool on = (m >> i) & 1u;
        /* The checked options carry a mark, as SeedSigner's check icons. */
        snprintf(sel_state.labels[i], sizeof sel_state.labels[i], "%s%s",
                 multi ? (on ? "[x] " : "[ ] ") : (on ? "* " : "  "), d->options[i].display_name);
        sel_state.ptrs[i] = sel_state.labels[i];
    }
    ui_draw_list_from(y, sel_state.ptrs, d->n_options, sel_state.sel, &sel_state.top);
}

static void entry_changed(int id)
{
    char text[64];
    settings_value_text(id, text, sizeof text);
    ui_note("setting %s=%s", SETTINGS_DEF[id].attr_name, text);
}

static bool entry_input(const screen_t *s, input_ev_t ev)
{
    const int id = entry_id(s);
    const setting_def_t *d = &SETTINGS_DEF[id];
    switch (ev) {
    case IN_UP:
        if (sel_state.sel > 0) sel_state.sel--;
        return true;
    case IN_DOWN:
        if (sel_state.sel + 1 < d->n_options) sel_state.sel++;
        return true;
    case IN_OK: {
        const uint32_t bit = 1u << sel_state.sel, m = settings_get_mask(id);
        if (d->type == STYPE_MULTISELECT) {
            settings_set_mask(id, m ^ bit);     /* toggle, and stay */
            entry_changed(id);
        } else if (m & bit) {
            ui_pop();                           /* unchanged: back to the menu */
        } else {
            settings_set_mask(id, bit);         /* set, and stay */
            entry_changed(id);
        }
        return true;
    }
    case IN_BACK:
        if (d->type == STYPE_MULTISELECT && settings_get_mask(id) == 0) {
            SettingsSelectionRequiredWarningView(id);
        } else {
            ui_pop();
        }
        return true;
    default:
        return false;
    }
}

static const char *entry_selected(const screen_t *s)
{
    const setting_def_t *d = &SETTINGS_DEF[entry_id(s)];
    return sel_state.sel < d->n_options ? d->options[sel_state.sel].display_name : NULL;
}

#define MAX_SETTINGS 32
static screen_t ENTRY_SCREENS[MAX_SETTINGS];

/* ---- LocaleSelectionView --------------------------------------------------- */

static void locale_select(int i)
{
    (void)i;
    settings_set_mask(setting_by_attr("locale"), 1u);
    ui_pop();
}

static void LocaleSelectionView(void)
{
    /* English is the only language this firmware has. */
    const view_spec_t v = { .name = "LocaleSelectionView", .title = "Language", .buttons = { "English" },
                            .n_buttons = 1, .select = locale_select };
    view_push(&v);
}

/* ---- DonateView ------------------------------------------------------------ */

static void donate_select(int i)
{
    (void)i;
    ui_pop();
}

static void DonateView(void)
{
    /* SeedSigner's: this port is built on SeedSigner's work. */
    const view_spec_t v = { .name = "DonateView", .title = "Donate",
                            .text = "SeedSigner is 100% free & open source, funded solely by the Bitcoin "
                                    "community.\n\nDonate onchain or LN at:\nseedsigner.com",
                            .buttons = { "OK" }, .n_buttons = 1, .select = donate_select };
    view_push(&v);
}

/* ---- SettingsMenuView ------------------------------------------------------ */

static menu_item_t general_items[MAX_SETTINGS + 4], advanced_items[MAX_SETTINGS + 1],
    hardware_items[MAX_SETTINGS];
static menu_t general_menu = { "Settings", general_items, 0, 0, 0 };
static menu_t advanced_menu = { "Advanced", advanced_items, 0, 0, 0 };
static menu_t hardware_menu = { "Hardware", hardware_items, 0, 0, 0 };

static void build(void);

static void menu_enter_built(const screen_t *s)
{
    build();
    menu_enter(s);
}

const screen_t SCREEN_SETTINGS = {
    .name = "SettingsMenuView", .enter = menu_enter_built, .render = menu_render, .input = menu_input,
    .ctx = (void *)&general_menu,
};
static const screen_t SETTINGS_ADVANCED = {
    .name = "SettingsMenuView", .enter = menu_enter, .render = menu_render, .input = menu_input,
    .ctx = (void *)&advanced_menu,
};
static const screen_t SETTINGS_HARDWARE = {
    .name = "SettingsMenuView", .enter = menu_enter, .render = menu_render, .input = menu_input,
    .ctx = (void *)&hardware_menu,
};

/* The menus list each visible setting at its level, in SeedSigner's order,
   then the level's own entries. */
static void build(void)
{
    static bool built;
    if (built) {
        return;
    }
    built = true;
    for (int id = 0; id < SETTINGS_DEF_COUNT && id < MAX_SETTINGS; id++) {
        const setting_def_t *d = &SETTINGS_DEF[id];
        ENTRY_SCREENS[id] = (screen_t){
            .name = "SettingsEntryUpdateSelectionView", .enter = entry_enter, .render = entry_render,
            .input = entry_input, .selected_label = entry_selected, .ctx = (void *)(intptr_t)id,
        };
        menu_item_t item = { d->display_name, &ENTRY_SCREENS[id], NULL };
        if (strcmp(d->attr_name, "locale") == 0) {
            item.next = NULL;
            item.action = LocaleSelectionView;
        }
        switch (d->visibility) {
        case SVIS_GENERAL:  general_items[general_menu.n++] = item; break;
        case SVIS_ADVANCED: advanced_items[advanced_menu.n++] = item; break;
        case SVIS_HARDWARE: hardware_items[hardware_menu.n++] = item; break;
        default:            break;      /* hidden, developer */
        }
    }
    general_items[general_menu.n++] = (menu_item_t){ "Advanced", &SETTINGS_ADVANCED, NULL };
    general_items[general_menu.n++] = (menu_item_t){ "I/O test", &SCREEN_IO_TEST, NULL };
    general_items[general_menu.n++] = (menu_item_t){ "Donate", NULL, DonateView };
    general_items[general_menu.n++] = (menu_item_t){ "Version", &SCREEN_VERSION, NULL };
    advanced_items[advanced_menu.n++] = (menu_item_t){ "Hardware", &SETTINGS_HARDWARE, NULL };
}

/* ---- SettingsIngestSettingsQRView ------------------------------------------ */

static char ingest_text[160];

static void ingest_select(int i)
{
    (void)i;
    ui_home_fresh();    /* Destination(MainMenuView) */
}

void settings_scanned(const char *text, size_t len)
{
    char data[1024];
    snprintf(data, sizeof data, "%.*s", (int)(len < sizeof data ? len : sizeof data - 1), text);
    settingsqr_t q;
    const char *err = settingsqr_parse(data, &q);
    if (err) {
        /* SeedSigner raises, to its UnhandledExceptionView. */
        snprintf(ingest_text, sizeof ingest_text, "This SettingsQR could not be applied (%s).", err);
        const view_spec_t v = { .name = "ErrorView", .status = true, .kind = NOTICE_ERROR,
                                .title = "Settings QR", .text = ingest_text, .buttons = { "OK" },
                                .n_buttons = 1, .select = ingest_select };
        view_replace(&v);
        ui_note("settingsqr %s", err);
        return;
    }
    settingsqr_apply(&q);
    /* No microSD, no persistence: always temporary. */
    if (q.has_name) {
        snprintf(ingest_text, sizeof ingest_text, "\"%s\"\n\nSettings updated in temporary memory", q.name);
    } else {
        snprintf(ingest_text, sizeof ingest_text, "Settings updated in temporary memory");
    }
    const view_spec_t v = { .name = "SettingsIngestSettingsQRView", .title = "Settings QR", .text = ingest_text,
                            .buttons = { "Home" }, .n_buttons = 1, .select = ingest_select, .back = no_back };
    view_replace(&v);
    ui_note("settingsqr applied %d", q.n);
}

/* ---- Opening one setting from elsewhere ------------------------------------ */

void settings_open_entry(const char *attr_name)
{
    const int id = setting_by_attr(attr_name);
    if (id < 0) {
        return;
    }
    build();
    /* As SeedSigner's clear_history Destination: the menus that lead to the
       setting are under it, so BACK climbs them to home. */
    view_clear_history();
    ui_push(&SCREEN_SETTINGS);
    if (SETTINGS_DEF[id].visibility != SVIS_GENERAL) {
        ui_push(&SETTINGS_ADVANCED);
    }
    if (SETTINGS_DEF[id].visibility == SVIS_HARDWARE) {
        ui_push(&SETTINGS_HARDWARE);
    }
    ui_push(&ENTRY_SCREENS[id]);
}
