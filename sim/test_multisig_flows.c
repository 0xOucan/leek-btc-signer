/*
 * SeedSigner's multisig flows, ported: its tests/test_flows_tools.py
 * test__address_explorer__legacy_multisig_p2sh__flow and
 * test__verify_address__legacy_multisig_p2sh__flow, driven through the
 * device's screens, with what SeedSigner's own functions give for their
 * descriptor and address (seedsigner_multisig.h, VEC_MS_FLOW_*).
 *
 * Then the routes around the descriptor views (OK, BACK, Cancel, a scan of
 * the wrong kind, a descriptor that does not parse), and every
 * verify_multisig_output() SeedSigner's tests made, as the PSBT review's
 * "Verify multisig change" reaches it: the descriptor scanned mid-review,
 * "Return to transaction", then the change verified or the transaction
 * refused, as SeedSigner's PSBTChangeDetailsView routes.
 */
#include "btc-init.h"
#include "flow.h"
#include "vectors/seedsigner_multisig.h"
#include "wally_core.h"

#define N(a) (sizeof(a) / sizeof((a)[0]))

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

static void load_seed(const char *mnemonic, const char *passphrase)
{
    seed_t s;
    CHECK(seed_from_mnemonic(&s, mnemonic, passphrase) == SEED_OK, "seed");
    storage_set_pending(&s);
    seed_clear(&s);
    storage_finalize();
}

static void search(void)
{
    for (int i = 0; i < 400 && strcmp(on(), "SeedAddressVerificationView") == 0; i++) {
        ui_tick();
        ui_render();
    }
}

static void check_label(const char *want)
{
    const char *sel = ui_selected_label();
    CHECK(sel && strcmp(sel, want) == 0, "button \"%s\", want \"%s\"", sel ? sel : "-", want);
}

/* The ten addresses of a list page, as noted. */
static void check_page(const char *const *want, int start)
{
    for (int k = 0; k < 10; k++) {
        char line[160];
        snprintf(line, sizeof line, "addr %d %s", start + k, want[start + k]);
        CHECK(noted(line), "no \"%s\"", line);
    }
}

/* tests/test_flows_tools.py: test__address_explorer__legacy_multisig_p2sh__flow */
static void test__address_explorer__legacy_multisig_p2sh__flow(void)
{
    printf("== test__address_explorer__legacy_multisig_p2sh__flow\n");
    fresh();
    notes_reset();
    choose("Tools");
    STEP("ToolsMenuView");
    choose("Address explorer");
    STEP("ToolsAddressExplorerSelectSourceView");
    choose("Scan wallet descriptor");
    STEP("ScanWalletDescriptorView");
    scan_text(VEC_MS_FLOW_DESCRIPTOR);
    STEP("MultisigWalletDescriptorView");
    check_label("Address explorer");
    CHECK(noted("descriptor 2 of 3"), "policy: %s", last_note);
    choose("Address explorer");
    STEP("ToolsAddressExplorerAddressTypeView");
    CHECK(noted("explorer descriptor 2 of 3"), "explorer: %s", last_note);
    choose("Receive addresses");
    STEP("ToolsAddressExplorerAddressListView");
    check_page(VEC_MS_FLOW_RECEIVE, 0);
    choose_index(10);                   /* NEXT page of addrs */
    STEP("ToolsAddressExplorerAddressListView");
    check_page(VEC_MS_FLOW_RECEIVE, 10);
    choose_index(4);                    /* a specific addr from the list */
    STEP("ToolsAddressExplorerAddressView");
    char want[160];
    snprintf(want, sizeof want, "address 14 %s", VEC_MS_FLOW_RECEIVE[14]);
    CHECK(strcmp(last_note, want) == 0, "note %s", last_note);
    key(IN_OK);
    STEP("ToolsAddressExplorerAddressListView");

    /* The change list too, back through the address type. */
    notes_reset();
    key(IN_BACK);
    key(IN_BACK);
    STEP("ToolsAddressExplorerAddressTypeView");
    choose("Change addresses");
    STEP("ToolsAddressExplorerAddressListView");
    check_page(VEC_MS_FLOW_CHANGE, 0);
    choose_index(10);
    check_page(VEC_MS_FLOW_CHANGE, 10);

    /* Back to the descriptor: the flow is over, so it offers just OK, which
       goes home and drops the descriptor. */
    key(IN_BACK);
    key(IN_BACK);
    key(IN_BACK);
    STEP("MultisigWalletDescriptorView");
    check_label("OK");
    CHECK(multisig_descriptor() != NULL, "the descriptor went too soon");
    choose("OK");
    STEP("MainMenuView");
    CHECK(multisig_descriptor() == NULL, "home kept the descriptor");
}

/* tests/test_flows_tools.py: test__verify_address__legacy_multisig_p2sh__flow */
static void test__verify_address__legacy_multisig_p2sh__flow(void)
{
    printf("== test__verify_address__legacy_multisig_p2sh__flow\n");
    fresh();
    notes_reset();
    SETTINGS.network = BTC_NET_REGTEST;
    choose("Tools");
    choose("Verify address");
    STEP("ScanAddressView");
    scan_text(VEC_MS_FLOW_ADDRESS);
    STEP("AddressVerificationSigTypeView");     /* AddressVerificationStartView redirected */
    choose("Multisig");
    STEP("LoadMultisigWalletDescriptorView");
    choose("Scan descriptor");
    STEP("ScanWalletDescriptorView");
    scan_text(VEC_MS_FLOW_DESCRIPTOR);
    STEP("MultisigWalletDescriptorView");
    char label[32];
    snprintf(label, sizeof label, "Verify addr %.7s", VEC_MS_FLOW_ADDRESS);
    check_label(label);
    choose_index(0);
    STEP("SeedAddressVerificationView");
    search();
    STEP("SeedAddressVerificationSuccessView");
    char want[160];
    snprintf(want, sizeof want, "verified %u %s %s", VEC_MS_FLOW_VERIFIED_INDEX,
             VEC_MS_FLOW_VERIFIED_CHANGE ? "change" : "receive", VEC_MS_FLOW_ADDRESS);
    CHECK(noted(want), "no \"%s\" (last: %s)", want, last_note);
    choose("OK");
    STEP("MainMenuView");
    CHECK(multisig_descriptor() == NULL, "home kept the descriptor");
}

/* A P2WSH address goes straight to LoadMultisigWalletDescriptorView, and is
   found among the descriptor's addresses (receive and change). */
static void test_verify_p2wsh(void)
{
    printf("== verify P2WSH multisig addresses against their descriptor\n");
    unsigned done = 0;
    for (size_t i = 0; i < N(VEC_MS_DESCRIPTORS); i++) {
        const vec_ms_descriptor_t *v = &VEC_MS_DESCRIPTORS[i];
        if (v->error || !v->basic || strncmp(v->descriptor, "wsh(", 4) != 0 || v->n_addresses == 0) {
            continue;
        }
        const vec_ms_addr_t *a = &v->addresses[v->n_addresses - 1];     /* a change address */
        fresh();
        notes_reset();
        SETTINGS.network = a->net;
        choose("Tools");
        choose("Verify address");
        scan_text(a->address);
        STEP("LoadMultisigWalletDescriptorView");
        choose("Scan descriptor");
        scan_text(v->descriptor);
        STEP("MultisigWalletDescriptorView");
        choose_index(0);
        search();
        char want[160];
        snprintf(want, sizeof want, "verified %u %s %s", a->index, a->change ? "change" : "receive", a->address);
        CHECK(noted(want), "descriptor %zu: no \"%s\" (last: %s)", i, want, last_note);
        done++;
    }
    printf("   %u addresses verified\n", done);
}

static void test_routes(void)
{
    printf("== descriptor views: OK, BACK, Cancel, wrong type, bad descriptors\n");
    /* Scanned from the main menu: OK goes home. */
    fresh();
    choose("Scan");
    scan_text(VEC_MS_FLOW_DESCRIPTOR);
    STEP("MultisigWalletDescriptorView");
    check_label("OK");
    choose("OK");
    STEP("MainMenuView");
    CHECK(multisig_descriptor() == NULL, "home kept the descriptor");

    /* BACK drops the descriptor and returns to what was under the scan. */
    fresh();
    SETTINGS.network = BTC_NET_REGTEST;
    choose("Tools");
    choose("Verify address");
    scan_text(VEC_MS_FLOW_ADDRESS);
    choose("Multisig");
    choose("Scan descriptor");
    scan_text(VEC_MS_FLOW_DESCRIPTOR);
    STEP("MultisigWalletDescriptorView");
    key(IN_BACK);
    STEP("LoadMultisigWalletDescriptorView");
    CHECK(multisig_descriptor() == NULL, "BACK kept the descriptor");
    /* No back button here; Cancel, outside a PSBT, goes home. */
    key(IN_BACK);
    STEP("LoadMultisigWalletDescriptorView");
    choose("Cancel");
    STEP("MainMenuView");

    /* ScanWalletDescriptorView refuses anything else. */
    fresh();
    choose("Tools");
    choose("Address explorer");
    choose("Scan wallet descriptor");
    scan_text(VEC_MS_FLOW_ADDRESS);
    STEP("ErrorView");
    CHECK(noted("wrong-type bitcoin_address"), "wrong type: %s", last_note);

    /* A descriptor embit would refuse after the rewrite, a setup file in UR
       bytes that SeedSigner cannot read (P2SH), and a single sig descriptor. */
    for (size_t i = 0; i < N(VEC_MS_SCANS); i++) {
        const vec_ms_scan_t *s = &VEC_MS_SCANS[i];
        if (strcmp(s->qr_type, "bytes__ur") == 0 && s->complete && s->descriptor == NULL) {
            fresh();
            choose("Scan");
            scan_text(s->frames[0].text);
            STEP("UnhandledExceptionView");
            choose_index(0);
            STEP("MainMenuView");
            break;
        }
    }
    fresh();
    choose("Scan");
    scan_text("sortedmulti wpkh([73c5da0a/84h/1h/0h]tpubDC8msFGeGuwnKG9Upg7DM2b4DaRqg3CUZa5g8v2SRQ6K4NSkxUgd7HsL2XVWbVm39yBA4LAxysQAm397zwQSQoQgewGiYZqrA9DsP4zbQ1M/0/*)");
    STEP("ScanInvalidQRTypeView");      /* embit refuses it as scanned: INVALID */
    fresh();
    choose("Scan");
    scan_text("{\"label\": \"x\", \"descriptor\": \"wpkh([73c5da0a/84h/1h/0h]tpubDC8msFGeGuwnKG9Upg7DM2b4DaRqg3CUZa5g8v2SRQ6K4NSkxUgd7HsL2XVWbVm39yBA4LAxysQAm397zwQSQoQgewGiYZqrA9DsP4zbQ1M/0/*)\"}");
    STEP("NotYetImplementedView");      /* not a basic multisig */
    CHECK(noted("descriptor not-basic"), "single sig: %s", last_note);
    fresh();
    choose("Scan");
    scan_text("{\"label\": \"x\", \"descriptor\": \"wsh(sortedmulti(2,nonsense))\"}");
    STEP("UnhandledExceptionView");     /* Descriptor.from_string() raises */
}

/* ---- PSBTChangeDetailsView's "Verify multisig change" --------------------- */

/* From the seed's choice to change output 0, past whatever comes before. */
static bool to_change(void)
{
    for (int i = 0; i < 40 && strcmp(on(), "PSBTChangeDetailsView") != 0; i++) {
        const char *v = on();
        if (strcmp(v, "PSBTOverviewView") == 0) {
            choose("Review details");
        } else if (strcmp(v, "PSBTMathView") == 0) {
            choose("Review recipients");
        } else if (strcmp(v, "PSBTAddressDetailsView") == 0) {
            choose_index(0);
        } else if (strstr(v, "Warning") != NULL) {
            choose("Continue");
        } else {
            return false;
        }
    }
    return strcmp(on(), "PSBTChangeDetailsView") == 0;
}

static unsigned verified_n, refused_n, earlier_n, single_n;

/* SeedSigner's answer for the same PSBT, seed and descriptor at change n. */
static int result_at(const vec_ms_verify_t *v, unsigned n)
{
    for (size_t i = 0; i < N(VEC_MS_VERIFY); i++) {
        const vec_ms_verify_t *w = &VEC_MS_VERIFY[i];
        if (w->change_num == n && strcmp(w->psbt, v->psbt) == 0 && strcmp(w->mnemonic, v->mnemonic) == 0 &&
            strcmp(w->passphrase, v->passphrase) == 0 && strcmp(w->descriptor, v->descriptor) == 0) {
            return w->result;
        }
    }
    return -1;
}

static void verify_change(const vec_ms_verify_t *v, size_t i)
{
    fresh();
    notes_reset();
    SETTINGS.network = v->net;
    load_seed(v->mnemonic, v->passphrase);
    uint8_t *bytes = malloc(strlen(v->psbt));
    size_t len = 0;
    CHECK(bytes && wally_base64_to_bytes(v->psbt, 0, bytes, strlen(v->psbt), &len) == WALLY_OK, "base64");
    choose("Scan");
    CHECK(bytes && psbt_flow_scanned(bytes, len), "%zu: staging", i);
    free(bytes);
    choose_index(0);
    if (!to_change()) {
        CHECK(0, "%zu: never reached the change, on %s", i, on());
        return;
    }
    const char *sel = ui_selected_label();
    if (sel && strcmp(sel, "Next") == 0) {
        /* A single sig PSBT: its change is the seed's own, and SeedSigner's
           flow never asks for a descriptor (its test calls the parser). */
        single_n++;
        return;
    }
    choose_index(0);                    /* Verify multisig change / addr */
    STEP("LoadMultisigWalletDescriptorView");
    choose("Scan descriptor");
    STEP("ScanWalletDescriptorView");
    scan_text(v->descriptor);
    STEP("MultisigWalletDescriptorView");
    check_label("Return to transaction");
    choose("Return to transaction");
    /* PSBTChangeDetailsView(0), then on to the change SeedSigner checked. */
    for (unsigned n = 0; n < v->change_num; n++) {
        char line[32];
        snprintf(line, sizeof line, "change-verified %u", n);
        if (!noted(line)) {
            /* Refused at an earlier change, where SeedSigner refuses too:
               its flow could not reach this one either. */
            CHECK(strcmp(on(), "PSBTAddressVerificationFailedView") == 0 && result_at(v, n) == 0,
                  "%zu: change %u was not verified (on %s)", i, n, on());
            earlier_n++;
            return;
        }
        choose("Next");
    }
    char line[40];
    if (v->result) {
        snprintf(line, sizeof line, "change-verified %u", v->change_num);
        STEP("PSBTChangeDetailsView");
        CHECK(noted(line), "%zu: change %u not verified", i, v->change_num);
        verified_n++;
    } else {
        snprintf(line, sizeof line, "change-unverified %u", v->change_num);
        STEP("PSBTAddressVerificationFailedView");
        CHECK(noted(line), "%zu: change %u not refused", i, v->change_num);
        key(IN_BACK);                   /* no way back */
        STEP("PSBTAddressVerificationFailedView");
        choose("Discard transaction");
        STEP("MainMenuView");
        CHECK(!psbt_flow_loaded() && multisig_descriptor() == NULL, "%zu: the flow outlived the refusal", i);
        refused_n++;
    }
}

static void test_verify_change(void)
{
    printf("== verify_multisig_output, as PSBTChangeDetailsView reaches it\n");
    for (size_t i = 0; i < N(VEC_MS_VERIFY); i++) {
        verify_change(&VEC_MS_VERIFY[i], i);
    }
    printf("   %u verified, %u refused, %u refused at an earlier change, as SeedSigner's;\n"
           "   %u single sig (no descriptor asked for)\n", verified_n, refused_n, earlier_n, single_n);

    /* Cancel inside a PSBT goes back to the change. */
    const vec_ms_verify_t *v = &VEC_MS_VERIFY[0];
    fresh();
    SETTINGS.network = v->net;
    load_seed(v->mnemonic, v->passphrase);
    uint8_t *bytes = malloc(strlen(v->psbt));
    size_t len = 0;
    CHECK(bytes && wally_base64_to_bytes(v->psbt, 0, bytes, strlen(v->psbt), &len) == WALLY_OK, "base64");
    choose("Scan");
    CHECK(bytes && psbt_flow_scanned(bytes, len), "staging");
    free(bytes);
    choose_index(0);
    CHECK(to_change(), "change");
    choose_index(0);
    STEP("LoadMultisigWalletDescriptorView");
    choose("Cancel");
    STEP("PSBTChangeDetailsView");
}

int main(void)
{
    btc_init(rand_fill);
    ui_set_clock(clock_fn);
    ui_set_note_hook(note);
    test__address_explorer__legacy_multisig_p2sh__flow();
    test__verify_address__legacy_multisig_p2sh__flow();
    test_verify_p2wsh();
    test_routes();
    test_verify_change();
    fresh();
    storage_wipe_all();
    return CHECK_DONE("test_multisig_flows");
}
