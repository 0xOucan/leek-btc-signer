/*
 * A keypad for the encoder: SeedSigner's small KeyboardScreens (dice rolls,
 * coin flips, the BIP-85 child index). UP and DOWN choose a key, DEL or OK;
 * OK on a key enters it. The entry is wiped when the keypad closes, since
 * dice rolls and coin flips are entropy. See keypad.c.
 */
#pragma once

#include <stddef.h>

typedef struct {
    const char *name;       /* the SeedSigner View it stands for */
    const char *title;      /* "Dice Roll": counted ("Dice Roll 3/50") when total > 0 */
    const char *keys;       /* "123456", "10", "0123456789" */
    const char *hint;       /* a line under the keys, or NULL */
    size_t total;           /* finish after this many; 0: an OK key finishes */
    void (*done)(const char *text);
    const char *initial;    /* the entry to start from ("m/"), or NULL */
} keypad_spec_t;

void keypad_open(const keypad_spec_t *spec);

/* For the flow tests: the entry so far, and the key selected (the key
 * count for DEL, one more for OK). */
const char *views_keypad_text(void);
int views_keypad_sel(void);
