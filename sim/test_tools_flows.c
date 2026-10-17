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

/* Choose the index-th button, as SeedSigner's tests' screen_return_value. */
static void choose_index(int index)
{
    for (int i = 0; i < 40; i++) key(IN_UP);
    for (int i = 0; i < index; i++) key(IN_DOWN);
    key(IN_OK);
}

static void scan_text(const char *text)
{
    scan((const uint8_t *)text, strlen(text));
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

/* ---- tests/test_flows_tools.py: the address explorer --------------------- */

static void load_seed(const char *mnemonic, const char *passphrase)
{
    seed_t s;
    CHECK(seed_from_mnemonic(&s, mnemonic, passphrase) == SEED_OK, "seed");
    storage_set_pending(&s);
    seed_clear(&s);
    storage_finalize();
}

static const char *ABANDON = "abandon abandon abandon abandon abandon abandon abandon abandon "
                             "abandon abandon abandon about";

static void test__address_explorer__flow(void)
{
    printf("== test__address_explorer__flow\n");
    fresh();
    notes_reset();
    load_seed(ABANDON, "");
    choose("Tools");
    STEP("ToolsMenuView");
    choose("Address explorer");
    STEP("ToolsAddressExplorerSelectSourceView");
    choose_index(0);
    STEP("SeedExportXpubScriptTypeView");
    choose("Native Segwit");
    STEP("ToolsAddressExplorerAddressTypeView");
    choose("Receive addresses");
    STEP("ToolsAddressExplorerAddressListView");
    CHECK(noted("addr 0 bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu"), "BIP-84's first address");
    choose("Next");
    STEP("ToolsAddressExplorerAddressListView");
    choose_index(4);
    STEP("ToolsAddressExplorerAddressView");
    CHECK(strncmp(last_note, "address 14 bc1q", 15) == 0, "note %s", last_note);
    key(IN_OK);
    STEP("ToolsAddressExplorerAddressListView");
    const char *sel = ui_selected_label();
    CHECK(sel && strncmp(sel, "14:", 3) == 0, "the list did not return to address 14: %s", sel ? sel : "-");
    key(IN_BACK);
    STEP("ToolsAddressExplorerAddressListView");    /* the first page */
    key(IN_BACK);
    STEP("ToolsAddressExplorerAddressTypeView");
}

static void test__address_explorer__loadseed__sideflow(void)
{
    printf("== test__address_explorer__loadseed__sideflow\n");
    fresh();
    choose("Tools");
    choose("Address explorer");
    choose("Scan a seed");
    STEP("ScanSeedQRView");
    scan_text("000000000000000000000000000000000000000000000003");
    STEP("SeedFinalizeView");
    choose("Done");
    STEP("SeedExportXpubScriptTypeView");       /* SeedOptionsView redirected */

    /* The flow is still set: a seed finalised with a passphrase goes on to
       the explorer too. */
    storage_wipe_all();
    seed_t s;
    seed_from_mnemonic(&s, ABANDON, "");
    storage_set_pending(&s);
    seed_clear(&s);
    SeedFinalizeView();
    STEP("SeedFinalizeView");
    choose("BIP-39 Passphrase");
    STEP("SeedAddPassphraseView");
    type_passphrase("mypassphrase", true);
    STEP("SeedReviewPassphraseView");
    choose("Done");
    STEP("SeedExportXpubScriptTypeView");
}

static void test__address_explorer__scan_wrong_qrtype__flow(void)
{
    printf("== test__address_explorer__scan_wrong_qrtype__flow\n");
    fresh();
    choose("Tools");
    choose("Address explorer");
    choose("Scan a seed");
    scan_text("bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq");
    STEP("ErrorView");
}

static void test__address_explorer__back_button__flow(void)
{
    printf("== test__address_explorer__back_button__flow\n");
    /* 1: seed loaded, two script types: BACK can change the script type. */
    fresh();
    load_seed(ABANDON, "");
    for (int t = 0; t < SCRIPT_COUNT; t++) SETTINGS.script_types[t] = t == SCRIPT_NAT || t == SCRIPT_TR;
    choose("Seeds");
    STEP("SeedsMenuView");
    choose_index(0);
    STEP("SeedOptionsView");
    choose("Address explorer");
    STEP("SeedExportXpubScriptTypeView");
    choose_index(0);
    STEP("ToolsAddressExplorerAddressTypeView");
    key(IN_BACK);
    STEP("SeedExportXpubScriptTypeView");

    /* 2: one script type, from SeedOptionsView: BACK to it, flow ended. */
    for (int t = 0; t < SCRIPT_COUNT; t++) SETTINGS.script_types[t] = t == SCRIPT_NAT;
    key(IN_HOLD);
    choose("Seeds");
    choose_index(0);
    choose("Address explorer");
    STEP("ToolsAddressExplorerAddressTypeView");    /* the script type redirected */
    key(IN_BACK);
    STEP("SeedOptionsView");
    choose("Backup seed");
    STEP("SeedBackupView");                         /* no redirect loop */

    /* 3: one script type, from the Tools menu: BACK to the source. */
    key(IN_HOLD);
    choose("Tools");
    choose("Address explorer");
    STEP("ToolsAddressExplorerSelectSourceView");
    choose_index(0);
    STEP("ToolsAddressExplorerAddressTypeView");
    key(IN_BACK);
    STEP("ToolsAddressExplorerSelectSourceView");

    /* 4: no seed: loading one mid-flow clears the history, so BACK goes home. */
    key(IN_HOLD);
    storage_discard(0);
    choose("Tools");
    choose("Address explorer");
    choose("Scan a seed");
    scan_text("000000000000000000000000000000000000000000000003");
    STEP("SeedFinalizeView");
    choose("Done");
    STEP("ToolsAddressExplorerAddressTypeView");
    key(IN_BACK);
    STEP("MainMenuView");
}

/* Every address list SeedSigner's explorer produced, page by page. */
static void test_every_address_list(void)
{
    printf("== every address list SeedSigner's explorer made, through the screens\n");
    static const struct { const char *ss; script_opt_t t; } scripts[] = {
        { "nat", SCRIPT_NAT }, { "nes", SCRIPT_NES }, { "leg", SCRIPT_LEG }, { "tr", SCRIPT_TR },
    };
    unsigned lists = 0, addrs = 0;
    for (size_t i = 0; i < N(VEC_TOOLS_EXPLORER); i++) {
        const vec_tools_explorer_t *v = &VEC_TOOLS_EXPLORER[i];
        script_opt_t t = SCRIPT_COUNT;
        for (size_t k = 0; k < N(scripts); k++) {
            if (strcmp(scripts[k].ss, v->script_type) == 0) t = scripts[k].t;
        }
        fresh();
        notes_reset();
        SETTINGS.network = v->net;
        for (int k = 0; k < SCRIPT_COUNT; k++) SETTINGS.script_types[k] = k != SCRIPT_CUSTOM;
        load_seed(v->mnemonic, "");
        choose("Tools");
        choose("Address explorer");
        choose_index(0);
        STEP("SeedExportXpubScriptTypeView");
        choose(script_type_name(t));
        STEP("ToolsAddressExplorerAddressTypeView");
        char want[160];
        snprintf(want, sizeof want, "explorer %s %s", v->fingerprint, v->derivation_path);
        CHECK(noted(want), "list %zu: no \"%s\" (last: %s)", i, want, last_note);
        choose(v->is_change ? "Change addresses" : "Receive addresses");
        STEP(v->is_change ? "ToolsAddressExplorerAddressListView" : "ToolsAddressExplorerAddressListView");
        choose("Next");
        for (int k = 0; k < 20; k++) {
            snprintf(want, sizeof want, "addr %d %s", k, v->addresses[k]);
            CHECK(noted(want), "list %zu: no \"%s\"", i, want);
            addrs++;
        }
        choose_index(4);
        snprintf(want, sizeof want, "address 14 %s", v->addresses[14]);
        CHECK(noted(want), "list %zu: the QR is not of address 14", i);
        lists++;
    }
    printf("   %u lists, %u addresses as SeedSigner's\n", lists, addrs);
}

/* A custom path: SeedSigner shows the address type, then its list raises. */
static void test__address_explorer__custom_derivation(void)
{
    printf("== address explorer with a custom path\n");
    fresh();
    load_seed(ABANDON, "");
    for (int t = 0; t < SCRIPT_COUNT; t++) SETTINGS.script_types[t] = t == SCRIPT_NAT || t == SCRIPT_CUSTOM;
    choose("Tools");
    choose("Address explorer");
    choose_index(0);
    STEP("SeedExportXpubScriptTypeView");
    choose("Custom Derivation");
    STEP("SeedExportXpubCustomDerivationView");
    keypad("/'0123456789", "84'/0'/0'");
    keypad_to(13);
    key(IN_OK);
    STEP("ToolsAddressExplorerAddressTypeView");
    CHECK(noted("explorer 73c5da0a m/84'/0'/0'"), "the custom path: %s", last_note);
    choose("Receive addresses");
    STEP("UnhandledExceptionView");
    choose("Back to Main Menu");
    STEP("MainMenuView");
}

/* ---- Address verification ---------------------------------------------------- */

/* Let SeedAddressVerificationView search, as its ticks would. */
static void search(void)
{
    for (int i = 0; i < 400 && strcmp(on(), "SeedAddressVerificationView") == 0; i++) {
        ui_tick();
        ui_render();
    }
}

static void verify(const char *address)
{
    choose("Tools");
    STEP("ToolsMenuView");
    choose("Verify address");
    STEP("ScanAddressView");
    scan_text(address);
}

/* tests/test_flows_tools.py: test__verify_address__singlesig__flow */
static void test__verify_address__singlesig__flow(void)
{
    printf("== test__verify_address__singlesig__flow\n");
    static const struct { const char *addr; const char *want; } cases[] = {
        /* Native segwit regtest receive address at index 6 */
        { "bcrt1q4e9q5taxnsvc6m0uxv6h75mkzvnkxeqk6l90u2", "verified 6 receive" },
        /* Taproot regtest change address at index 48 */
        { "bcrt1pj5v8ean2hc5lh2djsgfx4j9uc0n67942ngv6q9r49qv88ex5mrwsn3u4f7", "verified 48 change" },
    };
    for (size_t i = 0; i < N(cases); i++) {
        fresh();
        notes_reset();
        load_seed(ABANDON, "");
        SETTINGS.network = BTC_NET_REGTEST;
        verify(cases[i].addr);
        STEP("SeedSelectSeedView");     /* AddressVerificationStartView redirected */
        choose_index(0);
        STEP("SeedAddressVerificationView");
        search();
        STEP("SeedAddressVerificationSuccessView");
        char want[160];
        snprintf(want, sizeof want, "%s %s", cases[i].want, cases[i].addr);
        CHECK(noted(want), "no \"%s\" (last: %s)", want, last_note);
        choose("OK");
        STEP("MainMenuView");
    }
}

static void test_verify_address_routes(void)
{
    printf("== verify address: seed loaded mid-flow, Skip 10, Cancel, wrong type, multisig\n");
    /* No seed yet: load one by scanning, and the search resumes with it. */
    fresh();
    notes_reset();
    verify("bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu");
    STEP("SeedSelectSeedView");
    choose("Scan a seed");
    STEP("ScanView");
    scan_text("000000000000000000000000000000000000000000000003");
    STEP("SeedFinalizeView");
    choose("Done");
    STEP("SeedAddressVerificationView");
    search();
    CHECK(noted("verified 0 receive bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu"), "BIP-84's first address");

    /* Skip 10 jumps the search ahead. */
    fresh();
    notes_reset();
    load_seed(ABANDON, "");
    const vec_tools_explorer_t *v = &VEC_TOOLS_EXPLORER[0];     /* abandon, mainnet, native, receive */
    verify(v->addresses[15]);
    choose_index(0);
    STEP("SeedAddressVerificationView");
    key(IN_OK);                         /* Skip 10, before any search */
    search();
    char want[160];
    snprintf(want, sizeof want, "verified 15 receive %s", v->addresses[15]);
    CHECK(noted("skip 10") && noted(want), "Skip 10 then index 15");

    /* Cancel ends at the main menu. */
    fresh();
    load_seed(ABANDON, "");
    verify(v->addresses[3]);
    choose_index(0);
    key(IN_DOWN);
    key(IN_OK);
    STEP("MainMenuView");

    /* ScanAddressView refuses anything but an address. */
    fresh();
    verify("000000000000000000000000000000000000000000000003");
    STEP("ErrorView");

    /* A P2WSH address is multisig: it needs a wallet descriptor (T47). */
    fresh();
    verify("bc1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3qccfmv3");
    STEP("NotYetImplementedView");

    /* Nested segwit cannot say which it is: AddressVerificationSigTypeView. */
    fresh();
    load_seed(ABANDON, "");
    verify(VEC_TOOLS_EXPLORER[2].addresses[0]);
    STEP("AddressVerificationSigTypeView");
    key(IN_BACK);
    STEP("ToolsMenuView");
}

/* Every third-or-so address of every list SeedSigner's explorer made,
   verified back to its index and branch. */
static void test_verify_every_list(void)
{
    printf("== verify addresses from every list SeedSigner's explorer made\n");
    static const int picks[] = { 0, 7, 19 };
    unsigned done = 0;
    for (size_t i = 0; i < N(VEC_TOOLS_EXPLORER); i++) {
        const vec_tools_explorer_t *v = &VEC_TOOLS_EXPLORER[i];
        for (size_t p = 0; p < N(picks); p++) {
            fresh();
            notes_reset();
            load_seed(v->mnemonic, "");
            verify(v->addresses[picks[p]]);
            if (strcmp(v->script_type, "nes") == 0) {
                STEP("AddressVerificationSigTypeView");
                choose("Single Sig");
            }
            STEP("SeedSelectSeedView");
            choose_index(0);
            search();
            char want[160];
            snprintf(want, sizeof want, "verified %d %s %s", picks[p], v->is_change ? "change" : "receive",
                     v->addresses[picks[p]]);
            CHECK(noted(want), "list %zu: no \"%s\" (last %s)", i, want, last_note);
            done++;
        }
    }
    printf("   %u addresses verified at SeedSigner's index and branch\n", done);
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
    test__address_explorer__flow();
    test__address_explorer__loadseed__sideflow();
    test__address_explorer__scan_wrong_qrtype__flow();
    test__address_explorer__back_button__flow();
    test__address_explorer__custom_derivation();
    test_every_address_list();
    test__verify_address__singlesig__flow();
    test_verify_address_routes();
    test_verify_every_list();
    fresh();
    storage_wipe_all();
    return CHECK_DONE("test_tools_flows");
}
