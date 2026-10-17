/*
 * SeedSigner's seed views (views/seed_views.py), ported view by view: the
 * seeds menu, loading a seed by scan or by typing it, finalising it with an
 * optional BIP-39 passphrase, its options, discarding it, backing it up as
 * words, and exporting its xpub. Each function or screen is named after the
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
#include "views-psbt.h"
#include "views.h"
#include "wally_core.h"

/* ---- Helpers ---------------------------------------------------------- */

static char fp_buf[9];

static const char *fingerprint(const seed_t *s)
{
    seed_fingerprint_hex(s, fp_buf);
    return fp_buf;
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
    case OPT_EXPLORER:      not_yet("ToolsAddressExplorerAddressTypeView"); break;
    case OPT_BACKUP:        SeedBackupView(seed); break;
    case OPT_SIGN_MESSAGE:  not_yet("SeedSignMessageStartView"); break;
    case OPT_BIP85:         not_yet("SeedBIP85SelectNumWordsView"); break;
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
    if (SETTINGS.bip85_child_seeds) {
        v.buttons[n] = "BIP-85 child seed"; options_map[n++] = OPT_BIP85;
    }
    v.buttons[n] = "Discard seed";     options_map[n++] = OPT_DISCARD;
    v.n_buttons = n;
    if (fresh) {
        view_clear_history();   /* clear_history=True */
    }
    /* A seed loaded for a PSBT goes back to it (resume_main_flow). */
    if (psbt_flow_resume(seed)) {
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
        v.buttons[v.n_buttons++] = "BIP-39 Passphrase";
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
    ui_draw_title("BIP-39 Passphrase");
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
    const bool empty = passphrase_entry.length == 0;
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
} entry;

static void entry_start_word(size_t index)
{
    entry.index = index;
    mnemonic_entry_reset(&entry.e, (int)storage_mnemonic_len());
}

static void entry_enter(const screen_t *s)
{
    (void)s;
    entry_start_word(0);
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
    if (entry.index + 1 < storage_mnemonic_len()) {
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
    (void)s;
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
    (void)s;
    mnemonic_entry_clear(&entry.e);
    gfx_wipe();
}

static const screen_t MNEMONIC_ENTRY = {
    .name = "SeedMnemonicEntryView", .enter = entry_enter, .render = entry_render,
    .input = entry_input, .leave = entry_leave,
};

void SeedMnemonicEntryView(size_t words)
{
    storage_init_mnemonic(words);
    ui_push(&MNEMONIC_ENTRY);
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
        not_yet("SeedTranscribeSeedQRFormatView");
    }
}

void SeedBackupView(int seed)
{
    const view_spec_t v = { .name = "SeedBackupView", .title = "Backup Seed",
                            .buttons = { "View seed words", "Export as SeedQR" }, .n_buttons = 2,
                            .select = backup_select, .arg = seed };
    view_push(&v);
}

static struct {
    int seed;       /* -1: the pending seed */
    int page;
} words;

#define WORDS_PER_PAGE 4

static const seed_t *words_seed(void)
{
    return words.seed < 0 ? storage_pending() : storage_seed(words.seed);
}

static int words_pages(void)
{
    const seed_t *s = words_seed();
    return s ? s->n_words / WORDS_PER_PAGE : 0;
}

static void words_render(const screen_t *sc)
{
    (void)sc;
    const seed_t *s = words_seed();
    if (s == NULL) {
        return;
    }
    char title[32];
    snprintf(title, sizeof title, "Seed Words: %d/%d", words.page + 1, words_pages());
    ui_draw_title(title);
    int y = THEME.top_h + THEME.pad;
    for (int k = 0; k < WORDS_PER_PAGE; k++) {
        const int i = words.page * WORDS_PER_PAGE + k;
        char line[24];
        snprintf(line, sizeof line, "%2d: %s", i + 1, BIP39_WORDLIST[s->words[i]]);
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
            ui_pop();
            not_yet("SeedWordsBackupTestPromptView");
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

void SeedWordsWarningView(int seed)
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
        ok = qr_encode_xpub(xp.enc, s, xp.path, SETTINGS.network, xp.buf, XPUB_QR_BUF);
    } else if (xp.format == XPUB_QR_SPECTER) {
        ok = qr_encode_xpub_specter(xp.enc, s, xp.path, SETTINGS.network, SETTINGS.qr_density,
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
    if (!btc_standard_path(SETTINGS.network, xp.sig, to_btc_script(xp.script), xp.path, sizeof xp.path)) {
        not_yet("SeedExportXpubDetailsView for this script type");
        return;
    }
    if (!SETTINGS.xpub_details) {
        SeedExportXpubQRDisplayView();
        return;
    }
    ui_working("Generating xpub...");
    char xpub[120] = "";
    btc_xpub(storage_seed(xp.seed)->bytes, 64, xp.path, SETTINGS.network,
             btc_slip132_version(xp.path, SETTINGS.network), xpub, sizeof xpub);
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

static void script_select(int i)
{
    xp.script = script_map[i];
    if (xp.script == SCRIPT_CUSTOM) {
        not_yet("SeedExportXpubCustomDerivationView");
        return;
    }
    SeedExportXpubQRFormatView();
}

static void SeedExportXpubScriptTypeView(void)
{
    view_spec_t v = { .name = "SeedExportXpubScriptTypeView", .title = "Export Xpub",
                      .select = script_select };
    for (int t = 0; t < SCRIPT_COUNT; t++) {
        if (SETTINGS.script_types[t]) {
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
