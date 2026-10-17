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

#include <stdbool.h>

/* The address explorer, from the Tools menu. */
void ToolsAddressExplorerSelectSourceView(void);
/* SeedOptionsView's "Address explorer": straight to the script type. */
void tools_explorer_from_seed(int seed);
/* SeedOptionsView's redirect when a seed was loaded for the explorer:
 * opens the script type and returns true. */
bool tools_explorer_resume(int seed);
/* What MainMenuView drops: resume_main_flow for the explorer. */
void tools_flow_clear(void);

#include "keypad.h"     /* views_keypad_text(), views_keypad_sel() for the tests */
