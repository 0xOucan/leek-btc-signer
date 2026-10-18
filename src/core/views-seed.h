/*
 * SeedSigner's seed views, ported. See views-seed.c. Each function opens the
 * SeedSigner View of the same name; `seed` is an index into seed-storage,
 * or -1 for the pending seed.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mnemonic-entry.h"
#include "text-entry.h"

void SeedsMenuView(void);
void SeedMnemonicEntryView(size_t words);
/* SeedMnemonicEntryView(is_calc_final_word=True), from the final-word
 * calculator: the first n-1 words, then the calculator's prompt. */
void SeedMnemonicEntryCalcView(size_t words);
/* The calculator's "Word selection entropy": type the last word. */
void SeedMnemonicEntryFinalWordView(void);
/* SeedElectrumMnemonicStartView: its warning, then 12 words entered as an
 * Electrum seed. */
void SeedElectrumMnemonicStartView(void);
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
void SeedWordsBackupTestPromptView(int seed);
void SeedBIP85SelectNumWordsView(int seed);
/* For the flow tests: word `i` of the mnemonic the words screen shows (a
 * BIP-85 child's, when it shows one), or NULL. */
const char *views_shown_word(size_t i);
void SeedTranscribeSeedQRFormatView(int seed);
/* The confirming scan's result: the words of the SeedQR read, or n = 0
 * for anything that is not one. Called by the scan screen. */
void transcribe_confirm_scanned(const uint16_t *words, size_t n);
void SeedExportXpubSigTypeView(int seed);
/* SeedExportXpubCustomDerivationView: `done` gets the path typed. */
void SeedExportXpubCustomDerivationView(void (*done)(const char *path));

/* For the host flow tests, which press keys against the entry state. */
MnemonicEntry *views_entry_state(void);
TextEntry *views_passphrase_state(void);
