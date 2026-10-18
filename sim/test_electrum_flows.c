/*
 * SeedSigner's Electrum seed flows, ported: tests/test_flows_seed.py
 * test_electrum_mnemonic_entry_flow and test_export_xpub_electrum_seed_flow,
 * and tests/test_flows_tools.py
 * test__address_explorer__load_electrum_seed__sideflow (its PSBT flow is in
 * test_psbt_flows.c), driven through the device's screens, with what
 * SeedSigner's ElectrumSeed gives (seedsigner_electrum.h).
 *
 * Then what SeedSigner turns off or overrides for an Electrum seed, each
 * reached as a user would: no BIP-85 child seeds, no SeedQR backup, the
 * custom extension's label and normalisation, address verification at
 * m/0h, and a message signed with one loaded mid-flow.
 */
#include "btc-init.h"
#include "flow.h"
#include "vectors/seedsigner_electrum.h"
#include "wally_core.h"

#define N(a) (sizeof(a) / sizeof((a)[0]))

static const char *REGULAR = "regular reject rare profit once math fringe chase until ketchup century escape";

static void scan_text(const char *text)
{
    scan((const uint8_t *)text, strlen(text));
}

/* Is there a button labelled `label` on the current view? */
static bool has_button(const char *label)
{
    for (int i = 0; i < 40; i++) key(IN_UP);
    for (int i = 0; i < 40; i++) {
        const char *sel = ui_selected_label();
        if (sel && strcmp(sel, label) == 0) {
            return true;
        }
        key(IN_DOWN);
    }
    return false;
}

static const vec_electrum_seed_t *seed_vec(const char *mnemonic, const char *ext)
{
    for (size_t i = 0; i < N(VEC_ELECTRUM_SEEDS); i++) {
        const vec_electrum_seed_t *v = &VEC_ELECTRUM_SEEDS[i];
        if (strcmp(v->mnemonic, mnemonic) == 0 && (v->error || strcmp(v->extension, ext) == 0)) {
            return v;
        }
    }
    CHECK(0, "no vector for \"%s\" \"%s\"", mnemonic, ext);
    return NULL;
}

static const vec_electrum_address_t *address_vec(const char *mnemonic, btc_net_t net, btc_script_t script,
                                                 bool change, unsigned index)
{
    for (size_t i = 0; i < N(VEC_ELECTRUM_ADDRESSES); i++) {
        const vec_electrum_address_t *a = &VEC_ELECTRUM_ADDRESSES[i];
        if (strcmp(a->mnemonic, mnemonic) == 0 && a->extension[0] == '\0' && a->net == net &&
            a->script == script && a->change == change && a->index == index) {
            return a;
        }
    }
    CHECK(0, "no address vector");
    return NULL;
}

static void load_electrum(const char *mnemonic)
{
    seed_t s;
    uint16_t idx[SEED_MAX_WORDS];
    char buf[160];
    snprintf(buf, sizeof buf, "%s", mnemonic);
    size_t n = 0;
    for (char *w = strtok(buf, " "); w && n < SEED_MAX_WORDS; w = strtok(NULL, " ")) {
        idx[n++] = (uint16_t)bip39_word_index(w);
    }
    CHECK(seed_from_indices_electrum(&s, idx, n, "") == SEED_OK, "seed");
    storage_set_pending(&s);
    seed_clear(&s);
    storage_finalize();
}

/* SeedElectrumMnemonicStartView, then the words. */
static void enter_electrum(const char *mnemonic)
{
    STEP("SeedElectrumMnemonicStartView");
    CHECK(!has_button("Done"), "the warning has more than \"I understand\"");
    choose("I understand");
    STEP("SeedMnemonicEntryView");
    CHECK(storage_mnemonic_is_electrum() && storage_mnemonic_len() == 12, "not a 12-word Electrum entry");
    type_mnemonic(mnemonic);
}

/* ---- tests/test_flows_seed.py ------------------------------------------- */

static void test_with_mnemonic(const char *mnemonic, const char *custom_extension, bool valid)
{
    fresh();
    notes_reset();
    SETTINGS.electrum_seeds = true;
    choose("Seeds");
    STEP("LoadSeedView");   /* SeedsMenuView redirects with no seed loaded */
    choose("Enter Electrum seed");
    enter_electrum(mnemonic);
    if (!valid) {
        STEP("SeedMnemonicInvalidView");
        choose("Discard");
        STEP("MainMenuView");
        CHECK(storage_count() == 0 && storage_pending() == NULL, "an invalid seed was kept");
        CHECK(!storage_mnemonic_is_electrum(), "the Electrum entry outlived Discard");
        return;
    }
    STEP("SeedFinalizeView");
    const vec_electrum_seed_t *bare = seed_vec(mnemonic, "");
    char want[48];
    snprintf(want, sizeof want, "fingerprint %s", bare ? bare->fingerprint : "");
    CHECK(noted(want), "no \"%s\"", want);
    if (custom_extension) {
        CHECK(!has_button("BIP-39 Passphrase"), "Electrum's finalize offers a BIP-39 passphrase");
        choose("Custom Extension");     /* screen_return_value=1 */
        STEP("SeedAddPassphraseView");
        type_passphrase(custom_extension, true);
        STEP("SeedReviewPassphraseView");
        choose("Done");
    } else {
        choose("Done");
    }
    STEP("SeedOptionsView");
    const seed_t *s = storage_seed(0);
    const vec_electrum_seed_t *v = seed_vec(mnemonic, custom_extension ? custom_extension : "");
    CHECK(s && s->electrum, "not loaded as an Electrum seed");
    if (s && v) {
        char fp[9];
        seed_fingerprint_hex(s, fp);
        CHECK(strcmp(fp, v->fingerprint) == 0, "fingerprint %s, SeedSigner %s", fp, v->fingerprint);
        CHECK(strcmp(s->passphrase, v->normalized) == 0, "extension \"%s\", SeedSigner \"%s\"", s->passphrase,
              v->normalized);
    }
}

static void test_electrum_mnemonic_entry_flow(void)
{
    printf("== test_electrum_mnemonic_entry_flow\n");
    /* Seeds generated by Electrum v4.5.5. */
    test_with_mnemonic("bomb congress scorpion mutual word stamp tongue valid permit salmon yellow spy", NULL, true);
    test_with_mnemonic("morning pretty hobby click extend color wait joke define sausage boost salmon", NULL, true);
    test_with_mnemonic("basket print toy noodle betray weird filter ticket insect copy force machine", NULL, true);
    test_with_mnemonic("basket print toy noodle betray weird filter ticket insect copy force machine", "test", true);
    test_with_mnemonic("basket print toy noodle betray weird filter ticket insect copy force machine",
                       "monkey fling orange coin good", true);
    /* Most BIP-39 seeds fail. */
    test_with_mnemonic("pioneer divide volcano art victory family grow novel mandate bicycle senior adjust", NULL,
                       false);
}

static void test_export_xpub_electrum_seed_flow(void)
{
    printf("== test_export_xpub_electrum_seed_flow\n");
    static const char *sigs[] = { "Single Sig", "Multisig" };
    static const char *formats[] = { "Static", "Specter legacy", "Animated (default)" };
    for (int a = 0; a < 2; a++)
        for (int f = 0; f < 3; f++) {
            fresh();
            notes_reset();
            /* Every option enabled: the script type is still skipped. */
            SETTINGS.sig_single = SETTINGS.sig_multi = true;
            for (int t = 0; t < SCRIPT_COUNT; t++) SETTINGS.script_types[t] = true;
            for (int q = 0; q < XPUB_QR_COUNT; q++) SETTINGS.xpub_qr_format[q] = true;
            load_electrum(REGULAR);
            SeedOptionsView(0);
            STEP("SeedOptionsView");
            choose("Export xpub");
            STEP("SeedExportXpubSigTypeView");
            choose(sigs[a]);
            STEP("SeedExportXpubQRFormatView");     /* SeedExportXpubScriptTypeView redirected */
            choose(formats[f]);
            STEP("SeedExportXpubWarningView");
            choose("I understand");
            STEP("SeedExportXpubDetailsView");
            /* What SeedSigner exports: its static xpub's key, at its path. */
            const btc_sig_t sig = a ? BTC_SIG_MULTI : BTC_SIG_SINGLE;
            for (size_t i = 0; i < N(VEC_ELECTRUM_XPUBS); i++) {
                const vec_electrum_xpub_t *x = &VEC_ELECTRUM_XPUBS[i];
                if (strcmp(x->mnemonic, REGULAR) == 0 && x->extension[0] == '\0' && x->sig == sig &&
                    x->net == BTC_NET_MAIN) {
                    char want[160];
                    snprintf(want, sizeof want, "xpub %s %s", x->path, strchr(x->xpub_static, ']') + 1);
                    CHECK(noted(want), "no \"%s\"", want);
                }
            }
            choose("Export Xpub");
            STEP("SeedExportXpubQRDisplayView");
            key(IN_OK);
            STEP("MainMenuView");
        }
}

/* ---- tests/test_flows_tools.py ------------------------------------------ */

static void explorer_list(bool change)
{
    for (unsigned i = 0; i < 10; i++) {
        const vec_electrum_address_t *a = address_vec(REGULAR, BTC_NET_MAIN, BTC_SCRIPT_P2WPKH, change, i);
        char want[120];
        snprintf(want, sizeof want, "addr %u %s", i, a ? a->address : "");
        CHECK(noted(want), "no \"%s\"", want);
    }
}

static void test__address_explorer__load_electrum_seed__sideflow(void)
{
    printf("== test__address_explorer__load_electrum_seed__sideflow\n");
    for (int change = 0; change < 2; change++) {
        fresh();
        notes_reset();
        SETTINGS.electrum_seeds = true;
        for (int t = 0; t < SCRIPT_COUNT; t++) SETTINGS.script_types[t] = true;
        choose("Tools");
        STEP("ToolsMenuView");
        choose("Address explorer");
        STEP("ToolsAddressExplorerSelectSourceView");
        choose("Enter Electrum seed");
        enter_electrum(REGULAR);
        STEP("SeedFinalizeView");
        choose("Done");
        /* SeedOptionsView and SeedExportXpubScriptTypeView redirect. */
        STEP("ToolsAddressExplorerAddressTypeView");
        char want[64], fp[9];
        seed_fingerprint_hex(storage_seed(0), fp);
        snprintf(want, sizeof want, "explorer %s m/0h", fp);
        CHECK(noted(want), "no \"%s\"", want);
        choose(change ? "Change addresses" : "Receive addresses");
        STEP("ToolsAddressExplorerAddressListView");
        explorer_list(change);
    }
}

/* ---- What an Electrum seed turns off ------------------------------------ */

static void test_disabled_features(void)
{
    printf("== no BIP-85 child seeds, no SeedQR backup\n");
    fresh();
    SETTINGS.bip85_child_seeds = true;
    load_electrum(REGULAR);
    SeedOptionsView(0);
    STEP("SeedOptionsView");
    CHECK(!has_button("BIP-85 child seed"), "BIP-85 offered for an Electrum seed");
    choose("Backup seed");
    STEP("SeedBackupView");
    CHECK(has_button("View seed words"), "no words to back up");
    CHECK(!has_button("Export as SeedQR"), "SeedQR offered for an Electrum seed");
    choose("View seed words");
    STEP("SeedWordsWarningView");

    /* A BIP-39 seed still has both. */
    fresh();
    SETTINGS.bip85_child_seeds = true;
    seed_t s;
    CHECK(seed_from_mnemonic(&s, "abandon abandon abandon abandon abandon abandon abandon abandon abandon "
                                 "abandon abandon about", "") == SEED_OK, "seed");
    storage_set_pending(&s);
    seed_clear(&s);
    storage_finalize();
    SeedOptionsView(0);
    CHECK(has_button("BIP-85 child seed"), "BIP-85 missing for a BIP-39 seed");

    /* LoadSeedView offers Electrum entry only when enabled, before "Create a seed". */
    fresh();
    choose("Seeds");
    STEP("LoadSeedView");
    CHECK(!has_button("Enter Electrum seed"), "Electrum entry offered while disabled");
    fresh();
    SETTINGS.electrum_seeds = true;
    choose("Seeds");
    STEP("LoadSeedView");
    choose("Enter Electrum seed");
    STEP("SeedElectrumMnemonicStartView");
    /* BACK at the first word returns to the warning, which has no BACK. */
    choose("I understand");
    STEP("SeedMnemonicEntryView");
    key(IN_BACK);
    STEP("SeedElectrumMnemonicStartView");
    CHECK(!storage_mnemonic_is_electrum(), "the entry outlived BACK");
    key(IN_BACK);
    STEP("SeedElectrumMnemonicStartView");
}

static void test_custom_extension(void)
{
    printf("== the custom extension: label and normalisation\n");
    fresh();
    SETTINGS.electrum_seeds = true;
    choose("Seeds");
    choose("Enter Electrum seed");
    enter_electrum(REGULAR);
    STEP("SeedFinalizeView");
    choose("Custom Extension");
    STEP("SeedAddPassphraseView");
    /* Spaces only: nothing once normalised, so SeedSigner asks to skip. */
    type_passphrase("   ", true);
    STEP("SeedAddPassphraseExitDialogView");
    CHECK(has_button("Skip passphrase"), "spaces were kept as an extension");
    choose("Edit passphrase");
    STEP("SeedAddPassphraseView");
    type_passphrase("  Monkey   FLING orange  coin good ", true);
    STEP("SeedReviewPassphraseView");
    const vec_electrum_seed_t *v = seed_vec(REGULAR, "  Monkey   FLING orange  coin good ");
    char want[48];
    seed_t *p = storage_pending();
    CHECK(p && v && strcmp(p->passphrase, v->normalized) == 0, "extension \"%s\"", p ? p->passphrase : "");
    snprintf(want, sizeof want, "passphrase %s %s", seed_vec(REGULAR, "")->fingerprint, v ? v->fingerprint : "");
    CHECK(noted(want), "no \"%s\"", want);
    choose("Done");
    STEP("SeedOptionsView");
}

static void test_verify_address(void)
{
    printf("== address verification with an Electrum seed loaded mid-flow\n");
    const struct { btc_script_t script; bool change; unsigned index; } cases[] = {
        { BTC_SCRIPT_P2WPKH, false, 7 }, { BTC_SCRIPT_P2WPKH, true, 11 }, { BTC_SCRIPT_P2PKH, false, 2 },
        { BTC_SCRIPT_P2TR, true, 1 },
    };
    for (size_t c = 0; c < N(cases); c++) {
        const vec_electrum_address_t *a = address_vec(REGULAR, BTC_NET_MAIN, cases[c].script, cases[c].change,
                                                      cases[c].index);
        if (!a) continue;
        fresh();
        notes_reset();
        SETTINGS.electrum_seeds = true;
        choose("Scan");
        scan_text(a->address);
        STEP("SeedSelectSeedView");
        choose("Enter Electrum seed");
        enter_electrum(REGULAR);
        STEP("SeedFinalizeView");
        choose("Done");
        STEP("SeedAddressVerificationView");
        char want[160];
        snprintf(want, sizeof want, "verify m/0h %s", a->address);
        CHECK(noted(want), "no \"%s\"", want);
        for (int i = 0; i < 400 && strcmp(on(), "SeedAddressVerificationView") == 0; i++) {
            ui_tick();
            ui_render();
        }
        STEP("SeedAddressVerificationSuccessView");
        snprintf(want, sizeof want, "verified %u %s %s", cases[c].index, cases[c].change ? "change" : "receive",
                 a->address);
        CHECK(noted(want), "no \"%s\"", want);
    }
}

static void test_sign_message(void)
{
    printf("== a message signed with an Electrum seed loaded mid-flow\n");
    const vec_electrum_sign_t *v = NULL;
    for (size_t i = 0; i < N(VEC_ELECTRUM_SIGNS); i++) {
        if (strcmp(VEC_ELECTRUM_SIGNS[i].mnemonic, REGULAR) == 0 &&
            strcmp(VEC_ELECTRUM_SIGNS[i].path, "m/84h/0h/0h/0/0") == 0 &&
            strcmp(VEC_ELECTRUM_SIGNS[i].message, "hello world") == 0) {
            v = &VEC_ELECTRUM_SIGNS[i];
        }
    }
    CHECK(v, "no signature vector");
    if (!v) return;
    fresh();
    notes_reset();
    SETTINGS.electrum_seeds = true;
    SETTINGS.message_signing = true;
    choose("Scan");
    scan_text("signmessage m/84h/0h/0h/0/0 ascii:hello world");
    STEP("SeedSelectSeedView");
    choose("Enter Electrum seed");
    enter_electrum(REGULAR);
    STEP("SeedFinalizeView");
    choose("Done");
    STEP("SeedSignMessageConfirmMessageView");
    key(IN_OK);
    STEP("SeedSignMessageConfirmAddressView");
    choose("Sign message");
    STEP("SeedSignMessageSignedMessageQRView");
    char want[140];
    snprintf(want, sizeof want, "message-signed %s", v->signature);
    CHECK(noted(want), "no \"%s\"", want);
}

int main(void)
{
    btc_init(rand_fill);
    ui_set_clock(clock_fn);
    ui_set_note_hook(note);
    test_electrum_mnemonic_entry_flow();
    test_export_xpub_electrum_seed_flow();
    test__address_explorer__load_electrum_seed__sideflow();
    test_disabled_features();
    test_custom_extension();
    test_verify_address();
    test_sign_message();
    fresh();
    storage_wipe_all();
    return CHECK_DONE("test_electrum_flows");
}
