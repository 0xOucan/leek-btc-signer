/*
 * SeedSigner's seed flow tests (tests/test_flows_seed.py), ported: the same
 * sequence of Views, driven through the device's own screens with the keys a
 * user would press. Each step names the SeedSigner View the device must be on.
 *
 * Where SeedSigner's test injects a screen's return value (a typed word, a
 * passphrase), this presses the keys that produce it, so the entry screens
 * are tested too.
 */
#include "btc-init.h"
#include "flow.h"

/* abandon x11 about: CompactSeedQR all zero bytes, fingerprint 73c5da0a. */
static const uint8_t ZERO16[16];

static void test_scan_seedqr_flow(void)
{
    printf("== test_scan_seedqr_flow\n");
    fresh();
    STEP("MainMenuView");
    choose("Scan");
    STEP("ScanView");
    scan(ZERO16, sizeof ZERO16);
    STEP("SeedFinalizeView");
    CHECK(strcmp(last_note, "fingerprint 73c5da0a") == 0, "note %s", last_note);
    choose("Done");
    STEP("SeedOptionsView");
    CHECK(storage_count() == 1, "the seed was not stored");
    key(IN_BACK);
    STEP("MainMenuView");       /* clear_history: BACK goes home */
}

static void test_passphrase_entry_flow(void)
{
    printf("== test_passphrase_entry_flow\n");
    fresh();
    choose("Scan");
    STEP("ScanView");
    scan(ZERO16, sizeof ZERO16);
    STEP("SeedFinalizeView");
    choose("BIP-39 Passphrase");
    STEP("SeedAddPassphraseView");
    type_passphrase("muhpassphrase", false);
    key(IN_BACK);                                   /* is_back_button */
    STEP("SeedAddPassphraseExitDialogView");
    choose("Discard passphrase");
    STEP("SeedFinalizeView");
    choose("BIP-39 Passphrase");
    STEP("SeedAddPassphraseView");
    type_passphrase("muhpassphrase", false);
    key(IN_BACK);
    STEP("SeedAddPassphraseExitDialogView");
    choose("Edit passphrase");
    STEP("SeedAddPassphraseView");
    type_passphrase("", true);                      /* it kept the text: just OK */
    STEP("SeedReviewPassphraseView");
    choose("Edit passphrase");
    STEP("SeedAddPassphraseView");
    type_passphrase("", true);
    STEP("SeedReviewPassphraseView");
    CHECK(strncmp(last_note, "passphrase 73c5da0a ", 20) == 0, "note %s", last_note);
    choose("Done");
    STEP("SeedOptionsView");
    CHECK(storage_count() == 1 && strcmp(storage_seed(0)->passphrase, "muhpassphrase") == 0,
          "the passphrase was not kept");

    /* Skipping: leave with nothing entered. */
    fresh();
    choose("Scan");
    scan(ZERO16, sizeof ZERO16);
    choose("BIP-39 Passphrase");
    key(IN_BACK);
    STEP("SeedAddPassphraseExitDialogView");
    choose("Skip passphrase");
    STEP("SeedFinalizeView");
}

static void test_mnemonic_entry_flow(void)
{
    printf("== test_mnemonic_entry_flow\n");
    const char *m[2] = {
        "tone flat shed cool census soul paddle boy flight fantasy stem social",
        "cotton artefact spy mind wing there echo steak child oak awful host despair online "
        "bicycle divorce middle firm diamond rare execute chimney almost hollow",
    };
    for (int k = 0; k < 2; k++) {
        fresh();
        choose("Seeds");
        STEP("LoadSeedView");       /* SeedsMenuView redirects with no seeds */
        choose(k == 0 ? "Enter 12-word seed" : "Enter 24-word seed");
        STEP("SeedMnemonicEntryView");
        type_mnemonic(m[k]);
        STEP("SeedFinalizeView");
        choose("Done");
        STEP("SeedOptionsView");
        seed_t ref;
        CHECK(seed_from_mnemonic(&ref, m[k], "") == SEED_OK &&
              memcmp(ref.fingerprint, storage_seed(0)->fingerprint, 4) == 0, "wrong seed stored");
        seed_clear(&ref);
    }
}

static void test_invalid_mnemonic(void)
{
    printf("== test_invalid_mnemonic\n");
    fresh();
    choose("Seeds");
    choose("Enter 12-word seed");
    type_mnemonic("blush twice taste dawn feed second opinion lazy thumb play neglect zoo");
    STEP("SeedMnemonicInvalidView");
    choose("Review & edit");
    STEP("SeedMnemonicEntryView");
    type_mnemonic("blush twice taste dawn feed second opinion lazy thumb play neglect zebra");
    STEP("SeedMnemonicInvalidView");
    choose("Discard");
    STEP("MainMenuView");
    CHECK(storage_count() == 0 && storage_mnemonic_len() == 0, "the mnemonic was not discarded");
}

static void load_blush(void)
{
    seed_t s;
    seed_from_mnemonic(&s, "blush twice taste dawn feed second opinion lazy thumb play neglect impact", "");
    storage_set_pending(&s);
    seed_clear(&s);
    storage_finalize();
}

static void test_export_xpub_standard_flow(void)
{
    printf("== test_export_xpub_standard_flow\n");
    static const char *sigs[] = { "Single Sig", "Multisig" };
    static const char *scripts[] = { "Native Segwit", "Nested Segwit", "Legacy", "Taproot" };
    static const char *formats[] = { "Animated (default)", "Static", "Specter legacy" };
    int runs = 0;
    for (int a = 0; a < 2; a++)
        for (int b = 0; b < 4; b++)
            for (int c = 0; c < 3; c++) {
                if (a == 1 && b == 3) continue;     /* multisig taproot, as SeedSigner skips */
                fresh();
                for (int t = 0; t < SCRIPT_COUNT; t++) SETTINGS.script_types[t] = true;
                for (int f = 0; f < XPUB_QR_COUNT; f++) SETTINGS.xpub_qr_format[f] = true;
                load_blush();
                SeedOptionsView(0);
                STEP("SeedOptionsView");
                choose("Export xpub");
                STEP("SeedExportXpubSigTypeView");
                choose(sigs[a]);
                STEP("SeedExportXpubScriptTypeView");
                choose(scripts[b]);
                STEP("SeedExportXpubQRFormatView");
                choose(formats[c]);
                STEP("SeedExportXpubWarningView");
                choose("I understand");
                STEP("SeedExportXpubDetailsView");
                choose("Export Xpub");
                STEP("SeedExportXpubQRDisplayView");
                key(IN_OK);
                STEP("MainMenuView");
                runs++;
            }
    printf("   %d sig/script/format combinations\n", runs);
}

static void test_discard_seed_flow(void)
{
    printf("== test_discard_seed_flow\n");
    fresh();
    load_blush();
    SeedOptionsView(0);
    choose("Discard seed");
    STEP("SeedDiscardView");
    choose("Discard");
    STEP("MainMenuView");
    CHECK(storage_count() == 0, "the seed is still held");
    choose("Seeds");
    STEP("LoadSeedView");
}

static void test_seeds_menu_and_backup(void)
{
    printf("== seeds menu, backup words\n");
    fresh();
    load_blush();
    choose("Seeds");
    STEP("SeedsMenuView");
    choose("Load a seed");
    STEP("LoadSeedView");
    key(IN_BACK);
    STEP("SeedsMenuView");
    key(IN_UP);
    key(IN_OK);
    STEP("SeedOptionsView");
    choose("Backup seed");
    STEP("SeedBackupView");
    choose("View seed words");
    STEP("SeedWordsWarningView");
    choose("I understand");
    STEP("SeedWordsView");
    key(IN_OK);
    CHECK(strcmp(last_note, "words page 2") == 0, "note %s", last_note);
    key(IN_BACK);
    STEP("SeedWordsView");
    key(IN_BACK);
    STEP("SeedBackupView");     /* the warning was skipped on the way back */
    choose("View seed words");
    choose("I understand");
    for (int i = 0; i < 3; i++) key(IN_OK);
    STEP("NotYetImplementedView");
}

int main(void)
{
    btc_init(rand_fill);
    ui_set_clock(clock_fn);
    ui_set_note_hook(note);
    test_scan_seedqr_flow();
    test_passphrase_entry_flow();
    test_mnemonic_entry_flow();
    test_invalid_mnemonic();
    test_export_xpub_standard_flow();
    test_discard_seed_flow();
    test_seeds_menu_and_backup();
    storage_wipe_all();
    return CHECK_DONE("test_seed_flows");
}
