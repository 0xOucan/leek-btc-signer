/*
 * SeedSigner's seed views, ported. See views-seed.c. Each function opens the
 * SeedSigner View of the same name; `seed` is an index into seed-storage,
 * or -1 for the pending seed.
 */
#pragma once

#include <stddef.h>

#include "mnemonic-entry.h"
#include "text-entry.h"

void SeedsMenuView(void);
void SeedMnemonicEntryView(size_t words);
/* SeedMnemonicEntryView(is_calc_final_word=True), from the final-word
 * calculator: the first n-1 words, then the calculator's prompt. */
void SeedMnemonicEntryCalcView(size_t words);
/* The calculator's "Word selection entropy": type the last word. */
void SeedMnemonicEntryFinalWordView(void);
void SeedMnemonicInvalidView(void);
void SeedFinalizeView(void);
void SeedAddPassphraseView(void);
void SeedAddPassphraseExitDialogView(void);
void SeedReviewPassphraseView(void);
void SeedOptionsView(int seed);
void SeedDiscardView(int seed);
void SeedBackupView(int seed);
void SeedWordsWarningView(int seed);
void SeedWordsView(int seed);
void SeedExportXpubSigTypeView(int seed);

/* For the host flow tests, which press keys against the entry state. */
MnemonicEntry *views_entry_state(void);
TextEntry *views_passphrase_state(void);
