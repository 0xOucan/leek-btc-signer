/*
 * SeedSigner's multisig wallet descriptor views, ported: the descriptor is
 * scanned (ScanWalletDescriptorView, or any scan), checked and kept as the
 * Controller's multisig_wallet_descriptor, then shown with its policy and
 * the keys' fingerprints, and a button back to whichever flow asked for it
 * (resume_main_flow): a PSBT's change, an address to verify, or the address
 * explorer. Going home drops it, as MainMenuView does.
 *
 * SeedSigner's one resume_main_flow is a flag in each flow's module here;
 * MultisigWalletDescriptorView asks them in the order SeedSigner's elifs do.
 */
#include "views-multisig.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "screens.h"
#include "views-address.h"
#include "views-psbt.h"
#include "views-tools.h"
#include "views.h"

static ms_descriptor_t *loaded;

const ms_descriptor_t *multisig_descriptor(void)
{
    return loaded;
}

void multisig_flow_clear(void)
{
    if (loaded) {
        desc_free(loaded);
        free(loaded);
        loaded = NULL;
    }
}

static void push(const view_spec_t *v, bool replace)
{
    if (replace) {
        view_replace(v);
    } else {
        view_push(v);
    }
}

/* ---- LoadMultisigWalletDescriptorView ------------------------------------ */

/* show_back_button=False */
static void no_back(void)
{
}

static void load_select(int i)
{
    if (i == 0) {
        ui_push(&SCREEN_SCAN_DESCRIPTOR);
    } else if (psbt_flow_resuming()) {
        ui_pop();           /* Destination(BackStackView) */
    } else {
        ui_home_fresh();    /* Destination(MainMenuView) */
    }
}

void LoadMultisigWalletDescriptorView(bool replace)
{
    const view_spec_t v = {
        .name = "LoadMultisigWalletDescriptorView", .title = "Multisig Verification",
        .text = "Load your multisig wallet descriptor to verify your receive/self-transfer or change address.",
        .buttons = { "Scan descriptor", "Cancel" }, .n_buttons = 2, .select = load_select, .back = no_back,
    };
    push(&v, replace);
}

/* ---- MultisigWalletDescriptorView ------------------------------------------ */

enum { MS_OK, MS_RETURN, MS_VERIFY, MS_EXPLORER };

static char ms_text[48 + DESC_KEYS_MAX * 9];
static char ms_verify_label[24];

static void ms_back(void)
{
    multisig_flow_clear();  /* multisig_wallet_descriptor = None */
    ui_pop();
}

static void ms_select(int i);

/* The View as SeedSigner builds it each time it runs: the button follows
   resume_main_flow as it is now (`ok_only`: as it will be, cleared). */
static void ms_spec(view_spec_t *v, bool ok_only)
{
    size_t k = (size_t)snprintf(ms_text, sizeof ms_text, "Policy\n%d of %d\nSigning Keys\n", loaded->threshold,
                                loaded->n);
    for (int j = 0; j < loaded->n && j < DESC_KEYS_MAX && k < sizeof ms_text; j++) {
        const uint8_t *f = loaded->fingerprints[j];
        k += (size_t)snprintf(ms_text + k, sizeof ms_text - k, "%s%02x%02x%02x%02x", j ? " " : "", f[0], f[1],
                              f[2], f[3]);
    }
    const char *addr;
    int kind = MS_OK;
    const char *label = "OK";
    if (ok_only) {
        /* resume_main_flow = None */
    } else if (psbt_flow_resuming()) {
        kind = MS_RETURN;
        label = "Return to transaction";
    } else if ((addr = address_multisig_resuming()) != NULL) {
        kind = MS_VERIFY;
        snprintf(ms_verify_label, sizeof ms_verify_label, "Verify addr %.7s", addr);
        label = ms_verify_label;
    } else if (tools_explorer_resuming()) {
        kind = MS_EXPLORER;
        label = "Address explorer";
    }
    *v = (view_spec_t){ .name = "MultisigWalletDescriptorView", .title = "Descriptor Loaded", .text = ms_text,
                        .buttons = { label }, .n_buttons = 1, .select = ms_select, .back = ms_back,
                        .arg = kind };
}

static void ms_select(int i)
{
    (void)i;
    const int kind = view_arg();
    if (kind == MS_OK) {
        ui_home_fresh();
        return;
    }
    /* Each clears resume_main_flow, so were the user to come back here, the
       View would run again with just "OK": it is rebuilt so before the
       destination goes on top. */
    view_spec_t v;
    ms_spec(&v, true);
    view_replace(&v);
    switch (kind) {
    case MS_RETURN:
        psbt_flow_return_to_change();
        break;
    case MS_VERIFY:
        address_multisig_verify();
        break;
    default:
        tools_explorer_descriptor();
        break;
    }
}

void MultisigWalletDescriptorView(bool replace)
{
    if (loaded == NULL) {
        return;
    }
    view_spec_t v;
    ms_spec(&v, false);
    push(&v, replace);
    ui_note("descriptor %d of %d", loaded->threshold, loaded->n);
}

/* ---- ScanView's descriptor branch ------------------------------------------ */

static void not_yet(void)
{
    const view_spec_t v = { .name = "NotYetImplementedView", .title = "Work In Progress",
                            .text = "Single sig descriptors are not supported.", .status = true,
                            .kind = NOTICE_INFO, .buttons = { "OK" }, .n_buttons = 1 };
    view_push(&v);
}

void multisig_scanned(const char *text)
{
    char *rw = text ? malloc(DESC_TEXT_MAX) : NULL;
    ms_descriptor_t *d = calloc(1, sizeof *d);
    /* The scan has done its part: its decoder goes now. */
    ui_pop();
    if (text == NULL || rw == NULL || d == NULL) {
        /* get_wallet_descriptor() gave None, or not a str. */
        free(rw);
        free(d);
        UnhandledExceptionView("AttributeError", "scan_views.py, run", "The descriptor could not be read.");
        return;
    }
    /* The rewrite falls back to the descriptor as scanned, as ScanView's
       except does. */
    if (!desc_scanview_rewrite(text, rw, DESC_TEXT_MAX)) {
        snprintf(rw, DESC_TEXT_MAX, "%s", text);
    }
    const bool ok = desc_parse(rw, d);
    free(rw);
    if (!ok) {
        free(d);
        UnhandledExceptionView("DescriptorError", "descriptor.py, from_string", "Invalid wallet descriptor.");
        return;
    }
    if (!d->basic) {
        desc_free(d);
        free(d);
        ui_note("descriptor not-basic");
        not_yet();
        return;
    }
    multisig_flow_clear();
    loaded = d;
    MultisigWalletDescriptorView(false);    /* the scan is already gone */
}
