/*
 * SeedSigner's PSBT flow tests (tests/test_flows_psbt.py), ported: the same
 * sequence of Views, driven through the device's screens with the keys a
 * user would press. The PSBTs and seeds are the ones SeedSigner's tests use,
 * recorded by scripts/vectors/gen_psbt_vectors.py with what SeedSigner's
 * parser made of each (sim/vectors/seedsigner_psbt_flows.h).
 *
 * Then every PSBT SeedSigner's parser tests use (seedsigner_psbt.h) is
 * walked through the whole review, from choosing the seed to the signed QR:
 * each step must be the View SeedSigner's routing leads to from what its
 * parser established, show what it established, and sign to the same PSBT.
 */
#include "btc-init.h"
#include "flow.h"
#include "psbt-review.h"
#include "vectors/seedsigner_psbt.h"
#include "vectors/seedsigner_psbt_flows.h"
#include "wally_core.h"

#define N(a) (sizeof(a) / sizeof((a)[0]))

static const vec_psbt_t *flow_vec(const char *test, const char *passphrase)
{
    for (size_t i = 0; i < N(VEC_PSBT_FLOWS); i++) {
        const char *t = strstr(VEC_PSBT_FLOWS[i].test, "::");
        if (t && strcmp(t + 2, test) == 0 && strcmp(VEC_PSBT_FLOWS[i].passphrase, passphrase) == 0) {
            return &VEC_PSBT_FLOWS[i];
        }
    }
    CHECK(0, "no vector for %s", test);
    return NULL;
}

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

static void load_seed(const char *mnemonic, const char *passphrase)
{
    seed_t s;
    CHECK(seed_from_mnemonic(&s, mnemonic, passphrase) == SEED_OK, "seed");
    storage_set_pending(&s);
    seed_clear(&s);
    storage_finalize();
}

static size_t unbase64(const char *b64, uint8_t **out)
{
    const size_t max = strlen(b64);
    size_t len = 0;
    *out = malloc(max);
    if (*out == NULL || wally_base64_to_bytes(b64, 0, *out, max, &len) != WALLY_OK || len > max) {
        free(*out);
        *out = NULL;
        return 0;
    }
    return len;
}

/* The PSBT in memory is SeedSigner's signed PSBT (taproot: same count; its
   signature bytes legitimately differ, see test_psbt.c). */
static void check_signed(const vec_psbt_t *v, const char *what)
{
    size_t len;
    const uint8_t *got = psbt_flow_bytes(&len);
    if (v->policy && strcmp(v->policy, "p2tr") == 0) {
        return;
    }
    uint8_t *want;
    const size_t n = unbase64(v->signed_psbt, &want);
    CHECK(got && n == len && memcmp(got, want, n) == 0, "%s: not SeedSigner's signed PSBT", what);
    free(want);
}

static bool overview_noted(const vec_psbt_t *v)
{
    char want[128];
    snprintf(want, sizeof want, "overview %llu %llu %lld %u %u", (unsigned long long)v->spend_amount,
             (unsigned long long)v->change_amount, (long long)v->fee, v->num_inputs, v->n_dest);
    return noted(want);
}

/* ---- tests/test_flows_psbt.py: TestPSBTFlows ---------------------------- */

static void test_scan_psbt_first_then_correct_seedqr_flow(void)
{
    printf("== test_scan_psbt_first_then_correct_seedqr_flow\n");
    const vec_psbt_t *v = flow_vec("test_scan_psbt_first_then_correct_seedqr_flow", "");
    if (!v) return;
    fresh();
    notes_reset();
    choose("Scan");
    STEP("ScanView");
    scan_text(v->psbt);
    STEP("PSBTSelectSeedView");
    choose("Scan a seed");
    STEP("ScanSeedQRView");
    scan_text("080115060387063104071857067618681125136207731354");
    STEP("SeedFinalizeView");
    choose("Done");
    /* SeedOptionsView redirects back into the PSBT flow. */
    STEP("PSBTOverviewView");
    CHECK(overview_noted(v), "overview: %s", last_note);
    choose("Review details");
    STEP("PSBTMathView");
    choose("Review recipients");
    STEP("PSBTAddressDetailsView");
    choose("Next");
    for (unsigned i = 0; i < 3; i++) {
        STEP("PSBTChangeDetailsView");
        char want[160];
        snprintf(want, sizeof want, "change %u %s %llu m/%s", i, v->change[i].address,
                 (unsigned long long)v->change[i].amount, v->change[i].path);
        CHECK(noted(want), "no \"%s\"", want);
        choose("Next");
    }
    STEP("PSBTFinalizeView");
    choose("Approve transaction");
    STEP("PSBTSignedQRDisplayView");
    check_signed(v, "first flow");
    key(IN_OK);
    STEP("MainMenuView");
    CHECK(!psbt_flow_loaded(), "the PSBT outlived MainMenuView");

    /* Again, choosing the seed the first run left loaded. */
    choose("Scan");
    scan_text(v->psbt);
    STEP("PSBTSelectSeedView");
    choose_index(0);
    STEP("PSBTOverviewView");
    CHECK(psbt_flow_seed() == 0, "the existing seed is not the signing seed");
}

static void test_scan_psbt_first_then_load_electrum_seed(void)
{
    /* Electrum seeds are not ported yet: the option is offered, as in
       SeedSigner, and names the View it would open. */
    printf("== test_scan_psbt_first_then_load_electrum_seed (Electrum entry not ported)\n");
    const vec_psbt_t *v = flow_vec("test_scan_psbt_first_then_load_electrum_seed", "");
    if (!v) return;
    fresh();
    SETTINGS.electrum_seeds = true;
    choose("Scan");
    scan_text(v->psbt);
    STEP("PSBTSelectSeedView");
    choose("Enter Electrum seed");
    STEP("NotYetImplementedView");
}

static void test_scan_multisig_psbt_seed_already_signed_flow(void)
{
    printf("== test_scan_multisig_psbt_seed_already_signed_flow\n");
    const vec_psbt_t *v = flow_vec("test_scan_multisig_psbt_seed_already_signed_flow", "");
    const vec_psbt_t *abc = flow_vec("test_scan_multisig_psbt_seed_already_signed_flow", "abc");
    if (!v || !abc) return;
    const char *seedqr = "073318950739065415961602009907670428187212261116";
    fresh();
    choose("Scan");
    scan_text(v->psbt);
    STEP("PSBTSelectSeedView");
    choose("Scan a seed");
    STEP("ScanSeedQRView");
    scan_text(seedqr);
    STEP("SeedFinalizeView");
    choose("Done");
    STEP("PSBTOverviewView");
    choose("Review details");
    STEP("PSBTMathView");
    choose("Review recipients");
    STEP("PSBTAddressDetailsView");
    choose_index(0);
    STEP("PSBTChangeDetailsView");
    choose("Skip verification");
    STEP("PSBTFinalizeView");
    choose("Approve transaction");
    /* This seed's signature is already in the PSBT. */
    STEP("PSBTSigningErrorView");
    choose("Select different seed");
    STEP("PSBTSelectSeedView");
    CHECK(psbt_flow_seed() == -1, "the failed seed is still selected");
    choose("Scan a seed");
    STEP("ScanSeedQRView");
    scan_text(seedqr);
    STEP("SeedFinalizeView");
    choose("BIP-39 Passphrase");
    STEP("SeedAddPassphraseView");
    type_passphrase("abc", true);
    STEP("SeedReviewPassphraseView");
    choose("Done");
    STEP("PSBTOverviewView");
    choose("Review details");
    STEP("PSBTMathView");
    choose("Review recipients");
    STEP("PSBTAddressDetailsView");
    choose_index(0);
    STEP("PSBTChangeDetailsView");
    choose("Skip verification");
    STEP("PSBTFinalizeView");
    choose("Approve transaction");
    STEP("PSBTSignedQRDisplayView");
    check_signed(abc, "multisig, second key");
    key(IN_OK);
    STEP("MainMenuView");
}

static void test_parse_and_display_op_return_content(void)
{
    printf("== test_parse_and_display_op_return_content\n");
    const vec_psbt_t *v = flow_vec("test_parse_and_display_op_return_content", "");
    if (!v) return;
    fresh();
    notes_reset();
    choose("Scan");
    scan_text(v->psbt);
    STEP("PSBTSelectSeedView");
    choose("Scan a seed");
    scan_text("114006021552133507590698063102151531110102551496");
    STEP("SeedFinalizeView");
    choose("Done");
    STEP("PSBTOverviewView");
    choose("Review details");
    STEP("PSBTMathView");
    choose("Review recipients");
    STEP("PSBTChangeDetailsView");      /* a self-transfer: no recipients */
    choose("Next");
    STEP("PSBTOpReturnView");
    char want[200];
    snprintf(want, sizeof want, "op_return %s", v->op_return_hex);
    CHECK(noted(want), "no \"%s\"", want);
    choose_index(0);
    STEP("PSBTFinalizeView");
    choose("Approve transaction");
    STEP("PSBTSignedQRDisplayView");
    check_signed(v, "OP_RETURN");
    key(IN_OK);
    STEP("MainMenuView");
}

/* ---- TestPSBTOwnershipClaimRouting --------------------------------------- */

/* The PSBT staged and the seed loaded, as if both had just been scanned;
   each sequence then starts at seed selection. */
static void load_for_signing(const vec_psbt_t *v)
{
    fresh();
    SETTINGS.network = BTC_NET_REGTEST;
    load_seed(v->mnemonic, v->passphrase);
    choose("Scan");
    scan_text(v->psbt);
    STEP("PSBTSelectSeedView");
}

static void refused(const char *test, const char *view)
{
    printf("== %s\n", test);
    const vec_psbt_t *v = flow_vec(test, "");
    if (!v) return;
    load_for_signing(v);
    choose_index(0);
    STEP(view);
    key(IN_BACK);
    STEP(view);         /* no way back into a refused transaction */
    choose("Discard transaction");
    STEP("MainMenuView");
    CHECK(!psbt_flow_loaded(), "%s: the PSBT was not discarded", test);
}

static void test_wrong_seed_routes_back_to_seed_selection_flow(void)
{
    printf("== test_wrong_seed_routes_back_to_seed_selection_flow\n");
    const vec_psbt_t *v = flow_vec("test_wrong_seed_routes_back_to_seed_selection_flow", "");
    if (!v) return;
    load_for_signing(v);
    choose_index(0);
    STEP("PSBTSeedCannotSignView");
    choose("Select a different seed");
    STEP("PSBTSelectSeedView");
    CHECK(psbt_flow_seed() == -1, "the signing seed was not cleared");
    CHECK(!psbt_flow_parsed(), "the parse was not dropped");
    /* The PSBT itself is kept: the user is choosing another seed for it. */
    CHECK(psbt_flow_loaded(), "the PSBT was dropped");
}

/* ---- Beyond SeedSigner's tests ------------------------------------------ */

/* SeedOptionsView's "Scan transaction": the seed is chosen first, so the
   scan goes straight to the review; BACK from it unchooses the seed. */
static void test_scan_from_seed_options(void)
{
    printf("== scan a transaction from SeedOptionsView; BACK and HOLD\n");
    const vec_psbt_t *v = flow_vec("test_scan_psbt_first_then_correct_seedqr_flow", "");
    if (!v) return;
    fresh();
    load_seed(v->mnemonic, v->passphrase);
    SeedOptionsView(0);
    choose("Scan transaction");
    STEP("ScanPSBTView");
    scan_text("080115060387063104071857067618681125136207731354");
    STEP("ErrorView");                  /* Wrong QR Type: a SeedQR here */
    key(IN_BACK);
    choose("Scan transaction");
    scan_text(v->psbt);
    STEP("PSBTOverviewView");
    CHECK(psbt_flow_seed() == 0, "not the seed it was scanned from");
    key(IN_BACK);
    STEP("PSBTSelectSeedView");
    CHECK(psbt_flow_seed() == -1, "BACK kept the signing seed");
    choose_index(0);
    STEP("PSBTOverviewView");
    choose("Review details");
    key(IN_HOLD);
    STEP("MainMenuView");
    CHECK(!psbt_flow_loaded(), "HOLD home kept the PSBT");
}

/* PSBTUnsupportedScriptTypeWarningView. SeedSigner routes there when its
   parser has no policy, which never happens once a PSBT has an input: an
   input script it does not recognise gives a policy of type None, and the
   review carries on as if nothing were odd. Here the warning is shown for
   exactly that, as SeedSigner's comment there intends ("expecting p2sh
   (legacy multisig) and p2pkh to have no policy set").

   The PSBT: SeedSigner's SINGLE_SIG_NATIVE_SEGWIT_1_INPUT with its input
   turned into a bare P2PK of the test seed's key at m/84h/1h/0h/0/3, and
   0.5 btc to SINGLE_SIG_NATIVE_SEGWIT_RECEIVE, built with SeedSigner's
   psbt_testing_util; its parser reports policy {'type': None}, spend
   50000000, change 0, fee 50000000. */
static const char P2PK_INPUT_PSBT[] =
    "cHNidP8BAFICAAAAAU4T/0aX9mmNZHyKh+0AHYY+EtdJxndMRra0gn4QPCZdAQAAAAD9////AYDw+gIAAAAAFgAU/CQyke7BQzfO"
    "gYF3eMJR6S2nxFZwAAAATwEENYfPA1eZSZWAAAAA1EVrtHmBgPzhZuFrd3VCeOB9g84HDzuifpxdLfZajKoCw8W0/EHs9FN1u6NS"
    "cE9RUOH1hQG3CuXidzQ5xE0a8jIQD7iC/1QAAIABAACAAAAAgAABASwA4fUFAAAAACMhAvXbqnkmWDXPNEkHznP0l2VFFql2WpgX"
    "42yD3Z9fJY/3rAEDBAEAAAAiBgL126p5Jlg1zzRJB85z9JdlRRapdlqYF+Nsg92fXyWP9xgPuIL/VAAAgAEAAIAAAACAAAAAAAMA"
    "AAAAAA==";

static void test_unsupported_script_type_warning(void)
{
    printf("== an unrecognised input script: PSBTUnsupportedScriptTypeWarningView\n");
    fresh();
    notes_reset();
    SETTINGS.network = BTC_NET_REGTEST;
    load_seed("model ensure search plunge galaxy firm exclude brain satoshi meadow cable roast", "");
    choose("Scan");
    scan_text(P2PK_INPUT_PSBT);
    STEP("PSBTSelectSeedView");
    choose_index(0);
    STEP("PSBTOverviewView");
    CHECK(noted("overview 50000000 0 50000000 1 1"), "overview: %s", last_note);
    choose("Review details");
    STEP("PSBTUnsupportedScriptTypeWarningView");
    choose("Continue");
    STEP("PSBTAddressDetailsView");     /* past PSBTMathView, as SeedSigner routes */
    key(IN_BACK);
    STEP("PSBTOverviewView");           /* skip_current_view: not back to the warning */
    choose("Review details");
    choose("Continue");
    choose("Next");
    STEP("PSBTFinalizeView");
    choose("Approve transaction");
    /* Neither libwally nor embit signs a bare P2PK input. */
    STEP("PSBTSigningErrorView");
}

/* ---- Every PSBT of SeedSigner's parser tests, through the screens -------- */

static const char *refusal_view(const char *error)
{
    static const char *const map[][2] = {
        { "PSBTInputOwnershipClaimError", "PSBTInputOwnershipClaimFailedView" },
        { "PSBTOutputOwnershipClaimError", "PSBTOutputOwnershipClaimFailedView" },
        { "PSBTSurplusDerivationPathsError", "PSBTSurplusDerivationPathsView" },
        { "PSBTMixedDerivationPathTypesError", "PSBTMixedDerivationPathTypesView" },
        { "PSBTOutputOwnershipContradictionError", "PSBTOutputOwnershipContradictionView" },
        { "PSBTSeedCannotSignError", "PSBTSeedCannotSignView" },
    };
    for (size_t i = 0; i < N(map); i++) {
        if (strcmp(error, map[i][0]) == 0) {
            return map[i][1];
        }
    }
    return "ErrorView";     /* what SeedSigner shows as its crash screen */
}

static unsigned walked, refusals, signing_errors, views_seen;

/* How often the walk reached each View, to show what it covers. */
static struct { const char *view; unsigned n; } reached[24];

static void tally(const char *view)
{
    size_t i = 0;
    while (i < N(reached) && reached[i].view && strcmp(reached[i].view, view) != 0) i++;
    if (i < N(reached)) {
        reached[i].view = view;
        reached[i].n++;
    }
}

static bool step(const char *view, const vec_psbt_t *v)
{
    views_seen++;
    tally(view);
    if (strcmp(on(), view) != 0) {
        CHECK(0, "%s: expected %s, on %s", v->test, view, on());
        return false;
    }
    return true;
}

static void walk(const vec_psbt_t *v)
{
    fresh();
    notes_reset();
    SETTINGS.network = v->net;
    load_seed(v->mnemonic, v->passphrase);
    uint8_t *bytes;
    const size_t len = unbase64(v->psbt, &bytes);
    choose("Scan");
    CHECK(bytes && psbt_flow_scanned(bytes, len), "%s: staging", v->test);
    free(bytes);
    if (!step("PSBTSelectSeedView", v)) return;
    choose_index(0);

    if (v->error) {
        const char *view = refusal_view(v->error);
        if (!step(view, v)) return;
        choose_index(0);
        step(strcmp(view, "PSBTSeedCannotSignView") == 0 ? "PSBTSelectSeedView" : "MainMenuView", v);
        refusals++;
        return;
    }
    /* Stricter than SeedSigner (psbt-review.h): a PSBT with no outputs at
       all is refused, where SeedSigner reviews it as all fee. */
    if (v->n_dest == 0 && v->n_change == 0 && v->op_return_hex == NULL) {
        if (step("ErrorView", v) && noted("parse NoOutputs")) {
            refusals++;
        } else {
            CHECK(0, "%s: a PSBT with no outputs was not refused", v->test);
        }
        return;
    }
    if (!step("PSBTOverviewView", v)) return;
    CHECK(overview_noted(v), "%s: overview %s", v->test, last_note);
    choose("Review details");

    /* PSBTOverviewView's routing, from what SeedSigner's parser found. */
    bool math = true;
    if (v->policy == NULL) {
        if (!step("PSBTUnsupportedScriptTypeWarningView", v)) return;
        choose("Continue");
        math = false;
    } else if (v->is_high_fee) {
        if (!step("PSBTHighFeeWarningView", v)) return;
        choose("Continue");
        if (v->change_amount == 0) {
            if (!step("PSBTNoChangeWarningView", v)) return;
            choose("Continue");
        }
    } else if (v->change_amount == 0) {
        if (!step("PSBTNoChangeWarningView", v)) return;
        choose("Continue");
    }
    bool recipients = !math || v->n_dest > 0;
    if (math) {
        if (!step("PSBTMathView", v)) return;
        key(IN_BACK);   /* skip_current_view: BACK passes the warnings */
        if (!step("PSBTOverviewView", v)) return;
        choose("Review details");
        for (int i = 0; i < 2 && strcmp(on(), "PSBTMathView") != 0; i++) {
            choose("Continue");
        }
        if (!step("PSBTMathView", v)) return;
        choose("Review recipients");
    }
    bool change = !recipients || v->change_amount > 0;
    if (recipients) {
        for (unsigned i = 0; i < v->n_dest; i++) {
            if (!step("PSBTAddressDetailsView", v)) return;
            char want[200];
            snprintf(want, sizeof want, "recipient %u %s %llu", i, v->dest[i].address,
                     (unsigned long long)v->dest[i].amount);
            CHECK(noted(want), "%s: no \"%s\"", v->test, want);
            choose(i + 1 < v->n_dest ? "Next recipient" : "Next");
        }
    }
    if (change) {
        for (unsigned i = 0; i < v->n_change; i++) {
            if (!step("PSBTChangeDetailsView", v)) return;
            char want[200];
            snprintf(want, sizeof want, "change %u %s %llu m/%s", i, v->change[i].address,
                     (unsigned long long)v->change[i].amount, v->change[i].path);
            CHECK(noted(want), "%s: no \"%s\"", v->test, want);
            choose(v->m >= 0 ? "Skip verification" : "Next");
        }
    }
    if (v->op_return_hex && strlen(v->op_return_hex) <= 160) {
        if (!step("PSBTOpReturnView", v)) return;
        char want[200];
        snprintf(want, sizeof want, "op_return %s", v->op_return_hex);
        CHECK(noted(want), "%s: no \"%s\"", v->test, want);
        choose("Next");
    } else if (v->op_return_hex) {
        choose("Next");     /* longer than this port keeps (psbt-review.h) */
    }
    if (!step("PSBTFinalizeView", v)) return;
    choose("Approve transaction");
    if (v->sigs_added > 0) {
        if (!step("PSBTSignedQRDisplayView", v)) return;
        check_signed(v, v->test);
        key(IN_OK);
        step("MainMenuView", v);
        CHECK(!psbt_flow_loaded(), "%s: the PSBT outlived the flow", v->test);
    } else {
        step("PSBTSigningErrorView", v);
        signing_errors++;
    }
    walked++;
}

static void test_every_parser_psbt(void)
{
    printf("== every PSBT of SeedSigner's parser tests, through the screens\n");
    for (size_t i = 0; i < N(VEC_PSBT); i++) {
        walk(&VEC_PSBT[i]);
    }
    printf("   %u reviewed to the end (%u could not be signed), %u refused; %u views checked\n",
           walked, signing_errors, refusals, views_seen);
    for (size_t i = 0; i < N(reached) && reached[i].view; i++) {
        printf("   %5u %s\n", reached[i].n, reached[i].view);
    }
}

int main(void)
{
    btc_init(rand_fill);
    ui_set_clock(clock_fn);
    ui_set_note_hook(note);
    test_scan_psbt_first_then_correct_seedqr_flow();
    test_scan_psbt_first_then_load_electrum_seed();
    test_scan_multisig_psbt_seed_already_signed_flow();
    test_parse_and_display_op_return_content();
    refused("test_forged_output_claim_terminates_signing_flow", "PSBTOutputOwnershipClaimFailedView");
    refused("test_forged_input_claim_terminates_signing_flow", "PSBTInputOwnershipClaimFailedView");
    refused("test_surplus_derivation_paths_terminate_signing_flow", "PSBTSurplusDerivationPathsView");
    refused("test_output_ownership_contradiction_terminates_signing_flow",
            "PSBTOutputOwnershipContradictionView");
    refused("test_mixed_derivation_path_types_terminate_signing_flow", "PSBTMixedDerivationPathTypesView");
    test_wrong_seed_routes_back_to_seed_selection_flow();
    test_scan_from_seed_options();
    test_unsupported_script_type_warning();
    test_every_parser_psbt();
    fresh();
    storage_wipe_all();
    return CHECK_DONE("test_psbt_flows");
}
