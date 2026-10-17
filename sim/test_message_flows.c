/*
 * SeedSigner's message signing flow tests (TestMessageSigningFlows and
 * test_back_from_seed_entry_via_seed_select in tests/test_flows_seed.py),
 * ported, and every signature SeedSigner's own code made
 * (seedsigner_message.h) produced through the screens: the request
 * scanned, the seed chosen, the message paged, the address confirmed, the
 * signature shown.
 */
#include "btc-init.h"
#include "flow.h"
#include "views-message.h"
#include "vectors/seedsigner_message.h"

#define N(a) (sizeof(a) / sizeof((a)[0]))

static const char *ABANDON = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon "
                             "abandon about";
static const char *SEEDQR = "000000000000000000000000000000000000000000000003";
static const char *MAINNET_PATH = "m/84h/0h/0h/0/0";
static const char *TESTNET_PATH = "m/84h/1h/0h/0/0";
static const char *SHORT_MESSAGE = "I attest that I control this bitcoin address blah blah blah";
static const char *MULTIPAGE_MESSAGE =
    "Chancellor on brink of second bailout for banks\n\n        Billions may be needed as lending squeeze "
    "tightens\n\n        Alistair Darling has been forced to consider a second bailout for banks as the "
    "lending drought worsens.\n\n        The Chancellor will decide within weeks whether to pump billions "
    "more into the economy as evidence mounts that the 37 billion part-nationalisation last year has failed "
    "to keep credit flowing.";

static void scan_text(const char *text)
{
    scan((const uint8_t *)text, strlen(text));
}

static void scan_request(const char *path, const char *message)
{
    char buf[2400];
    snprintf(buf, sizeof buf, "signmessage %s ascii:%s", path, message);
    scan_text(buf);
}

static void choose_index(int index)
{
    for (int i = 0; i < 40; i++) key(IN_UP);
    for (int i = 0; i < index; i++) key(IN_DOWN);
    key(IN_OK);
}

/* The message pages, then the address: OK until the address view. */
static int read_message(void)
{
    int pages = 0;
    while (strcmp(on(), "SeedSignMessageConfirmMessageView") == 0 && pages < 200) {
        key(IN_OK);
        pages++;
    }
    return pages;
}

static void test_sign_message_flow(void)
{
    printf("== test_sign_message_flow\n");
    /* Scenario 1: the message first, then the seed. */
    fresh();
    notes_reset();
    SETTINGS.message_signing = true;
    choose("Scan");
    STEP("ScanView");
    scan_request(MAINNET_PATH, SHORT_MESSAGE);
    STEP("SeedSelectSeedView");     /* SeedSignMessageStartView redirected */
    choose("Scan a seed");
    STEP("ScanView");
    scan_text(SEEDQR);
    STEP("SeedFinalizeView");
    choose("Done");
    STEP("SeedSignMessageConfirmMessageView");      /* SeedOptionsView redirected */
    key(IN_OK);
    STEP("SeedSignMessageConfirmAddressView");
    CHECK(noted("message-address bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu"), "address: %s", last_note);
    choose("Sign message");
    STEP("SeedSignMessageSignedMessageQRView");
    key(IN_OK);
    STEP("MainMenuView");

    /* Scenario 2: the seed first, then Sign message from its options. */
    storage_discard(0);
    choose("Scan");
    scan_text(SEEDQR);
    choose("Done");
    STEP("SeedOptionsView");
    choose("Sign message");
    STEP("ScanView");
    scan_request(MAINNET_PATH, SHORT_MESSAGE);
    STEP("SeedSignMessageConfirmMessageView");
    key(IN_OK);
    STEP("SeedSignMessageConfirmAddressView");
    choose("Sign message");
    STEP("SeedSignMessageSignedMessageQRView");
    key(IN_OK);
    STEP("MainMenuView");

    /* Scenario 3: a long message, paged; BACK pages back from the address. */
    choose("Scan");
    scan_request(MAINNET_PATH, MULTIPAGE_MESSAGE);
    STEP("SeedSelectSeedView");
    choose_index(0);
    STEP("SeedSignMessageConfirmMessageView");
    const int pages = read_message();
    CHECK(pages > 1, "%d page", pages);
    STEP("SeedSignMessageConfirmAddressView");
    key(IN_BACK);
    STEP("SeedSignMessageConfirmMessageView");
    for (int i = 1; i < pages; i++) key(IN_BACK);
    char want[32];
    snprintf(want, sizeof want, "message page 1/%d", pages);
    CHECK(strcmp(last_note, want) == 0, "back to the first page: %s", last_note);
    CHECK(read_message() == pages, "forward again");
    choose("Sign message");
    STEP("SeedSignMessageSignedMessageQRView");
    key(IN_OK);
    STEP("MainMenuView");
}

static void test_sign_message_network_mismatch_flow(void)
{
    printf("== test_sign_message_network_mismatch_flow\n");
    static const struct { btc_net_t net; const char *path; } cases[] = {
        { BTC_NET_MAIN, "m/84h/1h/0h/0/0" }, { BTC_NET_TEST, "m/84h/0h/0h/0/0" },
        { BTC_NET_REGTEST, "m/84h/0h/0h/0/0" },
    };
    for (size_t i = 0; i < N(cases); i++) {
        fresh();
        SETTINGS.message_signing = true;
        SETTINGS.network = cases[i].net;
        choose("Scan");
        scan_request(cases[i].path, SHORT_MESSAGE);
        STEP("NetworkMismatchErrorView");
        key(IN_BACK);
        STEP("NetworkMismatchErrorView");          /* no way back */
        choose("Change Setting");
        STEP("SettingsEntryUpdateSelectionView");
        choose(cases[i].net == BTC_NET_MAIN ? "Testnet" : "Mainnet");
        key(IN_BACK);
        STEP("SettingsMenuView");                   /* Advanced */
        key(IN_BACK);
        STEP("SettingsMenuView");
        key(IN_BACK);
        STEP("MainMenuView");
    }
    (void)TESTNET_PATH;
}

static void test_sign_message_option_disabled(void)
{
    printf("== test_sign_message_option_disabled\n");
    fresh();
    choose("Scan");
    scan_request(MAINNET_PATH, SHORT_MESSAGE);
    STEP("OptionDisabledView");
    choose("Update setting");
    STEP("SettingsEntryUpdateSelectionView");
    choose("Enabled");
    CHECK(SETTINGS.message_signing, "not enabled");

    fresh();
    choose("Scan");
    scan_request(MAINNET_PATH, SHORT_MESSAGE);
    STEP("OptionDisabledView");
    choose("Back to Main Menu");
    STEP("MainMenuView");
}

static void test_sign_message_invalid_qr_flow(void)
{
    printf("== test_sign_message_invalid_qr_flow\n");
    fresh();
    SETTINGS.message_signing = true;
    choose("Scan");
    scan_text(SEEDQR);
    choose("Done");
    STEP("SeedOptionsView");
    choose("Sign message");
    STEP("ScanView");
    scan_text("this text will not make sense to the decoder");
    STEP("ScanInvalidQRTypeView");
    key(IN_OK);
    STEP("MainMenuView");
    /* The flow is dropped: the next seed loaded goes to its options. */
    storage_discard(0);
    choose("Scan");
    scan_text(SEEDQR);
    choose("Done");
    STEP("SeedOptionsView");
}

static void test_custom_derivation(void)
{
    printf("== custom and multisig paths are refused\n");
    static const char *const paths[] = { "m/99h/0/0", "m/48h/0h/0h/2h/0/0", "m/84h/0h/0h" };
    for (size_t i = 0; i < N(paths); i++) {
        fresh();
        SETTINGS.message_signing = true;
        choose("Scan");
        scan_request(paths[i], SHORT_MESSAGE);
        STEP("NotYetImplementedView");
    }
}

/* tests/test_flows_seed.py: test_back_from_seed_entry_via_seed_select */
static void test_back_from_seed_entry_via_seed_select(void)
{
    printf("== test_back_from_seed_entry_via_seed_select\n");
    fresh();
    SETTINGS.message_signing = true;
    choose("Scan");
    scan_text("signmessage m/84h/0h/0h/0/0 ascii:test message");
    STEP("SeedSelectSeedView");
    choose("Enter 12-word seed");
    STEP("SeedMnemonicEntryView");
    key(IN_BACK);               /* BACK on the first word */
    STEP("SeedSelectSeedView");
    /* Still in the flow: a seed typed now goes on to the message. */
    choose("Enter 12-word seed");
    type_mnemonic(ABANDON);
    choose("Done");
    STEP("SeedSignMessageConfirmMessageView");
}

/* Every case SeedSigner's own code signed, through the screens. */
static void test_every_signature(void)
{
    printf("== every signature SeedSigner's code made, through the screens\n");
    unsigned n = 0;
    for (size_t i = 0; i < N(VEC_MSG_SIGNS); i++) {
        const vec_msg_sign_t *v = &VEC_MSG_SIGNS[i];
        fresh();
        notes_reset();
        SETTINGS.message_signing = true;
        SETTINGS.network = v->net;
        seed_t s;
        seed_from_mnemonic(&s, v->mnemonic, "");
        storage_set_pending(&s);
        seed_clear(&s);
        storage_finalize();
        choose("Scan");
        scan_request(v->path, v->message);
        STEP("SeedSelectSeedView");
        choose_index(0);
        read_message();
        STEP("SeedSignMessageConfirmAddressView");
        char want[200];
        snprintf(want, sizeof want, "message-address %s", v->address);
        CHECK(noted(want), "%zu: no \"%s\"", i, want);
        choose("Sign message");
        snprintf(want, sizeof want, "message-signed %s", v->signature);
        CHECK(noted(want), "%zu: not SeedSigner's signature (%s)", i, last_note);
        key(IN_OK);
        n++;
    }
    printf("   %u requests signed as SeedSigner signs them\n", n);
}

int main(void)
{
    btc_init(rand_fill);
    ui_set_clock(clock_fn);
    ui_set_note_hook(note);
    test_sign_message_flow();
    test_sign_message_network_mismatch_flow();
    test_sign_message_option_disabled();
    test_sign_message_invalid_qr_flow();
    test_custom_derivation();
    test_back_from_seed_entry_via_seed_select();
    test_every_signature();
    fresh();
    return CHECK_DONE("test_message_flows");
}
