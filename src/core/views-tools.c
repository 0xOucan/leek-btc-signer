/*
 * SeedSigner's Tools views (views/tools_views.py), ported view by view: a new
 * seed from dice rolls, and the final-word calculator (type the first 11 or
 * 23 words, then build the last from coin flips, a chosen word, or zeros,
 * with its checksum calculated). Each function or screen is named after the
 * View it ports and routes where that View's Destination goes.
 *
 * SeedSigner's dice and coin-flip keyboards become one keypad screen for the
 * encoder: UP and DOWN choose a face (or DEL), OK enters it. The rolls and
 * flips are entropy, so they live in internal RAM and are wiped when the
 * keypad closes; notes carry only fingerprints.
 *
 * The arithmetic is btc.c's port of SeedSigner's mnemonic_generation.py.
 *
 * The address explorer: choose a seed (or load one, which resumes the
 * explorer as SeedSigner's resume_main_flow does), a script type, receive
 * or change, then pages of ten addresses, each shown as a QR on request.
 * Wallet descriptors (multisig) wait for T47.
 */
#include "views-tools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "btc.h"
#include "qr-encode.h"
#include "screens.h"
#include "settings.h"
#include "secmem.h"
#include "seed-storage.h"
#include "theme.h"
#include "views-seed.h"
#include "views.h"
#include "wally_core.h"

static void not_yet(const char *view)
{
    char text[96];
    snprintf(text, sizeof text, "%s is not ported yet.", view);
    const view_spec_t v = { .name = "NotYetImplementedView", .title = "Work In Progress",
                            .text = text, .status = true, .kind = NOTICE_INFO,
                            .buttons = { "OK" }, .n_buttons = 1 };
    view_push(&v);
}

#define DICE_ROLLS_12 50    /* DICE__NUM_ROLLS__12WORD */
#define DICE_ROLLS_24 99    /* DICE__NUM_ROLLS__24WORD */

static void seed_fingerprint_of(const uint16_t *idx, size_t n, char fp[9])
{
    seed_t *s = sec_alloc(sizeof *s);
    snprintf(fp, 9, "?");
    if (s && seed_from_indices(s, idx, n, "") == SEED_OK) {
        seed_fingerprint_hex(s, fp);
    }
    if (s) {
        seed_clear(s);
        sec_free(s, sizeof *s);
    }
}

/* ---- The keypad: ToolsDiceEntropyEntryScreen, ToolsCoinFlipEntryScreen -- */

#define PAD_MAX 100

static struct {
    const char *name;           /* the View it stands for */
    const char *title;          /* "Dice Roll" or "Coin Flip" */
    const char *keys;           /* "123456" or "10" */
    const char *hint;           /* a line under the keys, or NULL */
    size_t total;
    char text[PAD_MAX + 1];
    size_t len;
    int sel;                    /* a key, or strlen(keys) for DEL */
    void (*done)(const char *text);
} pad;

static int pad_keys(void)
{
    return (int)strlen(pad.keys);
}

static void pad_render(const screen_t *s)
{
    (void)s;
    char title[32];
    const size_t at = pad.len < pad.total ? pad.len + 1 : pad.total;
    snprintf(title, sizeof title, "%s %u/%u", pad.title, (unsigned)at, (unsigned)pad.total);
    ui_draw_title(title);
    int y = THEME.top_h + THEME.pad;
    /* The entry so far: the tail that fits. */
    const int fit = gfx_text_fit(pad.text, THEME.body, GFX_W - 2 * THEME.pad - 6);
    const char *tail = pad.text + (pad.len > (size_t)fit ? pad.len - (size_t)fit : 0);
    const int x = gfx_text(THEME.pad, y, tail, THEME.body, C_FG);
    gfx_text(x, y, "_", THEME.body, C_ACCENT);
    y += THEME.line_h * 2;
    /* The keys in a row, DEL last. */
    const int n = pad_keys() + 1;
    const int w = (GFX_W - 2 * THEME.pad) / n;
    for (int k = 0; k < n; k++) {
        char label[4];
        snprintf(label, sizeof label, "%s", k < n - 1 ? (char[]){ pad.keys[k], 0 } : "DEL");
        const int kx = THEME.pad + k * w;
        if (k == pad.sel) {
            gfx_fill_rect(kx, y - 2, w - 1, gfx_font_height(THEME.body) + 4, C_SEL_BG);
        }
        gfx_text_centered(kx, w - 1, y, label, THEME.body, k == pad.sel ? C_SEL_FG : C_FG);
    }
    if (pad.hint && y + THEME.line_h * 2 + gfx_font_height(THEME.body) <= GFX_H) {
        ui_draw_wrapped(y + THEME.line_h * 2, pad.hint, C_MUTED);
    }
}

static bool pad_input(const screen_t *s, input_ev_t ev)
{
    (void)s;
    const int n = pad_keys() + 1;
    switch (ev) {
    case IN_UP:   pad.sel = (pad.sel + n - 1) % n; return true;
    case IN_DOWN: pad.sel = (pad.sel + 1) % n; return true;
    case IN_OK:
        if (pad.sel == n - 1) {
            if (pad.len > 0) {
                pad.text[--pad.len] = '\0';
            }
            return true;
        }
        if (pad.len < pad.total && pad.len < PAD_MAX) {
            pad.text[pad.len++] = pad.keys[pad.sel];
            pad.text[pad.len] = '\0';
        }
        if (pad.len == pad.total) {
            pad.done(pad.text);
        }
        return true;
    default:
        return false;       /* BACK leaves, as SeedSigner's back arrow */
    }
}

static void pad_clear(void)
{
    wally_bzero(pad.text, sizeof pad.text);
    pad.len = 0;
}

/* Back on the keypad from the view it finished into: a new entry, as
   SeedSigner runs the View again. */
static void pad_resume(const screen_t *s)
{
    (void)s;
    pad_clear();
}

static void pad_leave(const screen_t *s)
{
    (void)s;
    pad_clear();
    gfx_wipe();
}

static screen_t PAD = {
    .render = pad_render, .input = pad_input, .resume = pad_resume, .leave = pad_leave,
};

static void pad_open(const char *name, const char *title, const char *keys, const char *hint,
                     size_t total, void (*done)(const char *))
{
    pad_clear();
    pad.name = name;
    pad.title = title;
    pad.keys = keys;
    pad.hint = hint;
    pad.total = total < PAD_MAX ? total : PAD_MAX;
    pad.sel = 0;
    pad.done = done;
    PAD.name = name;
    ui_push(&PAD);
}

const char *views_keypad_text(void)
{
    return pad.text;
}

int views_keypad_sel(void)
{
    return pad.sel;
}

/* ---- Dice: ToolsDiceEntropyMnemonicLengthView, ToolsDiceEntropyEntryView */

static void dice_done(const char *rolls)
{
    uint16_t idx[SEED_MAX_WORDS];
    size_t n = 0;
    seed_t *s = sec_alloc(sizeof *s);
    bool ok = s && mnemonic_from_dice(rolls, idx, &n) && seed_from_indices(s, idx, n, "") == SEED_OK;
    char fp[9] = "";
    if (ok) {
        seed_fingerprint_hex(s, fp);
        storage_set_pending(s);     /* set_pending_seed() */
    }
    if (s) {
        seed_clear(s);
        sec_free(s, sizeof *s);
    }
    wally_bzero(idx, sizeof idx);
    if (!ok) {
        return;
    }
    ui_note("dice %u %s", (unsigned)n, fp);
    /* Cannot return BACK to this View: clear_history, and the keypad's
       leave wipes the rolls. */
    view_clear_history();
    SeedWordsWarningView(-1);
}

static void dice_length_select(int i)
{
    pad_open("ToolsDiceEntropyEntryView", "Dice Roll", "123456", NULL,
             i == 0 ? DICE_ROLLS_12 : DICE_ROLLS_24, dice_done);
}

void ToolsDiceEntropyMnemonicLengthView(void)
{
    static char twelve[24], twenty_four[24];
    snprintf(twelve, sizeof twelve, "12 words (%d rolls)", DICE_ROLLS_12);
    snprintf(twenty_four, sizeof twenty_four, "24 words (%d rolls)", DICE_ROLLS_24);
    const view_spec_t v = { .name = "ToolsDiceEntropyMnemonicLengthView", .title = "Mnemonic Length",
                            .buttons = { twelve, twenty_four }, .n_buttons = 2,
                            .select = dice_length_select };
    view_push(&v);
}

/* ---- The final-word calculator ------------------------------------------ */

static void final_word_done_select(int i)
{
    /* convert_pending_mnemonic_to_pending_seed(): the checksum is right now. */
    if (!storage_mnemonic_to_pending()) {
        return;
    }
    if (i == 0) {
        SeedFinalizeView();
    } else {
        SeedDiscardView(-1);
    }
}

static void ToolsCalcFinalWordDoneView(void)
{
    const size_t n = storage_mnemonic_len();
    uint16_t idx[SEED_MAX_WORDS];
    for (size_t i = 0; i < n; i++) {
        idx[i] = (uint16_t)storage_word(i);
    }
    char fp[9], text[64];
    seed_fingerprint_of(idx, n, fp);
    snprintf(text, sizeof text, "%s\n\nFingerprint: %s", BIP39_WORDLIST[idx[n - 1]], fp);
    wally_bzero(idx, sizeof idx);
    const view_spec_t v = { .name = "ToolsCalcFinalWordDoneView",
                            .title = n == 12 ? "12th Word" : "24th Word", .text = text,
                            .buttons = { "Load seed", "Discard" }, .n_buttons = 2,
                            .destructive_last = true, .select = final_word_done_select };
    view_push(&v);
    ui_note("final_word_done %s", fp);
}

static void show_final_word_select(int i)
{
    (void)i;
    ToolsCalcFinalWordDoneView();
}

static void bits11(unsigned w, char out[12])
{
    for (int b = 0; b < 11; b++) {
        out[b] = (char)('0' + ((w >> (10 - b)) & 1));
    }
    out[11] = '\0';
}

void ToolsCalcFinalWordShowFinalWordView(const char *coin_flips)
{
    const size_t n = storage_mnemonic_len();
    if (n != 12 && n != 24) {
        return;
    }
    uint16_t idx[SEED_MAX_WORDS], out[SEED_MAX_WORDS];
    size_t out_n = 0;
    for (size_t i = 0; i + 1 < n; i++) {
        idx[i] = (uint16_t)storage_word(i);
    }
    char selected[16] = "", bits[12], checksum[12];
    if (coin_flips) {
        /* Fill what will be the checksum with zeros: get_partial_final_word. */
        snprintf(bits, sizeof bits, "%s", coin_flips);
        idx[n - 1] = (uint16_t)mnemonic_partial_final_word(coin_flips);
    } else {
        idx[n - 1] = (uint16_t)storage_word(n - 1);
        snprintf(selected, sizeof selected, "%s", BIP39_WORDLIST[idx[n - 1]]);
        bits11(idx[n - 1], bits);
    }
    /* calculate_checksum: the real final word. */
    if (!mnemonic_complete_checksum(idx, n, out, &out_n) || out_n != n) {
        wally_bzero(idx, sizeof idx);
        return;
    }
    const uint16_t final = out[n - 1];
    storage_set_word(n - 1, final);
    char all[12];
    bits11(final, all);
    snprintf(checksum, sizeof checksum, "%s", all + (n == 12 ? 7 : 3));
    wally_bzero(idx, sizeof idx);
    wally_bzero(out, sizeof out);

    char text[160];
    if (selected[0]) {
        snprintf(text, sizeof text, "Your word: %s\nYour bits: %s\nChecksum: %s\nFinal word: %s",
                 selected, bits, checksum, BIP39_WORDLIST[final]);
    } else {
        snprintf(text, sizeof text, "Your bits: %s\nChecksum: %s\nFinal word: %s", bits, checksum,
                 BIP39_WORDLIST[final]);
    }
    const view_spec_t v = { .name = "ToolsCalcFinalWordShowFinalWordView", .title = "Final Word Calc",
                            .text = text, .buttons = { "Next" }, .n_buttons = 1,
                            .select = show_final_word_select };
    view_push(&v);
    ui_note("final_word %s %s %s %s", selected[0] ? selected : "-", bits, checksum,
            BIP39_WORDLIST[final]);
}

static void coin_flips_done(const char *flips)
{
    ToolsCalcFinalWordShowFinalWordView(flips);
}

static void finalize_prompt_select(int i)
{
    const size_t n = storage_mnemonic_len();
    const size_t bits = n == 12 ? 7 : 3;
    if (i == 0) {
        /* ToolsCalcFinalWordCoinFlipsView */
        pad_open("ToolsCalcFinalWordCoinFlipsView", "Coin Flip", "10", "Heads = 1, Tails = 0", bits,
                 coin_flips_done);
    } else if (i == 1) {
        SeedMnemonicEntryFinalWordView();
    } else {
        /* Finalize with zeros: "abandon" in the last place, all-zero flips. */
        storage_set_word(n - 1, 0);
        ToolsCalcFinalWordShowFinalWordView(bits == 7 ? "0000000" : "000");
    }
}

void ToolsCalcFinalWordFinalizePromptView(void)
{
    const size_t n = storage_mnemonic_len();
    char text[120];
    snprintf(text, sizeof text,
             "The %uth word is built from %u more entropy bits plus auto-calculated checksum.",
             (unsigned)n, n == 12 ? 7u : 3u);
    const view_spec_t v = { .name = "ToolsCalcFinalWordFinalizePromptView", .title = "Build Final Word",
                            .text = text,
                            .buttons = { "Coin flip entropy", "Word selection entropy",
                                         "Finalize with zeros" },
                            .n_buttons = 3, .select = finalize_prompt_select };
    view_push(&v);
}

static void num_words_select(int i)
{
    SeedMnemonicEntryCalcView(i == 0 ? 12 : 24);
}

void ToolsCalcFinalWordNumWordsView(void)
{
    const view_spec_t v = { .name = "ToolsCalcFinalWordNumWordsView", .title = "Mnemonic Length",
                            .buttons = { "12 words", "24 words" }, .n_buttons = 2,
                            .select = num_words_select };
    view_push(&v);
}

/* ---- Address explorer ---------------------------------------------------- */

/* The Controller's address_explorer_data and resume_main_flow ==
   FLOW__ADDRESS_EXPLORER. The seed is an index into seed-storage; the
   path and xpub are public. */
static struct {
    bool resume;
    int seed;
    script_opt_t script;
    char path[48];
    char xpub[120];
    bool is_change;
    int start;              /* the list page on screen */
    qr_encoder_t *enc;      /* the address QR, only while shown */
    uint8_t *qr_buf;
    int return_start, return_sel;
} ex;

#define ADDRS_PER_SCREEN 10

void tools_flow_clear(void)
{
    ex.resume = false;
}

static btc_script_t ex_btc_script(script_opt_t s)
{
    switch (s) {
    case SCRIPT_NAT: return BTC_SCRIPT_P2WPKH;
    case SCRIPT_NES: return BTC_SCRIPT_P2SH_P2WPKH;
    case SCRIPT_LEG: return BTC_SCRIPT_P2PKH;
    case SCRIPT_TR:  return BTC_SCRIPT_P2TR;
    default:         return BTC_SCRIPT_INVALID;
    }
}

/* The View under the top one, by name. */
static bool under_is(const char *name)
{
    return ui_depth() >= 2 && strcmp(ui_screen_at(ui_depth() - 2)->name, name) == 0;
}

static void ToolsAddressExplorerAddressListView(bool is_change, int start, int selected, bool replace);

static void address_qr_reopen(void)
{
    if (ui_depth() > 1) {   /* not after HOLD sent the user home */
        ToolsAddressExplorerAddressListView(ex.is_change, ex.return_start, ex.return_sel, false);
    }
}

static void address_qr_leave(void)
{
    if (ex.enc) {
        qr_encoder_wipe(ex.enc);
    }
    free(ex.enc);
    free(ex.qr_buf);
    ex.enc = NULL;
    ex.qr_buf = NULL;
    /* Exiting the QR always returns to the list, at the address shown. */
    ui_after(address_qr_reopen);
}

#define ADDR_QR_BUF 128

static void ToolsAddressExplorerAddressView(int index, const char *address)
{
    ex.enc = malloc(sizeof *ex.enc);
    ex.qr_buf = malloc(ADDR_QR_BUF);
    if (!ex.enc || !ex.qr_buf || !qr_encode_text(ex.enc, address, ex.qr_buf, ADDR_QR_BUF)) {
        free(ex.enc);
        free(ex.qr_buf);
        ex.enc = NULL;
        ex.qr_buf = NULL;
        return;
    }
    ex.return_start = ex.start;
    ex.return_sel = index - ex.start;
    ui_note("address %d %s", index, address);
    /* skip_current_view: the QR takes the list's place. */
    ui_pop();
    ui_show_qr("ToolsAddressExplorerAddressView", ex.enc, address_qr_leave);
}

static char ex_addrs[ADDRS_PER_SCREEN][100];

static void address_list_select(int i)
{
    if (i == ADDRS_PER_SCREEN) {
        ToolsAddressExplorerAddressListView(ex.is_change, ex.start + ADDRS_PER_SCREEN, 0, false);
        return;
    }
    ToolsAddressExplorerAddressView(ex.start + i, ex_addrs[i]);
}

static void address_list_back(void)
{
    ui_pop();
    /* The page under, if any, is what is on screen again. */
    if (ex.start >= ADDRS_PER_SCREEN && strcmp(ui_top()->name, "ToolsAddressExplorerAddressListView") == 0) {
        ex.start -= ADDRS_PER_SCREEN;
        for (int k = 0; k < ADDRS_PER_SCREEN; k++) {
            btc_address(ex.xpub, ex_btc_script(ex.script), (uint32_t)(ex.start + k), ex.is_change,
                        SETTINGS.network, ex_addrs[k], sizeof ex_addrs[k]);
        }
    }
}

static void ToolsAddressExplorerAddressListView(bool is_change, int start, int selected, bool replace)
{
    (void)replace;
    ui_working("Calculating addrs...");
    ex.is_change = is_change;
    ex.start = start;
    static char labels[ADDRS_PER_SCREEN][VIEW_LABEL_MAX];
    view_spec_t v = { .name = "ToolsAddressExplorerAddressListView",
                      .title = is_change ? "Change Addrs" : "Receive Addrs",
                      .select = address_list_select, .back = address_list_back, .selected = selected };
    for (int k = 0; k < ADDRS_PER_SCREEN; k++) {
        const int index = start + k;
        if (!btc_address(ex.xpub, ex_btc_script(ex.script), (uint32_t)index, is_change, SETTINGS.network,
                         ex_addrs[k], sizeof ex_addrs[k])) {
            snprintf(ex_addrs[k], sizeof ex_addrs[k], "?");
        }
        const size_t len = strlen(ex_addrs[k]);
        snprintf(labels[k], sizeof labels[k], "%d:%.8s..%s", index, ex_addrs[k],
                 ex_addrs[k] + (len > 6 ? len - 6 : 0));
        v.buttons[v.n_buttons++] = labels[k];
        ui_note("addr %d %s", index, ex_addrs[k]);
    }
    v.buttons[v.n_buttons++] = "Next";
    view_push(&v);
}

static void address_type_select(int i)
{
    ToolsAddressExplorerAddressListView(i == 1, 0, 0, false);
}

static void address_type_back(void)
{
    /* Entered from SeedOptionsView: the flow ends here, or SeedOptionsView
       would send the user straight back in. */
    if (under_is("SeedOptionsView")) {
        ex.resume = false;
    }
    ui_pop();
}

static void ToolsAddressExplorerAddressTypeView(int seed, script_opt_t script, bool replace)
{
    const seed_t *s = storage_seed(seed);
    if (s == NULL || ex_btc_script(script) == BTC_SCRIPT_INVALID ||
        !btc_standard_path(SETTINGS.network, BTC_SIG_SINGLE, ex_btc_script(script), ex.path, sizeof ex.path) ||
        !btc_xpub(s->bytes, sizeof s->bytes, ex.path, SETTINGS.network, 0, ex.xpub, sizeof ex.xpub)) {
        return;
    }
    ex.seed = seed;
    ex.script = script;
    char fp[9], text[96];
    seed_fingerprint_hex(s, fp);
    snprintf(text, sizeof text, "%s\n%s\n%s", fp, script_type_name(script), ex.path);
    const view_spec_t v = { .name = "ToolsAddressExplorerAddressTypeView", .title = "Address Explorer",
                            .text = text, .buttons = { "Receive addresses", "Change addresses" },
                            .n_buttons = 2, .select = address_type_select, .back = address_type_back };
    if (replace) {
        view_replace(&v);
    } else {
        view_push(&v);
    }
    ui_note("explorer %s %s", fp, ex.path);
}

/* SeedExportXpubScriptTypeView as the explorer uses it: single sig, its own
   title, and on to the address type rather than the QR format. */
static script_opt_t ex_script_map[SCRIPT_COUNT];

static void ex_script_select(int i)
{
    if (ex_script_map[i] == SCRIPT_CUSTOM) {
        not_yet("SeedExportXpubCustomDerivationView");
        return;
    }
    ToolsAddressExplorerAddressTypeView(view_arg(), ex_script_map[i], false);
}

static void ex_script_back(void)
{
    if (under_is("SeedOptionsView")) {
        ex.resume = false;
    }
    ui_pop();
}

void tools_explorer_script_type(int seed, bool replace)
{
    view_spec_t v = { .name = "SeedExportXpubScriptTypeView", .title = "Address Explorer",
                      .select = ex_script_select, .back = ex_script_back, .arg = seed };
    for (int t = 0; t < SCRIPT_COUNT; t++) {
        if (SETTINGS.script_types[t]) {
            ex_script_map[v.n_buttons] = (script_opt_t)t;
            v.buttons[v.n_buttons++] = script_type_name((script_opt_t)t);
        }
    }
    if (v.n_buttons == 1) {
        /* Nothing to choose: straight on, in this View's place. */
        ToolsAddressExplorerAddressTypeView(seed, ex_script_map[0], replace);
        return;
    }
    if (replace) {
        view_replace(&v);
    } else {
        view_push(&v);
    }
}

void tools_explorer_from_seed(int seed)
{
    ex.resume = true;
    tools_explorer_script_type(seed, false);
}

bool tools_explorer_resume(int seed)
{
    if (!ex.resume) {
        return false;
    }
    /* The flow stays set: it is still needed after the script type. */
    tools_explorer_script_type(seed, false);
    return true;
}

enum { SRC_SCAN, SRC_DESCRIPTOR, SRC_12, SRC_24, SRC_ELECTRUM };

static void select_source_select(int i)
{
    const int n = storage_count();
    ex.resume = true;
    if (i < n) {
        tools_explorer_script_type(i, false);
        return;
    }
    switch (i - n) {
    case SRC_SCAN:       ui_push(&SCREEN_SCAN_SEED); break;
    case SRC_DESCRIPTOR: not_yet("ScanWalletDescriptorView"); break;
    case SRC_12:         SeedMnemonicEntryView(12); break;
    case SRC_24:         SeedMnemonicEntryView(24); break;
    default:             not_yet("SeedElectrumMnemonicStartView"); break;
    }
}

void ToolsAddressExplorerSelectSourceView(void)
{
    static char labels[SEED_STORAGE_MAX][9];
    view_spec_t v = { .name = "ToolsAddressExplorerSelectSourceView", .title = "Address Explorer",
                      .select = select_source_select };
    for (int i = 0; i < storage_count(); i++) {
        seed_fingerprint_hex(storage_seed(i), labels[i]);
        v.buttons[v.n_buttons++] = labels[i];
    }
    v.buttons[v.n_buttons++] = "Scan a seed";
    v.buttons[v.n_buttons++] = "Scan wallet descriptor";
    v.buttons[v.n_buttons++] = "Enter 12-word seed";
    v.buttons[v.n_buttons++] = "Enter 24-word seed";
    if (SETTINGS.electrum_seeds) {
        v.buttons[v.n_buttons++] = "Enter Electrum seed";
    }
    view_push(&v);
}
