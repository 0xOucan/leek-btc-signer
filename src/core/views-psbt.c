/*
 * SeedSigner's PSBT views (views/psbt_views.py), ported view by view: choose
 * the seed, review the transaction (overview, warnings, the math, each
 * recipient, each change output, OP_RETURN), sign it and show the signed
 * PSBT as an animated QR; and the screens that refuse a PSBT whose account
 * of itself does not hold up. Each function or screen is named after the
 * View it ports, and routes where that View's Destination goes.
 *
 * Review and signing are psbt-review.c, SeedSigner's PSBTParser ported. A
 * PSBT is public; the seed signing it stays in seed-storage and is only
 * read here.
 */
#include "views-psbt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psbt-review.h"
#include "qr-encode.h"
#include "screens.h"
#include "seed-storage.h"
#include "settings.h"
#include "views-seed.h"
#include "views.h"
#include "wally_core.h"

#define VIEW_TEXT_MAX_PSBT 400

/* ---- The Controller's PSBT fields ------------------------------------- */

static struct {
    uint8_t *psbt;          /* controller.psbt: as scanned, then as signed */
    size_t len;
    int seed;               /* controller.psbt_seed; -1 for none */
    bool resume;            /* resume_main_flow == FLOW__PSBT */
    psbt_review_t review;   /* controller.psbt_parser */
    bool parsed;
    int parsed_seed;
    qr_encoder_t *enc;      /* the signed PSBT's QR, only while shown */
    uint8_t *qr_buf;
} flow = { .seed = -1 };

static void drop_parse(void)
{
    if (flow.parsed) {
        psbt_review_free(&flow.review);
    }
    flow.parsed = false;
}

void psbt_flow_clear(void)
{
    drop_parse();
    free(flow.psbt);
    flow.psbt = NULL;
    flow.len = 0;
    flow.seed = -1;
    flow.resume = false;
}

bool psbt_flow_loaded(void)
{
    return flow.psbt != NULL;
}

const uint8_t *psbt_flow_bytes(size_t *len)
{
    *len = flow.len;
    return flow.psbt;
}

int psbt_flow_seed(void)
{
    return flow.seed;
}

bool psbt_flow_parsed(void)
{
    return flow.parsed;
}

void psbt_flow_set_seed(int seed)
{
    flow.seed = seed;
}

static bool can_sign_hint(int seed)
{
    const seed_t *s = storage_seed(seed);
    return s && flow.psbt &&
           psbt_has_matching_input_fingerprint(flow.psbt, flow.len, s, SETTINGS.network);
}

/* ---- Helpers ---------------------------------------------------------- */

static void no_back(void)
{
}

static void not_yet(const char *view)
{
    char text[96];
    snprintf(text, sizeof text, "%s is not ported yet.", view);
    const view_spec_t v = { .name = "NotYetImplementedView", .title = "Work In Progress",
                            .text = text, .status = true, .kind = NOTICE_INFO,
                            .buttons = { "OK" }, .n_buttons = 1 };
    view_push(&v);
}

/* BtcAmount's text, on one line. */
static const char *amount(uint64_t sats, char *buf, size_t cap)
{
    btc_amount_t a;
    btc_amount_text(sats, SETTINGS.btc_denomination, SETTINGS.network, &a);
    if (a.sats[0]) {
        snprintf(buf, cap, "%s|%s %s", a.main, a.sats, a.unit);
    } else {
        snprintf(buf, cap, "%s %s", a.main, a.unit);
    }
    return buf;
}

static void path_text(const uint32_t *path, size_t len, char *buf, size_t cap)
{
    size_t k = (size_t)snprintf(buf, cap, "m");
    for (size_t i = 0; i < len && k < cap; i++) {
        const uint32_t p = path[i];
        k += (size_t)snprintf(buf + k, cap - k, p >= 0x80000000u ? "/%uh" : "/%u",
                              (unsigned)(p & 0x7fffffffu));
    }
}

/* Destination(MainMenuView): home, with the flow dropped. */
static void discard_select(int i)
{
    (void)i;
    ui_home_fresh();
}

/* ---- The refusals ------------------------------------------------------ */

/* A PSBT refused in parsing: a status screen with one way out, home. Each is
   reached with clear_history, so BACK has nowhere to go either. */
static void refusal(const char *name, notice_kind_t kind, const char *title,
                    const char *headline, const char *text)
{
    const view_spec_t v = { .name = name, .status = true, .kind = kind, .title = title,
                            .headline = headline, .text = text,
                            .buttons = { "Discard transaction" }, .n_buttons = 1,
                            .select = discard_select, .back = no_back };
    view_push_fresh(&v);
}

static void PSBTOutputOwnershipClaimFailedView(void)
{
    refusal("PSBTOutputOwnershipClaimFailedView", NOTICE_DIRE, "Suspicious Transaction",
            "Likely an Attack!",
            "The transaction's change/self-transfer output is not going back to your wallet.");
}

static void PSBTInputOwnershipClaimFailedView(void)
{
    refusal("PSBTInputOwnershipClaimFailedView", NOTICE_WARNING, "Transaction Problem", NULL,
            "This transaction incorrectly claims that one of its inputs belongs to this seed.");
}

static void PSBTSurplusDerivationPathsView(void)
{
    refusal("PSBTSurplusDerivationPathsView", NOTICE_WARNING, "Transaction Problem", NULL,
            "This transaction claims too many keys for one of its outputs.");
}

static void PSBTMixedDerivationPathTypesView(void)
{
    refusal("PSBTMixedDerivationPathTypesView", NOTICE_WARNING, "Transaction Problem", NULL,
            "This transaction claims taproot and non-taproot keys for the same script.");
}

static void PSBTOutputOwnershipContradictionView(void)
{
    refusal("PSBTOutputOwnershipContradictionView", NOTICE_DIRE, "Suspicious Transaction",
            "Likely an Attack!", "This transaction misrepresents where one of its outputs pays.");
}

/* What SeedSigner does not catch (it would show its crash screen): a PSBT
   it cannot read, inputs of mixed policies, a malformed script, and where
   this port is stricter than SeedSigner (psbt-review.h). */
static void psbt_error_view(psbt_err_t err)
{
    char text[160];
    snprintf(text, sizeof text, "This transaction cannot be reviewed (%s).", psbt_err_name(err));
    refusal("ErrorView", NOTICE_ERROR, "Transaction Problem", NULL, text);
}

static void select_different_seed(int i)
{
    (void)i;
    view_clear_history();       /* Destination(PSBTSelectSeedView, clear_history=True) */
    PSBTSelectSeedView();
}

static void PSBTSeedCannotSignView(void)
{
    const view_spec_t v = { .name = "PSBTSeedCannotSignView", .status = true, .kind = NOTICE_INFO,
                            .title = "Seed Can't Sign",
                            .text = "None of the inputs in this transaction are controlled by this seed.",
                            .buttons = { "Select a different seed" }, .n_buttons = 1,
                            .select = select_different_seed, .back = no_back };
    view_push(&v);
}

/* ---- Signing: PSBTFinalizeView and what follows ------------------------ */

static void signed_qr_leave(void)
{
    if (flow.enc) {
        qr_encoder_wipe(flow.enc);
    }
    free(flow.enc);
    free(flow.qr_buf);
    flow.enc = NULL;
    flow.qr_buf = NULL;
    ui_after(ui_home_fresh);    /* Destination(MainMenuView, clear_history=True) */
}

static void PSBTSignedQRDisplayView(void)
{
    const size_t cap = flow.len + 16;
    flow.enc = malloc(sizeof *flow.enc);
    flow.qr_buf = malloc(cap);
    if (!flow.enc || !flow.qr_buf ||
        !qr_encode_psbt_ur(flow.enc, flow.psbt, flow.len, SETTINGS.qr_density, flow.qr_buf, cap)) {
        free(flow.enc);
        free(flow.qr_buf);
        flow.enc = NULL;
        flow.qr_buf = NULL;
        psbt_error_view(PSBT_ERR_NO_MEMORY);
        return;
    }
    ui_note("signed-qr %u bytes %d parts", (unsigned)flow.len, (int)qr_encoder_seq_len(flow.enc));
    ui_show_qr("PSBTSignedQRDisplayView", flow.enc, signed_qr_leave);
}

static void signing_error_select(int i)
{
    (void)i;
    flow.seed = -1;     /* it did not add a valid signature */
    select_different_seed(0);
}

static void PSBTSigningErrorView(void)
{
    const view_spec_t v = { .name = "PSBTSigningErrorView", .status = true, .kind = NOTICE_WARNING,
                            .title = "Transaction Error", .headline = "Signing Failed",
                            .text = "Signing with this seed did not add a valid signature.",
                            .buttons = { "Select different seed" }, .n_buttons = 1,
                            .select = signing_error_select };
    view_push(&v);
}

static void finalize_select(int i)
{
    (void)i;
    const seed_t *s = storage_seed(flow.seed);
    if (!flow.parsed || s == NULL) {
        ui_home_fresh();    /* "Should not be able to get here" */
        return;
    }
    ui_working("Signing...");
    char *b64 = NULL;
    size_t added = 0;
    const psbt_err_t err = psbt_sign(&flow.review, s, SETTINGS.network, &b64, &added);
    uint8_t *bytes = NULL;
    size_t len = 0;
    if (err == PSBT_OK && added > 0) {
        const size_t max = strlen(b64);     /* base64 is longer than its bytes */
        bytes = malloc(max);
        if (bytes == NULL || wally_base64_to_bytes(b64, 0, bytes, max, &len) != WALLY_OK || len > max) {
            free(bytes);
            bytes = NULL;
        }
    }
    if (b64) {
        wally_free_string(b64);
    }
    ui_note("sign %s %u", psbt_err_name(err), (unsigned)added);
    if (bytes == NULL) {
        PSBTSigningErrorView();     /* signing failed or did nothing */
        return;
    }
    /* controller.psbt = trimmed_psbt */
    free(flow.psbt);
    flow.psbt = bytes;
    flow.len = len;
    PSBTSignedQRDisplayView();
}

static void PSBTFinalizeView(void)
{
    const view_spec_t v = { .name = "PSBTFinalizeView", .title = "Sign Transaction",
                            .text = "Click to approve this transaction",
                            .buttons = { "Approve transaction" }, .n_buttons = 1,
                            .select = finalize_select };
    view_push(&v);
}

/* ---- Review: the details, in SeedSigner's order ------------------------ */

static void PSBTChangeDetailsView(int n);

static void op_return_select(int i)
{
    (void)i;
    PSBTFinalizeView();
}

static void PSBTOpReturnView(void)
{
    const psbt_review_t *r = &flow.review;
    /* SeedSigner shows the bytes as text if they decode as UTF-8, else hex.
       This font has ASCII only, so printable ASCII is the text case. */
    bool text = true;
    for (size_t i = 0; i < r->op_return_len; i++) {
        text &= r->op_return[i] >= 0x20 && r->op_return[i] < 0x7f;
    }
    char body[2 * sizeof r->op_return + 24];
    if (text) {
        snprintf(body, sizeof body, "%.*s", (int)r->op_return_len, (const char *)r->op_return);
    } else {
        size_t k = (size_t)snprintf(body, sizeof body, "raw hex data\n");
        for (size_t i = 0; i < r->op_return_len && k + 3 < sizeof body; i++) {
            k += (size_t)snprintf(body + k, sizeof body - k, "%02x", r->op_return[i]);
        }
    }
    const view_spec_t v = { .name = "PSBTOpReturnView", .title = "OP_RETURN", .text = body,
                            .buttons = { "Next" }, .n_buttons = 1, .select = op_return_select };
    view_push(&v);
    char hex[2 * sizeof r->op_return + 1] = "";
    for (size_t i = 0; i < r->op_return_len; i++) {
        snprintf(hex + 2 * i, 3, "%02x", r->op_return[i]);
    }
    ui_note("op_return %s", hex);
}

/* After the last recipient or change output. */
static void after_outputs(void)
{
    if (flow.review.has_op_return) {
        PSBTOpReturnView();
    } else {
        PSBTFinalizeView();
    }
}

static void change_select(int i)
{
    const int n = view_arg();
    const psbt_review_t *r = &flow.review;
    if (r->is_multisig && i == 0) {
        /* Verify multisig change: needs the wallet descriptor (T47). */
        not_yet("LoadMultisigWalletDescriptorView");
        return;
    }
    if ((size_t)n + 1 < r->num_change) {
        PSBTChangeDetailsView(n + 1);
    } else {
        after_outputs();
    }
}

static void PSBTChangeDetailsView(int n)
{
    const psbt_review_t *r = &flow.review;
    if ((size_t)n >= r->num_change) {
        after_outputs();
        return;
    }
    const psbt_change_t *c = &r->change[n];
    const bool is_change = psbt_is_change_path(c->path, c->path_len);
    char fp[9], path[96], amt[64], text[VIEW_TEXT_MAX_PSBT];
    seed_fingerprint_hex(storage_seed(flow.seed), fp);
    path_text(c->path, c->path_len, path, sizeof path);
    /* Single sig change is proven the seed's own in parsing; multisig is
       checked against a descriptor, which this port cannot load yet. */
    const bool verified = !r->is_multisig;
    snprintf(text, sizeof text, "%s\n%s\n%s %s #%u\n%s: %s%s", amount(c->amount, amt, sizeof amt),
             c->address, r->is_multisig ? "Multisig" : "Single sig",
             is_change ? "change address" : "receive address",
             (unsigned)(c->path_len ? c->path[c->path_len - 1] : 0), fp, path,
             verified ? "\nAddress verified!" : "");
    view_spec_t v = { .name = "PSBTChangeDetailsView", .title = is_change ? "Your Change" : "Self-Transfer",
                      .text = text, .select = change_select, .arg = n };
    if (r->is_multisig) {
        v.buttons[v.n_buttons++] = is_change ? "Verify multisig change" : "Verify multisig addr";
        v.buttons[v.n_buttons++] = "Skip verification";
    } else {
        v.buttons[v.n_buttons++] = "Next";
    }
    view_push(&v);
    ui_note("change %d %s %llu %s", n, c->address, (unsigned long long)c->amount, path);
}

static void PSBTAddressDetailsView(int n);

/* Past recipient n: the next one, the change, or what comes after. */
static void address_next(int n)
{
    const psbt_review_t *r = &flow.review;
    if ((size_t)n + 1 < r->num_dest) {
        PSBTAddressDetailsView(n + 1);
    } else if (r->change_amount > 0) {
        PSBTChangeDetailsView(0);
    } else {
        after_outputs();
    }
}

static void address_select(int i)
{
    (void)i;
    address_next(view_arg());
}

static void PSBTAddressDetailsView(int n)
{
    const psbt_review_t *r = &flow.review;
    if ((size_t)n >= r->num_dest) {
        address_next((int)r->num_dest - 1);     /* none to show (SeedSigner would raise) */
        return;
    }
    char title[32], amt[64], text[VIEW_TEXT_MAX_PSBT];
    if (r->num_dest > 1) {
        snprintf(title, sizeof title, "Will Send (#%d)", n + 1);
    } else {
        snprintf(title, sizeof title, "Will Send");
    }
    snprintf(text, sizeof text, "%s\n%s", amount(r->dest[n].amount, amt, sizeof amt), r->dest[n].address);
    const view_spec_t v = { .name = "PSBTAddressDetailsView", .title = title, .text = text,
                            .buttons = { (size_t)n + 1 < r->num_dest ? "Next recipient" : "Next" },
                            .n_buttons = 1, .select = address_select, .arg = n };
    view_push(&v);
    ui_note("recipient %d %s %llu", n, r->dest[n].address, (unsigned long long)r->dest[n].amount);
}

static void math_select(int i)
{
    (void)i;
    if (flow.review.num_dest > 0) {
        PSBTAddressDetailsView(0);
    } else {
        PSBTChangeDetailsView(0);   /* a self-transfer */
    }
}

static void PSBTMathView(bool replace)
{
    const psbt_review_t *r = &flow.review;
    char a[4][48], text[VIEW_TEXT_MAX_PSBT];
    amount(r->input_amount, a[0], sizeof a[0]);
    amount(r->spend_amount, a[1], sizeof a[1]);
    amount(r->fee > 0 ? (uint64_t)r->fee : 0, a[2], sizeof a[2]);
    amount(r->change_amount, a[3], sizeof a[3]);
    snprintf(text, sizeof text, "  %s\n- %s\n- %s fee\n= %s change", a[0], a[1], a[2], a[3]);
    const view_spec_t v = { .name = "PSBTMathView", .title = "Transaction Math", .text = text,
                            .buttons = { "Review recipients" }, .n_buttons = 1,
                            .select = math_select };
    if (replace) {
        view_replace(&v);   /* skip_current_view: never BACK to a warning */
    } else {
        view_push(&v);
    }
}

/* ---- The warnings ------------------------------------------------------ */

static void no_change_select(int i)
{
    (void)i;
    PSBTMathView(true);
}

static void PSBTNoChangeWarningView(bool replace)
{
    const view_spec_t v = { .name = "PSBTNoChangeWarningView", .status = true, .kind = NOTICE_WARNING,
                            .title = "Caution", .headline = "Full Spend!",
                            .text = "This transaction spends its entire input value. No change is "
                                    "coming back to your wallet.",
                            .buttons = { "Continue" }, .n_buttons = 1, .select = no_change_select };
    if (replace) {
        view_replace(&v);
    } else {
        view_push(&v);
    }
}

static void high_fee_select(int i)
{
    (void)i;
    if (flow.review.change_amount == 0) {
        PSBTNoChangeWarningView(true);
    } else {
        PSBTMathView(true);
    }
}

static void PSBTHighFeeWarningView(void)
{
    const view_spec_t v = { .name = "PSBTHighFeeWarningView", .status = true, .kind = NOTICE_DIRE,
                            .title = "Caution", .headline = "High Fee!",
                            .text = "This transaction has a fee higher than 25% of the total output "
                                    "value (excluding change).",
                            .buttons = { "Continue" }, .n_buttons = 1, .select = high_fee_select };
    view_push(&v);
}

static void unsupported_select(int i)
{
    (void)i;
    /* Destination(PSBTAddressDetailsView, address_num=0, skip_current_view) */
    ui_pop();
    PSBTAddressDetailsView(0);
}

static void PSBTUnsupportedScriptTypeWarningView(void)
{
    const view_spec_t v = { .name = "PSBTUnsupportedScriptTypeWarningView", .status = true,
                            .kind = NOTICE_WARNING, .title = "Caution",
                            .headline = "Unsupported Script Type!",
                            .text = "Transaction has unsupported input script type, please verify "
                                    "your change addresses.",
                            .buttons = { "Continue" }, .n_buttons = 1, .select = unsupported_select };
    view_push(&v);
}

/* ---- PSBTOverviewView -------------------------------------------------- */

static void overview_select(int i)
{
    (void)i;
    const psbt_review_t *r = &flow.review;
    if (r->policy == POLICY_NONE) {
        PSBTUnsupportedScriptTypeWarningView();
    } else if (r->is_high_fee) {
        PSBTHighFeeWarningView();
    } else if (r->change_amount == 0) {
        PSBTNoChangeWarningView(false);
    } else {
        PSBTMathView(false);
    }
}

static void overview_back(void)
{
    flow.seed = -1;
    ui_pop();
}

/* Parse for the signing seed, unless already parsed for it. False if the
   parse refused the PSBT; the refusal's screen is then open. */
static bool parse(void)
{
    if (flow.parsed && flow.parsed_seed == flow.seed) {
        return true;
    }
    drop_parse();
    const seed_t *s = storage_seed(flow.seed);
    if (s == NULL || flow.psbt == NULL) {
        ui_home_fresh();
        return false;
    }
    ui_working("Parsing PSBT...");
    const psbt_err_t err = psbt_review(flow.psbt, flow.len, s, SETTINGS.network, &flow.review);
    ui_note("parse %s", psbt_err_name(err));
    if (err == PSBT_OK) {
        flow.parsed = true;
        flow.parsed_seed = flow.seed;
        return true;
    }
    switch (err) {
    case PSBT_ERR_INPUT_CLAIM:          PSBTInputOwnershipClaimFailedView(); break;
    case PSBT_ERR_OUTPUT_CLAIM:         PSBTOutputOwnershipClaimFailedView(); break;
    case PSBT_ERR_SURPLUS_PATHS:        PSBTSurplusDerivationPathsView(); break;
    case PSBT_ERR_MIXED_PATH_TYPES:     PSBTMixedDerivationPathTypesView(); break;
    case PSBT_ERR_OUTPUT_CONTRADICTION: PSBTOutputOwnershipContradictionView(); break;
    case PSBT_ERR_SEED_CANNOT_SIGN:
        /* Not suspicious, just the wrong seed: back to choose another. */
        flow.seed = -1;
        PSBTSeedCannotSignView();
        break;
    /* Where SeedSigner's parser raises, it shows its crash screen. */
    case PSBT_ERR_MIXED_INPUTS:
        UnhandledExceptionView("RuntimeError", "psbt_parser.py, _parse_inputs", "Mixed inputs in the transaction");
        break;
    case PSBT_ERR_UNSUPPORTED:
        UnhandledExceptionView("RuntimeError", "psbt_parser.py, _parse_outputs", "Unsupported policy type");
        break;
    case PSBT_ERR_INVALID:
        UnhandledExceptionView("ValueError", "psbt_parser.py", "Malformed multisig script");
        break;
    case PSBT_ERR_PARSE:
        UnhandledExceptionView("PSBTError", "psbt.py", "The transaction could not be read");
        break;
    default:
        /* This port's stricter refusals (psbt-review.h), and memory. */
        psbt_error_view(err);
        break;
    }
    return false;
}

void PSBTOverviewView(void)
{
    if (!parse()) {
        return;
    }
    const psbt_review_t *r = &flow.review;
    size_t n_change = 0, n_self = 0;
    for (size_t i = 0; i < r->num_change; i++) {
        if (psbt_is_change_path(r->change[i].path, r->change[i].path_len)) {
            n_change++;
        } else {
            n_self++;
        }
    }
    /* PSBTOverviewScreen: the amount sent, then where the inputs go. */
    char amt[48], fee[48], to[48], text[VIEW_TEXT_MAX_PSBT];
    amount(r->spend_amount, amt, sizeof amt);
    amount(r->fee > 0 ? (uint64_t)r->fee : 0, fee, sizeof fee);
    if (r->num_dest == 0) {
        snprintf(to, sizeof to, "self-transfer");
    } else if (r->num_dest + n_self == 1) {
        snprintf(to, sizeof to, "%.12s...", r->dest[0].address);
    } else {
        snprintf(to, sizeof to, "%u recipients", (unsigned)(r->num_dest + n_self));
    }
    snprintf(text, sizeof text, "%s\n%u input%s > %s\nfee %s%s%s%s", amt, (unsigned)r->num_inputs,
             r->num_inputs == 1 ? "" : "s", to, fee, r->is_high_fee ? " (high)" : "",
             n_change ? "\n+ change" : "", r->has_op_return ? "\n+ OP_RETURN" : "");
    const view_spec_t v = { .name = "PSBTOverviewView", .title = "Review Transaction", .text = text,
                            .buttons = { "Review details" }, .n_buttons = 1,
                            .select = overview_select, .back = overview_back };
    view_push(&v);
    ui_note("overview %llu %llu %lld %u %u", (unsigned long long)r->spend_amount,
            (unsigned long long)r->change_amount, (long long)r->fee, (unsigned)r->num_inputs,
            (unsigned)r->num_dest);
}

/* ---- PSBTSelectSeedView ------------------------------------------------ */

enum { SEL_SCAN, SEL_12, SEL_24, SEL_ELECTRUM };

static void select_seed_select(int i)
{
    const int n = storage_count();
    if (i < n) {
        flow.seed = i;
        PSBTOverviewView();
        return;
    }
    /* A sub-flow: the PSBT flow resumes once the seed is loaded. */
    flow.resume = true;
    switch (i - n) {
    case SEL_SCAN: ui_push(&SCREEN_SCAN_SEED); break;
    case SEL_12:   SeedMnemonicEntryView(12); break;
    case SEL_24:   SeedMnemonicEntryView(24); break;
    default:       not_yet("SeedElectrumMnemonicStartView"); break;
    }
}

static void select_seed(bool replace)
{
    if (flow.psbt == NULL) {
        ui_home_fresh();    /* "No transaction currently loaded" */
        return;
    }
    static char labels[SEED_STORAGE_MAX][16];
    view_spec_t v = { .name = "PSBTSelectSeedView", .title = "Select Signer",
                      .select = select_seed_select };
    for (int i = 0; i < storage_count(); i++) {
        char fp[9];
        seed_fingerprint_hex(storage_seed(i), fp);
        /* "(?)": this seed does not look like it can sign. */
        snprintf(labels[i], sizeof labels[i], can_sign_hint(i) ? "%s" : "%s (?)", fp);
        v.buttons[v.n_buttons++] = labels[i];
    }
    v.buttons[v.n_buttons++] = "Scan a seed";
    v.buttons[v.n_buttons++] = "Enter 12-word seed";
    v.buttons[v.n_buttons++] = "Enter 24-word seed";
    if (SETTINGS.electrum_seeds) {
        v.buttons[v.n_buttons++] = "Enter Electrum seed";
    }
    if (replace) {
        view_replace(&v);
    } else {
        view_push(&v);
    }
    /* A seed already chosen that looks right goes straight on. */
    if (flow.seed >= 0 && can_sign_hint(flow.seed)) {
        PSBTOverviewView();
    }
}

void PSBTSelectSeedView(void)
{
    select_seed(false);
}

bool psbt_flow_scanned(const uint8_t *psbt, size_t len)
{
    uint8_t *copy = malloc(len ? len : 1);
    if (copy == NULL) {
        return false;
    }
    memcpy(copy, psbt, len);
    const int seed = flow.seed;     /* chosen before the scan, from SeedOptionsView */
    psbt_flow_clear();
    flow.psbt = copy;
    flow.len = len;
    flow.seed = seed;
    select_seed(true);
    return true;
}

bool psbt_flow_resume(int seed)
{
    if (flow.psbt == NULL || !flow.resume || !can_sign_hint(seed)) {
        return false;
    }
    flow.resume = false;
    flow.seed = seed;
    PSBTOverviewView();
    return true;
}
