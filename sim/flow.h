/*
 * Driving the device's screens as a user would, for the flow tests that
 * port SeedSigner's tests/test_flows_*.py: press keys, choose a button by
 * its label, type BIP-39 words and passphrases on the entry screens, and
 * scan a frame. Each step names the SeedSigner View the device must be on.
 */
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "mnemonic-entry.h"
#include "qr-source.h"
#include "screens.h"
#include "seed-storage.h"
#include "settings.h"
#include "text-entry.h"
#include "ui.h"
#include "views-address.h"
#include "views-psbt.h"
#include "views-seed.h"
#include "keypad.h"
#include "views-tools.h"
#include "views.h"

/* The last note, and every note since notes_reset(), one per line. */
static char last_note[200];
static char note_log[16384];
static size_t note_log_len;

static inline void note(const char *n)
{
    snprintf(last_note, sizeof last_note, "%s", n);
    const size_t k = strlen(n);
    if (note_log_len + k + 2 < sizeof note_log) {
        memcpy(note_log + note_log_len, n, k);
        note_log_len += k;
        note_log[note_log_len++] = '\n';
        note_log[note_log_len] = '\0';
    }
}

static inline void notes_reset(void)
{
    note_log_len = 0;
    note_log[0] = '\0';
}

/* True if `line` was noted since notes_reset(). */
static inline bool noted(const char *line)
{
    const size_t k = strlen(line);
    for (const char *p = note_log; (p = strstr(p, line)) != NULL; p++) {
        if ((p == note_log || p[-1] == '\n') && p[k] == '\n') {
            return true;
        }
    }
    return false;
}

static inline void rand_fill(void *p, unsigned n) { memset(p, 0x5A, n); }
static uint32_t now;
static inline uint32_t clock_fn(void) { return now; }

static inline const char *on(void) { return ui_top()->name; }

#define STEP(view) CHECK(strcmp(on(), (view)) == 0, "expected %s, on %s", (view), on())

static inline void key(input_ev_t ev)
{
    ui_input(ev);
    ui_render();
}

/* Choose the button labelled `label` on the current menu, list or status
   view, by scrolling to it as a user would. */
static inline void choose(const char *label)
{
    for (int i = 0; i < 40; i++) key(IN_UP);
    for (int i = 0; i < 40; i++) {
        const char *sel = ui_selected_label();
        if (sel && strcmp(sel, label) == 0) {
            key(IN_OK);
            return;
        }
        key(IN_DOWN);
    }
    CHECK(0, "no button \"%s\" on %s", label, on());
}

/* Type one BIP-39 word on SeedMnemonicEntryView with the selector keys. */
static inline void type_word(const char *word)
{
    for (int guard = 0; guard < 200; guard++) {
        MnemonicEntry *e = views_entry_state();
        const char *sel = mnemonic_entry_selected_word(e);
        if (sel && strcmp(sel, word) == 0) {
            key(IN_OK);
            return;
        }
        const char opt = mnemonic_entry_option(e);
        const bool group = mnemonic_entry_on_group(e);
        const char next = word[e->prefix_len];
        if (!sel && opt == MNEMONIC_ENTRY_COMMIT && next == '\0') {
            key(IN_OK);
            return;
        }
        if (!sel && !group && opt == next) {
            key(IN_OK);
            continue;
        }
        key(IN_DOWN);
    }
    CHECK(0, "could not type \"%s\"", word);
}

static inline void type_mnemonic(const char *words)
{
    char buf[256];
    snprintf(buf, sizeof buf, "%s", words);
    for (char *w = strtok(buf, " "); w; w = strtok(NULL, " ")) {
        type_word(w);
    }
}

static inline void type_passphrase(const char *text, bool finish)
{
    for (const char *c = text; *c; c++) {
        for (int guard = 0; guard < 400; guard++) {
            TextEntry *e = views_passphrase_state();
            const char opt = text_entry_option(e);
            if (opt == *c) {
                key(IN_OK);
                break;
            }
            /* Switch sets when the character is not in this one. */
            const bool want_upper = *c >= 'A' && *c <= 'Z';
            const bool want_lower = *c >= 'a' && *c <= 'z';
            const bool in_set = (want_upper && e->set == TEXT_SET_UPPER) ||
                                (want_lower && e->set == TEXT_SET_LOWER) ||
                                (!want_upper && !want_lower && e->set == TEXT_SET_SYMBOL);
            if (!in_set && ((want_upper && opt == TEXT_ENTRY_MODE_CAPS) ||
                            (want_lower && opt == TEXT_ENTRY_MODE_ABC) ||
                            (!want_upper && !want_lower && opt == TEXT_ENTRY_MODE_NUM))) {
                key(IN_OK);
                continue;
            }
            key(IN_DOWN);
        }
    }
    if (!finish) {
        return;
    }
    /* Then the OK option. */
    for (int guard = 0; guard < 80; guard++) {
        TextEntry *e = views_passphrase_state();
        if (text_entry_option(e) == TEXT_ENTRY_OK) {
            key(IN_OK);
            return;
        }
        key(IN_DOWN);
    }
}

static inline void fresh(void)
{
    psbt_flow_clear();
    tools_flow_clear();
    address_flow_clear();
    storage_wipe_all();
    settings_reset();
    ui_init(&SCREEN_HOME);
}

static inline void scan(const uint8_t *data, size_t len)
{
    qr_source_push(data, len);
    for (int i = 0; i < 4; i++) {
        ui_tick();
        ui_render();
    }
}

/* Move the keypad's selection to key `want` (its length: DEL) with DOWN. */
static inline void keypad_to(int want)
{
    for (int guard = 0; guard < 16 && views_keypad_sel() != want; guard++) {
        key(IN_DOWN);
    }
}

/* Enter `text` on the keypad as a user would: DOWN to each key, OK. */
static inline void keypad(const char *keys, const char *text)
{
    for (const char *c = text; *c; c++) {
        keypad_to((int)(strchr(keys, *c) - keys));
        key(IN_OK);
    }
}
