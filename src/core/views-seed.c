/*
 * SeedSigner's seed views (views/seed_views.py), ported view by view: the
 * seeds menu, loading a seed by scan or by typing it (BIP-39 or Electrum),
 * finalising it with an optional BIP-39 passphrase (an Electrum seed's
 * custom extension), its options, discarding it, backing it up as words,
 * and exporting its xpub. An Electrum seed gets SeedSigner's overrides:
 * native segwit at m/0h or m/1h, and no BIP-85 or SeedQR. Each function or screen is named after the
 * View it ports, and the screen carries that name.
 *
 * Where a View leads to one not ported yet, it opens that View's placeholder,
 * which names it (NotYetImplementedView).
 */
#include "views-seed.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mnemonic-entry.h"
#include "screens.h"
#include "seed-storage.h"
#include "settings.h"
#include "text-entry.h"
#include "theme.h"
#include "btc-init.h"
#include "keypad.h"
#include "qrcode.h"
#include "views-address.h"
#include "views-message.h"
#include "views-psbt.h"
#include "views-tools.h"
#include "views.h"
#include "wally_core.h"

/* ---- Helpers ---------------------------------------------------------- */

static char fp_buf[9];

static const char *fingerprint(const seed_t *s)
{
    seed_fingerprint_hex(s, fp_buf);
    return fp_buf;
}

/* Seed.passphrase_label: Electrum calls its passphrase a custom extension. */
static const char *passphrase_label(const seed_t *s)
{
    return s && s->electrum ? "Custom Extension" : "BIP-39 Passphrase";
}

/* SeedSigner's show_back_button=False. */
static void no_back(void)
{
}

/* A View not ported yet: SeedSigner's NotYetImplementedView, naming it. */
static void not_yet(const char *view)
{
    char text[96];
    snprintf(text, sizeof text, "%s is not ported yet.", view);
    const view_spec_t v = { .name = "NotYetImplementedView", .title = "Work In Progress",
                            .text = text, .status = true, .kind = NOTICE_INFO,
                            .buttons = { "OK" }, .n_buttons = 1 };
    view_push(&v);
}

/* ---- SeedOptionsView -------------------------------------------------- */

enum { OPT_SCAN_PSBT, OPT_EXPORT_XPUB, OPT_EXPLORER, OPT_BACKUP, OPT_SIGN_MESSAGE,
       OPT_BIP85, OPT_DISCARD };

static int options_map[8];

static void seed_options_select(int i)
{
    const int seed = view_arg();
    switch (options_map[i]) {
    case OPT_SCAN_PSBT:
        psbt_flow_set_seed(seed);
        ui_push(&SCREEN_SCAN_PSBT);
        break;
    case OPT_EXPORT_XPUB:   SeedExportXpubSigTypeView(seed); break;
    case OPT_EXPLORER:      tools_explorer_from_seed(seed); break;
    case OPT_BACKUP:        SeedBackupView(seed); break;
    case OPT_SIGN_MESSAGE:  message_from_seed(seed); break;
    case OPT_BIP85:         SeedBIP85SelectNumWordsView(seed); break;
    case OPT_DISCARD:       SeedDiscardView(seed); break;
    }
}

static void seed_options_back(void)
{
    ui_home_fresh();    /* SeedSigner: Destination(MainMenuView) */
}

static void seed_options(int seed, bool fresh)
{
    view_spec_t v = { .name = "SeedOptionsView", .title = fingerprint(storage_seed(seed)),
                      .select = seed_options_select, .back = seed_options_back, .arg = seed,
                      .destructive_last = true };
    int n = 0;
    v.buttons[n] = "Scan transaction"; options_map[n++] = OPT_SCAN_PSBT;
    v.buttons[n] = "Export xpub";      options_map[n++] = OPT_EXPORT_XPUB;
    v.buttons[n] = "Address explorer"; options_map[n++] = OPT_EXPLORER;
    v.buttons[n] = "Backup seed";      options_map[n++] = OPT_BACKUP;
    if (SETTINGS.message_signing) {
        v.buttons[n] = "Sign message"; options_map[n++] = OPT_SIGN_MESSAGE;
    }
    if (SETTINGS.bip85_child_seeds && !storage_seed(seed)->electrum) {     /* bip85_supported */
        v.buttons[n] = "BIP-85 child seed"; options_map[n++] = OPT_BIP85;
    }
    v.buttons[n] = "Discard seed";     options_map[n++] = OPT_DISCARD;
    v.n_buttons = n;
    if (fresh) {
        view_clear_history();   /* clear_history=True */
    }
    /* A seed loaded mid-flow goes back to it (resume_main_flow), in
       SeedSigner's order: address verification, the explorer, a message to
       sign, a PSBT. */
    if (address_verify_resume(seed) || tools_explorer_resume(seed) || message_resume(seed) ||
        psbt_flow_resume(seed)) {
        return;
    }
    view_push(&v);
}

void SeedOptionsView(int seed)
{
    seed_options(seed, false);
}

/* ---- SeedsMenuView ---------------------------------------------------- */

static void seeds_menu_select(int i)
{
    if (i < storage_count()) {
        SeedOptionsView(i);
    } else {
        ui_push(&SCREEN_LOAD_SEED);
    }
}

void SeedsMenuView(void)
{
    if (storage_count() == 0) {
        ui_push(&SCREEN_LOAD_SEED);     /* the redirect SeedSigner makes */
        return;
    }
    static char labels[SEED_STORAGE_MAX][9];
    view_spec_t v = { .name = "SeedsMenuView", .title = "In-Memory Seeds", .select = seeds_menu_select };
    for (int i = 0; i < storage_count(); i++) {
        seed_fingerprint_hex(storage_seed(i), labels[i]);
        v.buttons[v.n_buttons++] = labels[i];
    }
    v.buttons[v.n_buttons++] = "Load a seed";
    view_push(&v);
}

/* ---- SeedFinalizeView and the passphrase views ------------------------ */

static void finalize_done(void)
{
    const int i = storage_finalize();
    if (i < 0) {
        const view_spec_t v = { .name = "ErrorView", .title = "Error", .status = true,
                                .kind = NOTICE_ERROR, .headline = "Too many seeds",
                                .text = "Discard a seed to load another.", .buttons = { "OK" },
                                .n_buttons = 1 };
        view_push(&v);
        return;
    }
    seed_options(i, true);
}

static void finalize_select(int i)
{
    if (i == 0) {
        finalize_done();
    } else {
        SeedAddPassphraseView();
    }
}

void SeedFinalizeView(void)
{
    seed_t *s = storage_pending();
    if (s == NULL) {
        return;
    }
    /* The fingerprint without the passphrase, as SeedSigner shows it here. */
    seed_t *bare = storage_pending();
    char saved[SEED_PASSPHRASE_MAX + 1];
    snprintf(saved, sizeof saved, "%s", bare->passphrase);
    seed_set_passphrase(bare, "");
    char text[48];
    snprintf(text, sizeof text, "Fingerprint\n%s", fingerprint(bare));
    seed_set_passphrase(bare, saved);
    wally_bzero(saved, sizeof saved);

    view_spec_t v = { .name = "SeedFinalizeView", .title = "Finalize Seed", .text = text,
                      .select = finalize_select };
    v.buttons[v.n_buttons++] = "Done";
    if (SETTINGS.passphrase != OPT_DISABLED) {
        v.buttons[v.n_buttons++] = passphrase_label(s);
    }
    view_push(&v);
    ui_note("fingerprint %s", fp_buf);
}

/* SeedAddPassphraseView: the character wheel. */
static TextEntry passphrase_entry;

static void passphrase_enter(const screen_t *s)
{
    (void)s;
    text_entry_reset(&passphrase_entry);
    const seed_t *p = storage_pending();
    if (p) {
        snprintf(passphrase_entry.text, sizeof passphrase_entry.text, "%s", p->passphrase);
        passphrase_entry.length = (int)strlen(passphrase_entry.text);
    }
}

static void passphrase_render(const screen_t *s)
{
    (void)s;
    ui_draw_title(passphrase_label(storage_pending()));
    int y = THEME.top_h + THEME.pad;
    /* The tail of the passphrase that fits, then the selector. */
    const int fit = gfx_text_fit(passphrase_entry.text, THEME.body, GFX_W - 2 * THEME.pad - 6);
    const char *tail = passphrase_entry.text + (passphrase_entry.length > fit ? passphrase_entry.length - fit : 0);
    const int x = gfx_text(THEME.pad, y, tail, THEME.body, C_FG);
    gfx_text(x, y, "_", THEME.body, C_ACCENT);
    y += THEME.line_h * 2;
    char label[16];
    for (int off = -1; off <= 1; off++) {
        text_entry_label_offset(&passphrase_entry, off, label, sizeof label);
        const int w = GFX_W / 3;
        if (off == 0) {
            gfx_fill_rect(w, y - 2, w, gfx_font_height(THEME.body) + 4, C_SEL_BG);
        }
        gfx_text_centered((off + 1) * w, w, y, label, THEME.body, off == 0 ? C_SEL_FG : C_MUTED);
    }
}

static void passphrase_finish(bool back)
{
    seed_t *p = storage_pending();
    if (p) {
        seed_set_passphrase(p, passphrase_entry.text);
    }
    /* As set: an Electrum extension of spaces normalises to nothing. */
    const bool empty = p ? p->passphrase[0] == '\0' : passphrase_entry.length == 0;
    text_entry_clear(&passphrase_entry);
    ui_pop();
    if (back || empty) {
        SeedAddPassphraseExitDialogView();
    } else {
        SeedReviewPassphraseView();
    }
}

static bool passphrase_input(const screen_t *s, input_ev_t ev)
{
    (void)s;
    switch (ev) {
    case IN_UP:   text_entry_scroll(&passphrase_entry, -1); return true;
    case IN_DOWN: text_entry_scroll(&passphrase_entry, +1); return true;
    case IN_OK:
        if (text_entry_accept(&passphrase_entry) == TEXT_ENTRY_DONE) {
            passphrase_finish(false);
        }
        return true;
    case IN_BACK:
        /* SeedSigner's back arrow leaves with the text kept (the dialog then
           offers to edit or discard it); deleting is the wheel's DEL. An open
           block of letters closes first. */
        if (passphrase_entry.in_group) {
            text_entry_back(&passphrase_entry);
        } else {
            passphrase_finish(true);
        }
        return true;
    default:
        return false;
    }
}

static void passphrase_leave(const screen_t *s)
{
    (void)s;
    text_entry_clear(&passphrase_entry);
    gfx_wipe();
}

static const screen_t ADD_PASSPHRASE = {
    .name = "SeedAddPassphraseView", .enter = passphrase_enter, .render = passphrase_render,
    .input = passphrase_input, .leave = passphrase_leave,
};

void SeedAddPassphraseView(void)
{
    ui_push(&ADD_PASSPHRASE);
}

static void exit_dialog_select(int i)
{
    ui_pop();
    if (i == 0) {
        SeedAddPassphraseView();
        return;
    }
    seed_t *p = storage_pending();
    if (p) {
        seed_set_passphrase(p, "");
    }
    /* Back to SeedFinalizeView, which is under this dialog. */
    if (strcmp(ui_top()->name, "SeedFinalizeView") != 0) {
        SeedFinalizeView();
    }
}

void SeedAddPassphraseExitDialogView(void)
{
    const seed_t *p = storage_pending();
    const bool has = p && p->passphrase[0];
    view_spec_t v = { .status = true, .kind = NOTICE_WARNING, .name = "SeedAddPassphraseExitDialogView",
                      .title = has ? "Discard passphrase?" : "Skip passphrase?",
                      .text = has ? "Your current passphrase entry will be erased."
                                  : "You have not entered a passphrase yet.",
                      .select = exit_dialog_select, .destructive_last = has };
    v.buttons[0] = "Edit passphrase";
    v.buttons[1] = has ? "Discard passphrase" : "Skip passphrase";
    v.n_buttons = 2;
    view_push(&v);
}

static void review_select(int i)
{
    ui_pop();
    if (i == 0) {
        SeedAddPassphraseView();
    } else {
        finalize_done();
    }
}

void SeedReviewPassphraseView(void)
{
    seed_t *p = storage_pending();
    if (p == NULL) {
        return;
    }
    char with[9], without[9], saved[SEED_PASSPHRASE_MAX + 1];
    seed_fingerprint_hex(p, with);
    snprintf(saved, sizeof saved, "%s", p->passphrase);
    seed_set_passphrase(p, "");
    seed_fingerprint_hex(p, without);
    seed_set_passphrase(p, saved);
    char text[200];
    snprintf(text, sizeof text, "%s >> %s\n\"%s\"", without, with, saved);
    wally_bzero(saved, sizeof saved);
    view_spec_t v = { .name = "SeedReviewPassphraseView", .title = "Verify Passphrase", .text = text,
                      .select = review_select, .back = NULL };
    v.buttons[0] = "Edit passphrase";
    v.buttons[1] = "Done";
    v.n_buttons = 2;
    view_push(&v);
    ui_note("passphrase %s %s", without, with);
}

/* ---- Typing a mnemonic: SeedMnemonicEntryView ------------------------- */

static struct {
    MnemonicEntry e;
    size_t index;
    /* ToolsCalcFinalWord's use (is_calc_final_word): the last word is
       built rather than typed, so the entry stops one word short. */
    bool calc;
} entry;

/* The calculator's own entry for the last word, when the user chooses it
   ("Word selection entropy"): ctx 1 marks it. It shares the entry's state,
   and on the way back hands it to the word before. */
#define FINAL_WORD_ENTRY ((void *)1)

static void entry_start_word(size_t index)
{
    entry.index = index;
    mnemonic_entry_reset(&entry.e, (int)storage_mnemonic_len());
}

static void entry_enter(const screen_t *s)
{
    entry_start_word(s->ctx == FINAL_WORD_ENTRY ? storage_mnemonic_len() - 1 : 0);
}

static void entry_render(const screen_t *s)
{
    (void)s;
    char title[24];
    snprintf(title, sizeof title, "Seed Word #%u", (unsigned)entry.index + 1);
    ui_draw_title(title);
    int y = THEME.top_h + THEME.pad;
    char line[24];
    snprintf(line, sizeof line, "%s_", entry.e.prefix);
    gfx_text(THEME.pad, y, line, THEME.body, C_FG);
    const char *sug = mnemonic_entry_suggestion(&entry.e);
    if (sug && entry.e.prefix_len > 0) {
        gfx_text(GFX_W / 2, y, sug, THEME.body, C_MUTED);
    }
    y += THEME.line_h * 2;
    char label[16];
    mnemonic_entry_option_label(&entry.e, label, sizeof label);
    gfx_fill_rect(THEME.pad, y - 2, GFX_W - 2 * THEME.pad, gfx_font_height(THEME.body) + 4, C_SEL_BG);
    gfx_text_centered(0, GFX_W, y, label, THEME.body, C_SEL_FG);
}

static void entry_word_done(void)
{
    /* The word just committed is the entry's first word. */
    const int w = bip39_word_index(entry.e.words[0]);
    if (w >= 0) {
        storage_set_word(entry.index, (uint16_t)w);
    }
    ui_note("word %u %s", (unsigned)entry.index + 1, entry.e.words[0]);
    mnemonic_entry_clear(&entry.e);
    const size_t n = storage_mnemonic_len();
    if (entry.calc && entry.index + 2 >= n) {
        /* The calculator takes over: after the next-to-last word, to ask
           for the last bits of entropy; after a chosen last word, to show
           the real one. Each View stays under it for BACK. */
        entry_start_word(entry.index);
        if (entry.index + 2 == n) {
            ToolsCalcFinalWordFinalizePromptView();
        } else {
            ToolsCalcFinalWordShowFinalWordView(NULL);
        }
        return;
    }
    if (entry.index + 1 < n) {
        entry_start_word(entry.index + 1);
        return;
    }
    if (storage_mnemonic_to_pending()) {
        SeedFinalizeView();
    } else {
        SeedMnemonicInvalidView();
    }
}

static bool entry_input(const screen_t *s, input_ev_t ev)
{
    switch (ev) {
    case IN_UP:   mnemonic_entry_scroll(&entry.e, -1); return true;
    case IN_DOWN: mnemonic_entry_scroll(&entry.e, +1); return true;
    case IN_OK:
        if (mnemonic_entry_accept(&entry.e) != MNEMONIC_ENTRY_CONTINUE) {
            entry_word_done();
        }
        return true;
    case IN_BACK:
        if (mnemonic_entry_back(&entry.e)) {
            return true;
        }
        /* Nothing typed: back to the word before, or out at the first. */
        if (s->ctx == FINAL_WORD_ENTRY) {
            ui_pop();
            return true;
        }
        if (entry.index > 0) {
            entry_start_word(entry.index - 1);
            return true;
        }
        storage_discard_mnemonic();
        ui_pop();
        return true;
    default:
        return false;
    }
}

static void entry_leave(const screen_t *s)
{
    mnemonic_entry_clear(&entry.e);
    gfx_wipe();
    if (s->ctx == FINAL_WORD_ENTRY && storage_mnemonic_len() >= 2) {
        entry_start_word(storage_mnemonic_len() - 2);
    }
}

static const screen_t MNEMONIC_ENTRY = {
    .name = "SeedMnemonicEntryView", .enter = entry_enter, .render = entry_render,
    .input = entry_input, .leave = entry_leave,
};

static const screen_t MNEMONIC_ENTRY_FINAL = {
    .name = "SeedMnemonicEntryView", .enter = entry_enter, .render = entry_render,
    .input = entry_input, .leave = entry_leave, .ctx = FINAL_WORD_ENTRY,
};

void SeedMnemonicEntryView(size_t words)
{
    storage_init_mnemonic(words);
    entry.calc = false;
    ui_push(&MNEMONIC_ENTRY);
}

void SeedMnemonicEntryCalcView(size_t words)
{
    storage_init_mnemonic(words);
    entry.calc = true;
    ui_push(&MNEMONIC_ENTRY);
}

void SeedMnemonicEntryFinalWordView(void)
{
    ui_push(&MNEMONIC_ENTRY_FINAL);
}

/* SeedElectrumMnemonicStartView: a warning, then 12 words typed as an
   Electrum mnemonic. The warning has no BACK and stays under the entry, so
   BACK from the first word returns to it, as in SeedSigner. */
static void electrum_start_select(int i)
{
    (void)i;
    storage_init_electrum_mnemonic();
    entry.calc = false;
    ui_push(&MNEMONIC_ENTRY);
}

void SeedElectrumMnemonicStartView(void)
{
    const view_spec_t v = { .name = "SeedElectrumMnemonicStartView", .title = "Electrum Warning",
                            .status = true, .kind = NOTICE_WARNING,
                            .text = "Some features are disabled for Electrum seeds.",
                            .buttons = { "I understand" }, .n_buttons = 1,
                            .select = electrum_start_select, .back = no_back };
    view_push(&v);
}

static void invalid_select(int i)
{
    if (i == 0) {
        /* Review & edit: from the first word again, on top of this. */
        ui_pop();
        entry_start_word(0);
        if (strcmp(ui_top()->name, "SeedMnemonicEntryView") != 0) {
            ui_push(&MNEMONIC_ENTRY);
        }
    } else {
        storage_discard_mnemonic();
        ui_home_fresh();
    }
}

void SeedMnemonicInvalidView(void)
{
    const view_spec_t v = { .name = "SeedMnemonicInvalidView", .title = "Invalid Mnemonic!",
                            .status = true, .kind = NOTICE_DIRE,
                            .text = "Checksum failure; not a valid seed phrase.",
                            .buttons = { "Review & edit", "Discard" }, .n_buttons = 2,
                            .destructive_last = true, .select = invalid_select,
                            .back = no_back };
    view_push(&v);
}

/* ---- SeedDiscardView -------------------------------------------------- */

static void discard_select(int i)
{
    const int seed = view_arg();
    if (i == 0) {
        ui_pop();       /* Keep seed: skip_current_view back to where it was */
        return;
    }
    if (seed < 0) {
        storage_clear_pending();
    } else {
        storage_discard(seed);
    }
    ui_home_fresh();
    ui_note("discarded");
}

void SeedDiscardView(int seed)
{
    const seed_t *s = seed < 0 ? storage_pending() : storage_seed(seed);
    if (s == NULL) {
        return;
    }
    char text[64];
    snprintf(text, sizeof text, "Wipe seed %s from the device?", fingerprint(s));
    const view_spec_t v = { .name = "SeedDiscardView", .title = "Discard Seed?", .status = true,
                            .kind = NOTICE_WARNING, .text = text,
                            .buttons = { "Keep seed", "Discard" }, .n_buttons = 2,
                            .destructive_last = true, .select = discard_select, .arg = seed };
    view_push(&v);
}

/* ---- Backup: SeedBackupView, SeedWordsWarningView, SeedWordsView ------- */

static void backup_select(int i)
{
    if (i == 0) {
        SeedWordsWarningView(view_arg());
    } else {
        SeedTranscribeSeedQRFormatView(view_arg());
    }
}

void SeedBackupView(int seed)
{
    /* An Electrum seed has no SeedQR (seedqr_supported). */
    const view_spec_t v = { .name = "SeedBackupView", .title = "Backup Seed",
                            .buttons = { "View seed words", "Export as SeedQR" },
                            .n_buttons = storage_seed(seed)->electrum ? 1 : 2,
                            .select = backup_select, .arg = seed };
    view_push(&v);
}

/* The mnemonic the words screen and the backup test show: a seed's own
   words, or (bip85_data) a BIP-85 child's, which is a seed in its own
   right: internal RAM, wiped when the words screen closes. */
static struct {
    int seed;       /* -1: the pending seed */
    int page;
    bool child;
    uint32_t child_index;
    uint16_t child_words[SEED_MAX_WORDS];
    size_t child_n;
} words;

#define WORDS_PER_PAGE 4

static const seed_t *words_seed(void)
{
    return words.seed < 0 ? storage_pending() : storage_seed(words.seed);
}

static size_t shown_n(void)
{
    const seed_t *s = words_seed();
    return words.child ? words.child_n : s ? s->n_words : 0;
}

static uint16_t shown_word(size_t i)
{
    return words.child ? words.child_words[i] : words_seed()->words[i];
}

static void child_wipe(void)
{
    wally_bzero(words.child_words, sizeof words.child_words);
    words.child = false;
    words.child_n = 0;
}

static int words_pages(void)
{
    return (int)shown_n() / WORDS_PER_PAGE;
}

static void words_render(const screen_t *sc)
{
    (void)sc;
    const seed_t *s = words_seed();
    if (s == NULL) {
        return;
    }
    char title[48];
    if (words.child) {
        snprintf(title, sizeof title, "Child #%u: %d/%d", (unsigned)words.child_index, words.page + 1,
                 words_pages());
    } else {
        snprintf(title, sizeof title, "Seed Words: %d/%d", words.page + 1, words_pages());
    }
    ui_draw_title(title);
    int y = THEME.top_h + THEME.pad;
    for (int k = 0; k < WORDS_PER_PAGE; k++) {
        const int i = words.page * WORDS_PER_PAGE + k;
        char line[24];
        snprintf(line, sizeof line, "%2d: %s", i + 1, BIP39_WORDLIST[shown_word((size_t)i)]);
        gfx_text(THEME.pad + 4, y, line, THEME.body, C_FG);
        y += THEME.line_h;
    }
    const bool last = words.page == words_pages() - 1 && words.seed >= 0;
    const int by = GFX_H - THEME.row_box_h - 1;
    gfx_fill_rect(THEME.pad, by, GFX_W - 2 * THEME.pad, THEME.row_box_h, C_SEL_BG);
    gfx_text_centered(0, GFX_W, by + (THEME.row_box_h - gfx_font_height(THEME.body)) / 2,
                      last ? "Done" : "Next", THEME.body, C_SEL_FG);
}

static bool words_input(const screen_t *sc, input_ev_t ev)
{
    (void)sc;
    if (ev == IN_OK) {
        if (words.page < words_pages() - 1) {
            words.page++;
            ui_note("words page %d", words.page + 1);
        } else {
            /* Done (or Next, for a seed not loaded yet): offer the test, on
               top of the last page so BACK returns to it. */
            SeedWordsBackupTestPromptView(words.seed);
        }
        return true;
    }
    if (ev == IN_BACK && words.page > 0) {
        words.page--;       /* each page is a View in SeedSigner's back stack */
        return true;
    }
    return ev == IN_UP || ev == IN_DOWN;
}

static void words_leave(const screen_t *sc)
{
    (void)sc;
    gfx_wipe();     /* the words were on the panel */
    child_wipe();
}

static const screen_t SEED_WORDS = {
    .name = "SeedWordsView", .render = words_render, .input = words_input, .leave = words_leave,
};

void SeedWordsView(int seed)
{
    words.seed = seed;
    words.page = 0;
    ui_push(&SEED_WORDS);
}

static void words_warning_select(int i)
{
    (void)i;
    const int seed = view_arg();
    ui_pop();       /* skip_current_view: BACK never returns to the warning */
    SeedWordsView(seed);
}

static void words_warning(int seed);

void SeedWordsWarningView(int seed)
{
    child_wipe();
    words_warning(seed);
}

static void words_warning(int seed)
{
    if (!SETTINGS.dire_warnings) {
        SeedWordsView(seed);
        return;
    }
    const view_spec_t v = { .name = "SeedWordsWarningView", .status = true, .kind = NOTICE_DIRE,
                            .headline = "Classified Info!",
                            .text = "You must keep your seed words private & away from all online devices.",
                            .buttons = { "I understand" }, .n_buttons = 1,
                            .select = words_warning_select, .arg = seed };
    view_push(&v);
}

/* ---- Export xpub ------------------------------------------------------ */

static struct {
    int seed;
    btc_sig_t sig;
    script_opt_t script;
    xpub_qr_t format;
    char path[48];
    /* Only while the code is on screen: the encoder's fountain scratch is
       about 26 KB, which belongs in PSRAM rather than in internal RAM for
       the life of the device. An xpub is public; nothing here is secret. */
    qr_encoder_t *enc;
    uint8_t *buf;
} xp;

#define XPUB_QR_BUF 2048

static btc_script_t to_btc_script(script_opt_t s)
{
    switch (s) {
    case SCRIPT_NAT: return BTC_SCRIPT_P2WPKH;
    case SCRIPT_NES: return BTC_SCRIPT_P2SH_P2WPKH;
    case SCRIPT_LEG: return BTC_SCRIPT_P2PKH;
    case SCRIPT_TR:  return BTC_SCRIPT_P2TR;
    default:         return BTC_SCRIPT_INVALID;
    }
}

static void xpub_qr_leave(void)
{
    if (xp.enc) {
        qr_encoder_wipe(xp.enc);
    }
    free(xp.enc);
    free(xp.buf);
    xp.enc = NULL;
    xp.buf = NULL;
    ui_after(ui_home_fresh);    /* SeedSigner: Destination(MainMenuView) */
}

static void SeedExportXpubQRDisplayView(void)
{
    const seed_t *s = storage_seed(xp.seed);
    xp.enc = malloc(sizeof *xp.enc);
    xp.buf = malloc(XPUB_QR_BUF);
    bool ok = xp.enc && xp.buf;
    if (!ok) {
        /* fall through to the message below */
    } else if (xp.format == XPUB_QR_STATIC) {
        ok = qr_encode_xpub(xp.enc, s, xp.path, SETTINGS.network, xp.sig, xp.buf, XPUB_QR_BUF);
    } else if (xp.format == XPUB_QR_SPECTER) {
        ok = qr_encode_xpub_specter(xp.enc, s, xp.path, SETTINGS.network, xp.sig, SETTINGS.qr_density,
                                    xp.buf, XPUB_QR_BUF);
    } else {
        ok = qr_encode_xpub_ur(xp.enc, s, xp.path, SETTINGS.network, SETTINGS.qr_density,
                               xp.buf, XPUB_QR_BUF);
    }
    if (!ok) {
        free(xp.enc);
        free(xp.buf);
        xp.enc = NULL;
        xp.buf = NULL;
        not_yet("SeedExportXpubQRDisplayView for this path");
        return;
    }
    ui_note("xpub-qr %s %d parts", xp.path, (int)qr_encoder_seq_len(xp.enc));
    ui_show_qr("SeedExportXpubQRDisplayView", xp.enc, xpub_qr_leave);
}

static void details_select(int i)
{
    (void)i;
    SeedExportXpubQRDisplayView();
}

static void SeedExportXpubDetailsView(void)
{
    const char *override = seed_derivation_override(storage_seed(xp.seed), xp.sig);
    if (xp.script != SCRIPT_CUSTOM && override) {
        snprintf(xp.path, sizeof xp.path, "%s", override);
    } else if (xp.script != SCRIPT_CUSTOM &&
               !btc_standard_path(SETTINGS.network, xp.sig, to_btc_script(xp.script), xp.path, sizeof xp.path)) {
        not_yet("SeedExportXpubDetailsView for this script type");
        return;
    }
    {
        /* A typed path that does not derive: SeedSigner's get_xpub raises. */
        char probe[120];
        if (!btc_xpub(storage_seed(xp.seed)->bytes, 64, xp.path, SETTINGS.network, 0, probe, sizeof probe)) {
            UnhandledExceptionView("ValueError", "seed.py, get_xpub", xp.path);
            return;
        }
    }
    if (!SETTINGS.xpub_details) {
        SeedExportXpubQRDisplayView();
        return;
    }
    ui_working("Generating xpub...");
    char xpub[120] = "";
    btc_xpub(storage_seed(xp.seed)->bytes, 64, xp.path, SETTINGS.network,
             seed_xpub_version(storage_seed(xp.seed), xp.path, SETTINGS.network, xp.sig), xpub, sizeof xpub);
    char text[200];
    snprintf(text, sizeof text, "%s\n%s\n%.10s...%s", fingerprint(storage_seed(xp.seed)), xp.path,
             xpub, xpub + (strlen(xpub) > 8 ? strlen(xpub) - 8 : 0));
    const view_spec_t v = { .name = "SeedExportXpubDetailsView", .title = "Xpub Details", .text = text,
                            .buttons = { "Export Xpub" }, .n_buttons = 1, .select = details_select };
    view_push(&v);
    ui_note("xpub %s %s", xp.path, xpub);
}

static void xpub_warning_select(int i)
{
    (void)i;
    ui_pop();       /* skip_current_view */
    SeedExportXpubDetailsView();
}

static void SeedExportXpubWarningView(void)
{
    if (!SETTINGS.privacy_warnings) {
        SeedExportXpubDetailsView();
        return;
    }
    const view_spec_t v = { .name = "SeedExportXpubWarningView", .status = true, .kind = NOTICE_WARNING,
                            .headline = "Privacy Leak!",
                            .text = "Xpub can be used to view all future transactions.",
                            .buttons = { "I understand" }, .n_buttons = 1,
                            .select = xpub_warning_select };
    view_push(&v);
}

static xpub_qr_t format_map[XPUB_QR_COUNT];

static void qr_format_select(int i)
{
    xp.format = format_map[i];
    SeedExportXpubWarningView();
}

static void SeedExportXpubQRFormatView(void)
{
    view_spec_t v = { .name = "SeedExportXpubQRFormatView", .title = "Xpub QR Format",
                      .select = qr_format_select };
    for (int f = 0; f < XPUB_QR_COUNT; f++) {
        if (SETTINGS.xpub_qr_format[f]) {
            format_map[v.n_buttons] = (xpub_qr_t)f;
            v.buttons[v.n_buttons++] = xpub_qr_format_name((xpub_qr_t)f);
        }
    }
    if (v.n_buttons == 1) {
        xp.format = format_map[0];
        SeedExportXpubWarningView();
        return;
    }
    view_push(&v);
}

static script_opt_t script_map[SCRIPT_COUNT];

/* SeedExportXpubCustomDerivationView's result: the path typed. */
static void custom_done(const char *path)
{
    snprintf(xp.path, sizeof xp.path, "%s", path);
    ui_note("custom %s", xp.path);
    SeedExportXpubQRFormatView();
}

/* SeedExportXpubCustomDerivationView: a path typed on SeedSigner's keys,
   from "m/". */
void SeedExportXpubCustomDerivationView(void (*done)(const char *path))
{
    const keypad_spec_t k = { "SeedExportXpubCustomDerivationView", "Derivation Path", "/'0123456789", NULL, 0,
                              done, "m/" };
    keypad_open(&k);
}

static void script_select(int i)
{
    xp.script = script_map[i];
    if (xp.script == SCRIPT_CUSTOM) {
        SeedExportXpubCustomDerivationView(custom_done);
        return;
    }
    SeedExportXpubQRFormatView();
}

static void SeedExportXpubScriptTypeView(void)
{
    view_spec_t v = { .name = "SeedExportXpubScriptTypeView", .title = "Export Xpub",
                      .select = script_select };
    /* An Electrum seed allows native segwit only (script_override). */
    const bool electrum = storage_seed(xp.seed)->electrum;
    for (int t = 0; t < SCRIPT_COUNT; t++) {
        if (electrum ? t == SCRIPT_NAT : SETTINGS.script_types[t]) {
            script_map[v.n_buttons] = (script_opt_t)t;
            v.buttons[v.n_buttons++] = script_type_name((script_opt_t)t);
        }
    }
    if (v.n_buttons == 1) {
        xp.script = script_map[0];
        SeedExportXpubQRFormatView();
        return;
    }
    view_push(&v);
}

static void sig_select(int i)
{
    xp.sig = i == 0 ? BTC_SIG_SINGLE : BTC_SIG_MULTI;
    SeedExportXpubScriptTypeView();
}

void SeedExportXpubSigTypeView(int seed)
{
    xp.seed = seed;
    if (SETTINGS.sig_single != SETTINGS.sig_multi) {
        xp.sig = SETTINGS.sig_single ? BTC_SIG_SINGLE : BTC_SIG_MULTI;
        SeedExportXpubScriptTypeView();
        return;
    }
    const view_spec_t v = { .name = "SeedExportXpubSigTypeView", .title = "Export Xpub",
                            .buttons = { "Single Sig", "Multisig" }, .n_buttons = 2,
                            .select = sig_select };
    view_push(&v);
}

MnemonicEntry *views_entry_state(void)
{
    return &entry.e;
}

TextEntry *views_passphrase_state(void)
{
    return &passphrase_entry;
}

/* ---- The backup test: SeedWordsBackupTest{Prompt,,Mistake,Success}View -- */

/* Which words have been confirmed, and the one asked. The words themselves
   are read from the seed when needed; nothing here is secret. */
static struct {
    int seed;                       /* -1: the pending seed */
    bool confirmed[SEED_MAX_WORDS];
    int n_confirmed;
    int cur;                        /* the word asked, 0-based */
    char labels[4][16];
} bt;

static const seed_t *bt_seed(void)
{
    return bt.seed < 0 ? storage_pending() : storage_seed(bt.seed);
}

static uint32_t rand_below(uint32_t n)
{
    uint32_t r = 0;
    btc_random(&r, sizeof r);
    return n ? r % n : 0;
}

static void SeedWordsBackupTestView(int cur);

static void after_backup(void)
{
    /* A seed not loaded yet goes on to be finalised; a loaded one back to
       its options. */
    if (bt.seed < 0) {
        SeedFinalizeView();
    } else {
        view_clear_history();
        SeedOptionsView(bt.seed);
    }
}

static void success_select(int i)
{
    (void)i;
    after_backup();
}

static void SeedWordsBackupTestSuccessView(void)
{
    const view_spec_t v = { .name = "SeedWordsBackupTestSuccessView", .status = true,
                            .kind = NOTICE_SUCCESS, .title = "Backup Verified", .headline = "Success!",
                            .text = "All mnemonic backup words were successfully verified!",
                            .buttons = { "OK" }, .n_buttons = 1, .select = success_select,
                            .back = no_back };
    view_replace(&v);
    ui_note("backup verified");
}

static void mistake_select(int i)
{
    if (i == 0) {
        /* Review seed words: the words again, from the first page. The words
           screen is under this one (the test replaced the prompt above it). */
        ui_pop();
        if (strcmp(ui_top()->name, "SeedWordsView") == 0) {
            words.seed = bt.seed;
            words.page = 0;
        } else {
            SeedWordsView(bt.seed);
        }
    } else {
        SeedWordsBackupTestView(bt.cur);
    }
}

static void SeedWordsBackupTestMistakeView(const char *wrong)
{
    char text[64];
    snprintf(text, sizeof text, "Word #%d is not \"%s\"!", bt.cur + 1, wrong);
    const view_spec_t v = { .name = "SeedWordsBackupTestMistakeView", .status = true, .kind = NOTICE_DIRE,
                            .title = "Verification Error", .headline = "Wrong Word!", .text = text,
                            .buttons = { "Review seed words", "Try again" }, .n_buttons = 2,
                            .select = mistake_select, .back = no_back };
    view_replace(&v);
}

static void backup_test_select(int i)
{
    const seed_t *s = bt_seed();
    if (s == NULL) {
        return;
    }
    /* Any button reading the real word is right, as SeedSigner compares
       the options' labels. */
    if (strcmp(bt.labels[i], BIP39_WORDLIST[shown_word((size_t)bt.cur)]) == 0) {
        if (!bt.confirmed[bt.cur]) {
            bt.confirmed[bt.cur] = true;
            bt.n_confirmed++;
        }
        if (bt.n_confirmed == (int)shown_n()) {
            SeedWordsBackupTestSuccessView();
        } else {
            SeedWordsBackupTestView(-1);
        }
        return;
    }
    char wrong[16];
    snprintf(wrong, sizeof wrong, "%s", bt.labels[i]);
    SeedWordsBackupTestMistakeView(wrong);
}

/* Ask for word `cur`, or (-1) a random word not yet confirmed: the pick is
   among those left, so it never has to retry. */
static void SeedWordsBackupTestView(int cur)
{
    const seed_t *s = bt_seed();
    if (s == NULL) {
        return;
    }
    if (cur < 0) {
        uint32_t k = rand_below((uint32_t)(shown_n() - (size_t)bt.n_confirmed));
        for (cur = 0; cur < (int)shown_n(); cur++) {
            if (!bt.confirmed[cur] && k-- == 0) {
                break;
            }
        }
    }
    bt.cur = cur;
    /* The real word and three random ones, in a random order. */
    snprintf(bt.labels[0], sizeof bt.labels[0], "%s", BIP39_WORDLIST[shown_word((size_t)cur)]);
    for (int k = 1; k < 4; k++) {
        snprintf(bt.labels[k], sizeof bt.labels[k], "%s", BIP39_WORDLIST[rand_below(2047)]);
    }
    for (int k = 3; k > 0; k--) {
        const uint32_t j = rand_below((uint32_t)k + 1);
        char tmp[16];
        memcpy(tmp, bt.labels[k], sizeof tmp);
        memcpy(bt.labels[k], bt.labels[j], sizeof tmp);
        memcpy(bt.labels[j], tmp, sizeof tmp);
    }
    char title[24];
    snprintf(title, sizeof title, "Verify Word #%d", cur + 1);
    const view_spec_t v = { .name = "SeedWordsBackupTestView", .title = title,
                            .buttons = { bt.labels[0], bt.labels[1], bt.labels[2], bt.labels[3] },
                            .n_buttons = 4, .select = backup_test_select, .back = no_back };
    view_replace(&v);
    ui_note("verify word %d", cur + 1);
}

static void prompt_select(int i)
{
    if (i == 0) {
        memset(bt.confirmed, 0, sizeof bt.confirmed);
        bt.n_confirmed = 0;
        SeedWordsBackupTestView(-1);
    } else {
        after_backup();
    }
}

void SeedWordsBackupTestPromptView(int seed)
{
    bt.seed = seed;
    const view_spec_t v = { .name = "SeedWordsBackupTestPromptView", .title = "Verify Backup?",
                            .text = "Optionally verify that your mnemonic backup is correct.",
                            .buttons = { "Verify", "Skip" }, .n_buttons = 2, .select = prompt_select,
                            .back = no_back };
    view_push(&v);
}

/* ---- Export as SeedQR: SeedTranscribeSeedQR*View ------------------------- */

/* The SeedQR being transcribed. Its modules ARE the seed: internal RAM
   (static), wiped with the panel when the transcription screens close.
   Nothing about it goes in a note but its size and the zone shown. */
static struct {
    int seed;
    bool compact;
    QRCode code;
    uint8_t modules[128];           /* version 3 (29x29) needs 106 bytes */
    int per_zone;                   /* 5, or 7 for 21x21 */
    int zones;                      /* per side */
    int zone;                       /* row * zones + column */
} tq;

static void tq_wipe(void)
{
    wally_bzero(tq.modules, sizeof tq.modules);
    wally_bzero(&tq.code, sizeof tq.code);
}

/* Build the QR SeedSigner's SeedQrEncoder or CompactSeedQrEncoder makes. */
static bool tq_build(void)
{
    const seed_t *s = storage_seed(tq.seed);
    if (s == NULL) {
        return false;
    }
    uint8_t data[100];      /* 96 digits and the NUL for a standard 24-word SeedQR */
    size_t len;
    if (tq.compact) {
        len = seedqr_encode_compact(s, data);
    } else {
        len = seedqr_encode_standard(s, (char *)data, sizeof data) ? strlen((char *)data) : 0;
    }
    const bool ok = len > 0 &&
                    qrcode_fit(&tq.code, tq.modules, 3, ECC_LOW, data, (uint16_t)len) > 0;
    wally_bzero(data, sizeof data);
    if (!ok) {
        tq_wipe();
        return false;
    }
    tq.per_zone = tq.code.size == 21 ? 7 : 5;
    tq.zones = (tq.code.size + tq.per_zone - 1) / tq.per_zone;
    return true;
}

static void tq_leave(const screen_t *s)
{
    (void)s;
    gfx_wipe();     /* the seed was on the panel */
}

/* Draw `n` x `n` modules from (mx, my) at `px` pixels each, from (x, y),
   with grid lines every module (zoomed) or every zone (whole). */
static void tq_draw(int x, int y, int mx, int my, int n, int px, int grid)
{
    gfx_fill_rect(x - 1, y - 1, n * px + 2, n * px + 2, C_QR_LIGHT);
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) {
            const int qx = mx + c, qy = my + r;
            if (qx < tq.code.size && qy < tq.code.size && qrcode_getModule(&tq.code, (uint8_t)qx, (uint8_t)qy)) {
                gfx_fill_rect(x + c * px, y + r * px, px, px, C_BG);
            }
        }
    }
    if (grid > 0 && px >= 3) {
        for (int k = grid; k < n; k += grid) {
            for (int t = 0; t < n * px; t += 2) {
                gfx_pixel(x + k * px, y + t, C_ACCENT);
                gfx_pixel(x + t, y + k * px, C_ACCENT);
            }
        }
    }
}

static void zone_label(int zone, char out[16])
{
    snprintf(out, 16, "%c-%d", 'A' + zone / tq.zones, zone % tq.zones + 1);
}

/* SeedTranscribeSeedQRConfirmQRPromptView */
static void confirm_prompt_select(int i)
{
    if (i == 0) {
        ui_push(&SCREEN_SCAN_CONFIRM_SEEDQR);
    } else {
        view_clear_history();
        SeedOptionsView(tq.seed);
    }
}

static void SeedTranscribeSeedQRConfirmQRPromptView(void)
{
    const view_spec_t v = { .name = "SeedTranscribeSeedQRConfirmQRPromptView", .title = "Confirm SeedQR?",
                            .text = "Optionally scan your transcribed SeedQR to confirm that it reads "
                                    "back correctly.",
                            .buttons = { "Confirm SeedQR", "Done" }, .n_buttons = 2,
                            .select = confirm_prompt_select };
    view_push(&v);
}

/* SeedTranscribeSeedQRZoomedInView: one zone at a time, labelled as on
   SeedSigner's printed templates ("B-3"). UP and DOWN step through the
   zones in reading order; OK is done. */
static void zoomed_render(const screen_t *s)
{
    (void)s;
    char label[16], title[32];
    zone_label(tq.zone, label);
    snprintf(title, sizeof title, "Zone %s", label);
    ui_draw_title(title);
    const int top = THEME.top_h + 2;
    const int avail = GFX_H - top - 2;
    int px = avail / tq.per_zone;
    if (px * tq.per_zone > GFX_W - 4) px = (GFX_W - 4) / tq.per_zone;
    const int side = px * tq.per_zone;
    const int x = (GFX_W - side) / 2, y = top + (avail - side) / 2;
    tq_draw(x, y, (tq.zone % tq.zones) * tq.per_zone, (tq.zone / tq.zones) * tq.per_zone,
            tq.per_zone, px, 1);
}

static bool zoomed_input(const screen_t *s, input_ev_t ev)
{
    (void)s;
    const int n = tq.zones * tq.zones;
    if (ev == IN_DOWN || ev == IN_UP) {
        tq.zone = (tq.zone + (ev == IN_DOWN ? 1 : n - 1)) % n;
        char label[16];
        zone_label(tq.zone, label);
        ui_note("zone %s", label);
        return true;
    }
    if (ev == IN_OK) {
        SeedTranscribeSeedQRConfirmQRPromptView();
        return true;
    }
    return false;
}

/* Back on it after a failed confirmation: SeedSigner runs the View again,
   from the first zone. */
static void zoomed_resume(const screen_t *s)
{
    (void)s;
    tq.zone = 0;
}

static const screen_t ZOOMED = {
    .name = "SeedTranscribeSeedQRZoomedInView", .render = zoomed_render, .input = zoomed_input,
    .resume = zoomed_resume, .leave = tq_leave,
};

/* SeedTranscribeSeedQRWholeQRView: the whole code, as large as fits, and
   the way on. SeedSigner lays light grey zone lines over it; on a one-bit
   panel any line changes modules, and the code on screen must scan as the
   SeedQR, so the zones are shown in the zoomed-in view only. */
static void whole_render(const screen_t *s)
{
    (void)s;
    ui_draw_title("Transcribe SeedQR");
    const int top = THEME.top_h + 2;
    const int avail = GFX_H - top - 2;
    char begin[24];
    snprintf(begin, sizeof begin, "Begin %dx%d", tq.code.size, tq.code.size);
    /* A one-module quiet zone, as a reader (and the user's own check scan)
       needs. */
    const int span = tq.code.size + 2;
    int px = avail / span;
    const bool beside = px >= 1 && GFX_W - px * span - 6 >= gfx_text_width("Begin", THEME.body);
    if (!beside) {
        px = (avail - THEME.row_box_h - 2) / span;
    }
    if (px < 1) px = 1;
    const int side = px * span;
    if (beside) {
        gfx_fill_rect(1, top + (avail - side) / 2, side, side, C_QR_LIGHT);
        tq_draw(1 + px, top + (avail - side) / 2 + px, 0, 0, tq.code.size, px, 0);
        const int bx = side + 5, by = top + (avail - THEME.row_box_h) / 2;
        gfx_fill_rect(bx, by, GFX_W - bx - 1, THEME.row_box_h, C_SEL_BG);
        gfx_text(bx + 2, by + (THEME.row_box_h - gfx_font_height(THEME.body)) / 2,
                 gfx_text_width(begin, THEME.body) <= GFX_W - bx - 4 ? begin : "Begin",
                 THEME.body, C_SEL_FG);
    } else {
        gfx_fill_rect((GFX_W - side) / 2, top, side, side, C_QR_LIGHT);
        tq_draw((GFX_W - side) / 2 + px, top + px, 0, 0, tq.code.size, px, 0);
        const int by = GFX_H - THEME.row_box_h - 1;
        gfx_fill_rect(THEME.pad, by, GFX_W - 2 * THEME.pad, THEME.row_box_h, C_SEL_BG);
        gfx_text_centered(0, GFX_W, by + (THEME.row_box_h - gfx_font_height(THEME.body)) / 2,
                          begin, THEME.body, C_SEL_FG);
    }
}

static bool whole_input(const screen_t *s, input_ev_t ev)
{
    (void)s;
    if (ev == IN_OK) {
        tq.zone = 0;
        ui_push(&ZOOMED);
        return true;
    }
    return ev == IN_UP || ev == IN_DOWN;
}

static void whole_leave(const screen_t *s)
{
    tq_leave(s);
    tq_wipe();      /* leaving the transcription altogether */
}

static const screen_t WHOLE = {
    .name = "SeedTranscribeSeedQRWholeQRView", .render = whole_render, .input = whole_input,
    .leave = whole_leave,
};

static void SeedTranscribeSeedQRWholeQRView(bool replace)
{
    if (!tq_build()) {
        return;
    }
    ui_note("seedqr %dx%d", tq.code.size, tq.code.size);
    if (replace) {
        ui_replace(&WHOLE);
    } else {
        ui_push(&WHOLE);
    }
}

static void seedqr_warning_select(int i)
{
    (void)i;
    SeedTranscribeSeedQRWholeQRView(true);     /* skip_current_view */
}

static void SeedTranscribeSeedQRWarningView(void)
{
    if (!SETTINGS.dire_warnings) {
        SeedTranscribeSeedQRWholeQRView(false);
        return;
    }
    const view_spec_t v = { .name = "SeedTranscribeSeedQRWarningView", .status = true, .kind = NOTICE_DIRE,
                            .title = "Caution", .headline = "SeedQR is your private key!",
                            .text = "Never photograph or scan it into a device that connects to the "
                                    "internet.",
                            .buttons = { "I understand" }, .n_buttons = 1,
                            .select = seedqr_warning_select };
    view_push(&v);
}

static void seedqr_format_select(int i)
{
    tq.compact = i == 1;
    SeedTranscribeSeedQRWarningView();
}

void SeedTranscribeSeedQRFormatView(int seed)
{
    const seed_t *s = storage_seed(seed);
    if (s == NULL) {
        return;
    }
    tq.seed = seed;
    tq.compact = false;
    if (!SETTINGS.compact_seedqr) {
        /* Only standard SeedQR. SeedSigner passes the 12-word size here
           whatever the length, so labels a 24-word code "25x25"; this
           port gives its real size, 29x29. */
        SeedTranscribeSeedQRWarningView();
        return;
    }
    const bool twelve = s->n_words == 12;
    const view_spec_t v = { .name = "SeedTranscribeSeedQRFormatView", .title = "SeedQR Format",
                            .text = "Standard: BIP-39 wordlist indices\nCompact: raw entropy bits",
                            .buttons = { twelve ? "Standard: 25x25" : "Standard: 29x29",
                                         twelve ? "Compact: 21x21" : "Compact: 25x25" },
                            .n_buttons = 2, .select = seedqr_format_select };
    view_push(&v);
}

/* ---- Confirming the transcription: the scan and its three outcomes ------ */

static void review_seedqr_select(int i)
{
    (void)i;
    /* Back to the zoomed-in transcription, past the prompt. */
    ui_pop();
    if (strcmp(ui_top()->name, "SeedTranscribeSeedQRConfirmQRPromptView") == 0) {
        ui_pop();
    }
}

static void confirm_success_select(int i)
{
    (void)i;
    view_clear_history();
    SeedOptionsView(tq.seed);
}

void transcribe_confirm_scanned(const uint16_t *words, size_t n)
{
    const seed_t *s = storage_seed(tq.seed);
    if (n == 0 || s == NULL) {
        /* Not a SeedQR at all, or one that could not be read. */
        const view_spec_t v = { .name = "SeedTranscribeSeedQRConfirmInvalidQRView", .status = true,
                                .kind = NOTICE_DIRE, .title = "Confirm SeedQR", .headline = "Error!",
                                .text = "Your transcribed SeedQR could not be read!",
                                .buttons = { "Review SeedQR" }, .n_buttons = 1,
                                .select = review_seedqr_select, .back = no_back };
        view_replace(&v);
        return;
    }
    bool same = n == s->n_words;
    for (size_t i = 0; same && i < n; i++) {
        same = words[i] == s->words[i];
    }
    if (!same) {
        const view_spec_t v = { .name = "SeedTranscribeSeedQRConfirmWrongSeedView", .status = true,
                                .kind = NOTICE_DIRE, .title = "Confirm SeedQR", .headline = "Error!",
                                .text = "Your transcribed SeedQR does not match your original seed!",
                                .buttons = { "Review SeedQR" }, .n_buttons = 1,
                                .select = review_seedqr_select, .back = no_back };
        view_replace(&v);
        return;
    }
    const view_spec_t v = { .name = "SeedTranscribeSeedQRConfirmSuccessView", .status = true,
                            .kind = NOTICE_SUCCESS, .title = "Confirm SeedQR", .headline = "Success!",
                            .text = "Your transcribed SeedQR successfully scanned and yielded the same seed.",
                            .buttons = { "OK" }, .n_buttons = 1, .select = confirm_success_select,
                            .back = no_back };
    view_push(&v);
}

/* ---- BIP-85 child seeds -------------------------------------------------- */

static struct {
    int seed;
    size_t n_words;
} b85;

static void bip85_index_done(const char *text);

static void open_index_keypad(void)
{
    const keypad_spec_t k = { "SeedBIP85SelectChildIndexView", "BIP-85 Index", "0123456789", NULL, 0,
                              bip85_index_done, NULL };
    keypad_open(&k);
}

static void invalid_index_select(int i)
{
    (void)i;
    ui_pop();               /* skip_current_view */
    open_index_keypad();
}

static void SeedBIP85InvalidChildIndexView(void)
{
    const view_spec_t v = { .name = "SeedBIP85InvalidChildIndexView", .status = true, .kind = NOTICE_DIRE,
                            .title = "BIP-85 Index Error", .headline = "Invalid Child Index",
                            .text = "BIP-85 Child Index must be between 0 and 2^31-1.",
                            .buttons = { "Try again" }, .n_buttons = 1,
                            .select = invalid_index_select, .back = no_back };
    view_replace(&v);       /* in the keypad's place, as skip_current_view */
}

/* SeedBIP85SelectChildIndexView's result. SeedSigner raises on an empty
   entry (int("")); here it is an invalid index like any other. */
static void bip85_index_done(const char *text)
{
    const size_t len = strlen(text);
    unsigned long long v = 0;
    for (size_t i = 0; i < len && v < (1ull << 31); i++) {
        v = v * 10 + (unsigned)(text[i] - '0');
    }
    if (len == 0 || v >= (1ull << 31)) {
        ui_note("bip85 invalid");
        SeedBIP85InvalidChildIndexView();
        return;
    }
    const seed_t *s = storage_seed(b85.seed);
    size_t n = 0;
    child_wipe();
    if (s == NULL || !bip85_child(s, (uint32_t)v, b85.n_words, words.child_words, &n)) {
        return;
    }
    words.child = true;
    words.child_index = (uint32_t)v;
    words.child_n = n;
    ui_note("bip85 child %u %u", (unsigned)v, (unsigned)n);
    /* Destination(SeedWordsWarningView, bip85_data): the keypad stays under it. */
    words_warning(b85.seed);
}

static void bip85_words_select(int i)
{
    b85.seed = view_arg();
    b85.n_words = i == 0 ? 12 : 24;
    open_index_keypad();
}

void SeedBIP85SelectNumWordsView(int seed)
{
    const view_spec_t v = { .name = "SeedBIP85SelectNumWordsView", .title = "BIP-85 Num Words",
                            .buttons = { "12 Words", "24 Words" }, .n_buttons = 2,
                            .select = bip85_words_select, .arg = seed };
    view_push(&v);
}

/* For the flow tests: word `i` of what the words screen shows. */
const char *views_shown_word(size_t i)
{
    return i < shown_n() ? BIP39_WORDLIST[shown_word(i)] : NULL;
}
