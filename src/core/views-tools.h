/*
 * SeedSigner's Tools views (views/tools_views.py), ported. See views-tools.c.
 */
#pragma once

/* "New seed" from dice rolls: the mnemonic length, then the rolls. */
void ToolsDiceEntropyMnemonicLengthView(void);

/* "Calc 12th/24th word": the length, the first words, then the last
 * word built from coin flips, a chosen word or zeros, plus its checksum. */
void ToolsCalcFinalWordNumWordsView(void);
void ToolsCalcFinalWordFinalizePromptView(void);
/* `coin_flips` NULL: the last word was chosen and typed. */
void ToolsCalcFinalWordShowFinalWordView(const char *coin_flips);

/* For the flow tests: the keypad screen's entry so far, and the key
 * selected (its key count for DEL). */
const char *views_keypad_text(void);
int views_keypad_sel(void);
