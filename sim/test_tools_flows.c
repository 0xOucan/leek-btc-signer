/*
 * SeedSigner's Tools flows for a new seed from dice rolls and for the
 * final-word calculator, driven through the device's screens with the keys
 * a user would press. SeedSigner's test suite has no flow tests for these,
 * so the expected results come from running SeedSigner's own Views through
 * its FlowTest harness (scripts/vectors/gen_tools_vectors.py): the mnemonic
 * and fingerprint each dice case makes, and every figure
 * ToolsCalcFinalWordShowFinalWordView shows (the user's bits, the checksum
 * bits, the final word) for 12 and 24 words, from coin flips, a chosen word
 * and zeros.
 */
#include "btc-init.h"
#include "flow.h"
#include "views-tools.h"
#include "vectors/seedsigner_tools.h"

#define N(a) (sizeof(a) / sizeof((a)[0]))

/* Move the keypad's selection to key `want` (its length: DEL) with DOWN. */
static void keypad_to(int want)
{
    for (int guard = 0; guard < 16 && views_keypad_sel() != want; guard++) {
        key(IN_DOWN);
    }
}

/* Enter `text` on the keypad as a user would: DOWN to each key, OK. */
static void keypad(const char *keys, const char *text)
{
    for (const char *c = text; *c; c++) {
        keypad_to((int)(strchr(keys, *c) - keys));
        key(IN_OK);
    }
}

static bool pending_is(const char *mnemonic)
{
    const seed_t *s = storage_pending();
    if (s == NULL) {
        return false;
    }
    char got[300] = "";
    for (size_t i = 0; i < s->n_words; i++) {
        strcat(got, BIP39_WORDLIST[s->words[i]]);
        if (i + 1 < s->n_words) strcat(got, " ");
    }
    return strcmp(got, mnemonic) == 0;
}

static void test_dice(void)
{
    printf("== new seed from dice rolls: every case SeedSigner's Views ran\n");
    for (size_t i = 0; i < N(VEC_TOOLS_DICE); i++) {
        const vec_tools_dice_t *v = &VEC_TOOLS_DICE[i];
        const bool twelve = strlen(v->rolls) == 50;
        fresh();
        notes_reset();
        choose("Tools");
        STEP("ToolsMenuView");
        choose("New seed (dice)");
        STEP("ToolsDiceEntropyMnemonicLengthView");
        choose(twelve ? "12 words (50 rolls)" : "24 words (99 rolls)");
        STEP("ToolsDiceEntropyEntryView");
        keypad("123456", v->rolls);
        STEP("SeedWordsWarningView");
        char want[64];
        snprintf(want, sizeof want, "dice %d %s", twelve ? 12 : 24, v->fingerprint);
        CHECK(noted(want), "dice %zu: no \"%s\"", i, want);
        CHECK(pending_is(v->mnemonic), "dice %zu: not SeedSigner's mnemonic", i);
        CHECK(strlen(views_keypad_text()) == 0, "dice %zu: the rolls were not wiped", i);
        key(IN_BACK);
        STEP("MainMenuView");       /* clear_history: no way back to the rolls */
    }
}

static void test_dice_delete_and_back(void)
{
    printf("== dice: DEL removes a roll, BACK leaves and wipes\n");
    fresh();
    choose("Tools");
    choose("New seed (dice)");
    choose("12 words (50 rolls)");
    STEP("ToolsDiceEntropyEntryView");
    keypad("123456", "35");
    CHECK(strcmp(views_keypad_text(), "35") == 0, "keypad: %s", views_keypad_text());
    keypad_to(6);   /* DEL */
    key(IN_OK);
    CHECK(strcmp(views_keypad_text(), "3") == 0, "DEL left %s", views_keypad_text());
    key(IN_OK);
    key(IN_OK);     /* DEL on nothing: nothing */
    CHECK(strcmp(views_keypad_text(), "") == 0, "DEL left %s", views_keypad_text());
    keypad_to(0);
    key(IN_UP);     /* wraps from the first key to DEL */
    CHECK(views_keypad_sel() == 6, "UP did not wrap to DEL");
    keypad("123456", "61");
    CHECK(strcmp(views_keypad_text(), "61") == 0, "keypad: %s", views_keypad_text());
    key(IN_BACK);
    STEP("ToolsDiceEntropyMnemonicLengthView");
    CHECK(strlen(views_keypad_text()) == 0, "BACK did not wipe the rolls");
}

static void test_final_word(void)
{
    printf("== final-word calculator: every case SeedSigner's Views ran\n");
    unsigned by_mode[3] = { 0 };
    for (size_t i = 0; i < N(VEC_TOOLS_FINAL); i++) {
        const vec_tools_final_t *v = &VEC_TOOLS_FINAL[i];
        fresh();
        notes_reset();
        choose("Tools");
        choose("Calc 12th/24th word");
        STEP("ToolsCalcFinalWordNumWordsView");
        choose(v->n == 12 ? "12 words" : "24 words");
        STEP("SeedMnemonicEntryView");
        type_mnemonic(v->words);
        STEP("ToolsCalcFinalWordFinalizePromptView");
        if (strcmp(v->mode, "coins") == 0) {
            choose("Coin flip entropy");
            STEP("ToolsCalcFinalWordCoinFlipsView");
            keypad("10", v->flips);
            by_mode[0]++;
        } else if (strcmp(v->mode, "word") == 0) {
            choose("Word selection entropy");
            STEP("SeedMnemonicEntryView");
            type_word(v->chosen);
            by_mode[1]++;
        } else {
            choose("Finalize with zeros");
            by_mode[2]++;
        }
        STEP("ToolsCalcFinalWordShowFinalWordView");
        char want[160];
        snprintf(want, sizeof want, "final_word %s %s %s %s",
                 v->selected_final_word ? v->selected_final_word : "-", v->selected_final_bits,
                 v->checksum_bits, v->actual_final_word);
        CHECK(noted(want), "final %zu: no \"%s\" (last: %s)", i, want, last_note);
        choose("Next");
        STEP("ToolsCalcFinalWordDoneView");
        snprintf(want, sizeof want, "final_word_done %s", v->fingerprint);
        CHECK(noted(want), "final %zu: no \"%s\"", i, want);
        choose("Load seed");
        STEP("SeedFinalizeView");
        CHECK(pending_is(v->mnemonic), "final %zu: not SeedSigner's mnemonic", i);
        snprintf(want, sizeof want, "fingerprint %s", v->fingerprint);
        CHECK(noted(want), "final %zu: SeedFinalizeView shows another fingerprint", i);
    }
    printf("   %u from coin flips, %u from a chosen word, %u from zeros\n",
           by_mode[0], by_mode[1], by_mode[2]);
}

/* BACK through the calculator: each View is under the next, as SeedSigner's
   back stack; Discard wipes the pending seed. */
static void test_final_word_back_and_discard(void)
{
    printf("== final-word calculator: BACK at each step, and Discard\n");
    const vec_tools_final_t *v = NULL;
    for (size_t i = 0; i < N(VEC_TOOLS_FINAL) && !v; i++) {
        if (VEC_TOOLS_FINAL[i].n == 12 && strcmp(VEC_TOOLS_FINAL[i].mode, "word") == 0) {
            v = &VEC_TOOLS_FINAL[i];
        }
    }
    if (!v) {
        CHECK(0, "no vector");
        return;
    }
    fresh();
    choose("Tools");
    choose("Calc 12th/24th word");
    choose("12 words");
    type_mnemonic(v->words);
    STEP("ToolsCalcFinalWordFinalizePromptView");
    key(IN_BACK);
    STEP("SeedMnemonicEntryView");      /* word 11 again */
    type_word(strrchr(v->words, ' ') + 1);
    STEP("ToolsCalcFinalWordFinalizePromptView");
    choose("Word selection entropy");
    STEP("SeedMnemonicEntryView");
    key(IN_BACK);
    STEP("ToolsCalcFinalWordFinalizePromptView");
    choose("Coin flip entropy");
    STEP("ToolsCalcFinalWordCoinFlipsView");
    key(IN_BACK);
    STEP("ToolsCalcFinalWordFinalizePromptView");
    choose("Word selection entropy");
    type_word(v->chosen);
    STEP("ToolsCalcFinalWordShowFinalWordView");
    key(IN_BACK);
    STEP("SeedMnemonicEntryView");
    type_word(v->chosen);
    choose("Next");
    STEP("ToolsCalcFinalWordDoneView");
    choose("Discard");
    STEP("SeedDiscardView");
    choose("Discard");
    STEP("MainMenuView");
    CHECK(storage_pending() == NULL && storage_count() == 0, "Discard kept the seed");
}

int main(void)
{
    btc_init(rand_fill);
    ui_set_clock(clock_fn);
    ui_set_note_hook(note);
    test_dice();
    test_dice_delete_and_back();
    test_final_word();
    test_final_word_back_and_discard();
    fresh();
    storage_wipe_all();
    return CHECK_DONE("test_tools_flows");
}
