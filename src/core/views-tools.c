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
 */
#include "views-tools.h"

#include <stdio.h>
#include <string.h>

#include "btc.h"
#include "secmem.h"
#include "seed-storage.h"
#include "theme.h"
#include "views-seed.h"
#include "views.h"
#include "wally_core.h"

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
