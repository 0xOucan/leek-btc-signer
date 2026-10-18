/*
 * SeedSigner's address verification, ported view by view: a scanned address
 * (from the Tools menu's ScanAddressView or the main Scan) is sorted by its
 * type into single sig or multisig, the seed to check it against is chosen
 * or loaded (SeedSelectSeedView), and SeedAddressVerificationView searches
 * the seed's receive and change addresses, index by index, until one
 * matches, with "Skip 10" to jump ahead.
 *
 * SeedSigner searches in a background thread; here the search runs on the
 * screen's tick, a few indexes at a time, so the keys stay live. The search
 * is unbounded, as SeedSigner's is; Cancel ends it.
 *
 * A multisig address is searched for in the wallet descriptor's addresses
 * instead: the descriptor is loaded first (LoadMultisigWalletDescriptorView,
 * resume_main_flow == FLOW__VERIFY_MULTISIG_ADDR) unless one already is.
 */
#include "views-address.h"

#include <stdio.h>
#include <string.h>

#include "screens.h"
#include "seed-storage.h"
#include "settings.h"
#include "theme.h"
#include "views-multisig.h"
#include "views-seed.h"
#include "views.h"
#include "widgets.h"

/* The Controller's unverified_address, and resume_main_flow ==
   FLOW__VERIFY_SINGLESIG_ADDR or FLOW__VERIFY_MULTISIG_ADDR. Addresses and
   xpubs are public. */
static struct {
    bool active;
    bool resume;
    bool ms_resume;
    char address[100];
    btc_script_t script;
    btc_net_t net;
    btc_sig_t sig;
    char path[48];
    char xpub[120];
    int seed;
    uint32_t index;         /* the next index to check */
    int sel;                /* the screen's button: 0 Skip 10, 1 Cancel */
} av;

/* Indexes checked per tick (receive and change each). About 2 ms an
   address on the ESP32-S3; a tick is 20 ms. */
#define CHECKS_PER_TICK 2
/* A multisig address derives every key: one index a tick. */
#define CHECKS_PER_TICK_MULTISIG 1

void address_flow_clear(void)
{
    av.active = false;
    av.resume = false;
    av.ms_resume = false;
}

/* SeedSigner's show_back_button=False. */
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

static const char *script_display(btc_script_t s)
{
    switch (s) {
    case BTC_SCRIPT_P2PKH:       return "Legacy";
    case BTC_SCRIPT_P2SH_P2WPKH: return "Nested Segwit";
    case BTC_SCRIPT_P2WPKH:      return "Native Segwit";
    case BTC_SCRIPT_P2TR:        return "Taproot";
    default:                     return "?";
    }
}

static const char *net_display(btc_net_t n)
{
    return n == BTC_NET_MAIN ? "Mainnet" : n == BTC_NET_TEST ? "Testnet" : "Regtest";
}

/* ---- SeedAddressVerificationSuccessView --------------------------------- */

static void success_select(int i)
{
    (void)i;
    ui_home_fresh();    /* Destination(MainMenuView) */
}

static void SeedAddressVerificationSuccessView(uint32_t index, bool is_change)
{
    char text[160];
    snprintf(text, sizeof text, "%s\n\n%s #%u", av.address, is_change ? "change address" : "receive address",
             (unsigned)index);
    const view_spec_t v = { .name = "SeedAddressVerificationSuccessView", .status = true,
                            .kind = NOTICE_SUCCESS, .title = "Success!", .headline = "Address Verified",
                            .text = text, .buttons = { "OK" }, .n_buttons = 1,
                            .select = success_select, .back = no_back };
    view_push(&v);
}

/* ---- SeedAddressVerificationView: the search ----------------------------- */

static void verify_render(const screen_t *s)
{
    (void)s;
    ui_draw_title("Verify Address");
    int y = THEME.top_h + THEME.pad;
    y = addr_draw(y, av.address, 1);
    char line[64];
    snprintf(line, sizeof line, "%s - %s%s%s", av.sig == BTC_SIG_SINGLE ? "Single Sig" : "Multisig",
             script_display(av.script), av.net == BTC_NET_MAIN ? "" : " ",
             av.net == BTC_NET_MAIN ? "" : net_display(av.net));
    y = ui_draw_wrapped(y + 2, line, C_MUTED);
    snprintf(line, sizeof line, "Checking address %u", (unsigned)av.index);
    gfx_text(THEME.pad, y + 2, line, THEME.body, C_FG);
    static const char *const labels[] = { "Skip 10", "Cancel" };
    const int by = GFX_H - 2 * THEME.row_h;
    for (int i = 0; i < 2; i++) {
        const int ry = by + i * THEME.row_h;
        if (i == av.sel) {
            gfx_fill_rect(THEME.pad, ry, GFX_W - 2 * THEME.pad, THEME.row_box_h, C_SEL_BG);
        }
        gfx_text_centered(0, GFX_W, ry + (THEME.row_box_h - gfx_font_height(THEME.body)) / 2,
                          labels[i], THEME.body, i == av.sel ? C_SEL_FG : C_FG);
    }
}

static bool verify_tick(const screen_t *s)
{
    (void)s;
    const bool multi = av.sig == BTC_SIG_MULTI;
    const ms_descriptor_t *d = multisig_descriptor();
    for (int k = 0; k < (multi ? CHECKS_PER_TICK_MULTISIG : CHECKS_PER_TICK); k++) {
        char receive[100], change[100];
        const uint32_t i = av.index;
        if (multi ? !d || !desc_address(d, i, false, av.net, receive, sizeof receive) ||
                        !desc_address(d, i, true, av.net, change, sizeof change)
                  : !btc_address(av.xpub, av.script, i, false, av.net, receive, sizeof receive) ||
                        !btc_address(av.xpub, av.script, i, true, av.net, change, sizeof change)) {
            return false;
        }
        const bool is_receive = strcmp(receive, av.address) == 0;
        if (is_receive || strcmp(change, av.address) == 0) {
            ui_note("verified %u %s %s", (unsigned)i, is_receive ? "receive" : "change", av.address);
            ui_pop();
            SeedAddressVerificationSuccessView(i, !is_receive);
            return true;
        }
        av.index++;
    }
    return true;
}

static bool verify_input(const screen_t *s, input_ev_t ev)
{
    (void)s;
    switch (ev) {
    case IN_UP:
    case IN_DOWN:
        av.sel ^= 1;
        return true;
    case IN_OK:
        if (av.sel == 0) {
            av.index += 10;     /* Skip 10 */
            ui_note("skip %u", (unsigned)av.index);
        } else {
            ui_home_fresh();    /* Cancel: Destination(MainMenuView) */
        }
        return true;
    case IN_BACK:
        ui_home_fresh();        /* no back button; it ends as Cancel does */
        return true;
    default:
        return false;
    }
}

static const screen_t VERIFY = {
    .name = "SeedAddressVerificationView", .render = verify_render, .input = verify_input,
    .tick = verify_tick,
};

/* `seed` -1: multisig, checked against the wallet descriptor. */
static void SeedAddressVerificationView(int seed, bool replace)
{
    if (seed >= 0) {
        const seed_t *s = storage_seed(seed);
        /* The xpub at the standard path, on the address's network. */
        if (s == NULL || !btc_xpub(s->bytes, sizeof s->bytes, av.path, av.net, 0, av.xpub, sizeof av.xpub)) {
            return;
        }
    } else if (multisig_descriptor() == NULL) {
        return;
    }
    av.seed = seed;
    av.index = 0;
    av.sel = 0;
    ui_note("verify %s %s", av.path, av.address);
    if (replace) {
        ui_replace(&VERIFY);
    } else {
        ui_push(&VERIFY);
    }
}

/* ---- SeedSelectSeedView (flow: verify single sig address) ---------------- */

enum { SRC_SCAN, SRC_12, SRC_24, SRC_ELECTRUM };

static void select_seed_select(int i)
{
    const int n = storage_count();
    if (i < n) {
        SeedAddressVerificationView(i, false);
        return;
    }
    av.resume = true;
    switch (i - n) {
    case SRC_SCAN: ui_push(&SCREEN_SCAN); break;
    case SRC_12:   SeedMnemonicEntryView(12); break;
    case SRC_24:   SeedMnemonicEntryView(24); break;
    default:       not_yet("SeedElectrumMnemonicStartView"); break;
    }
}

static void SeedSelectSeedView(bool replace)
{
    static char labels[SEED_STORAGE_MAX][9];
    view_spec_t v = { .name = "SeedSelectSeedView", .title = "Verify Address",
                      .text = storage_count() ? "Select seed to verify" : "Load the seed to verify",
                      .select = select_seed_select };
    for (int i = 0; i < storage_count(); i++) {
        seed_fingerprint_hex(storage_seed(i), labels[i]);
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
}

bool address_verify_resume(int seed)
{
    if (!av.active || !av.resume) {
        return false;
    }
    av.resume = false;
    SeedAddressVerificationView(seed, false);
    return true;
}

const char *address_multisig_resuming(void)
{
    return av.active && av.ms_resume ? av.address : NULL;
}

void address_multisig_verify(void)
{
    av.ms_resume = false;
    SeedAddressVerificationView(-1, false);
}

static bool set_path(void);

/* Multisig: straight to the search with a descriptor loaded; else load
   one first, and come back. */
static void verify_multisig(bool replace)
{
    av.sig = BTC_SIG_MULTI;
    if (!set_path()) {
        return;
    }
    if (multisig_descriptor()) {
        SeedAddressVerificationView(-1, replace);
    } else {
        av.ms_resume = true;
        LoadMultisigWalletDescriptorView(replace);
    }
}

/* ---- AddressVerificationSigTypeView -------------------------------------- */

static bool set_path(void)
{
    return btc_standard_path(av.net, av.sig, av.script, av.path, sizeof av.path);
}

static void sig_type_select(int i)
{
    if (i == 0) {
        av.sig = BTC_SIG_SINGLE;
        if (set_path()) {
            SeedSelectSeedView(false);
        }
    } else {
        verify_multisig(false);
    }
}

static void sig_type_back(void)
{
    av.active = false;      /* unverified_address = None */
    ui_pop();
}

static void AddressVerificationSigTypeView(void)
{
    const view_spec_t v = { .name = "AddressVerificationSigTypeView", .title = "Verify Address",
                            .text = "Sig type can't be auto-detected from this address. Please specify:",
                            .buttons = { "Single Sig", "Multisig" }, .n_buttons = 2,
                            .select = sig_type_select, .back = sig_type_back };
    view_replace(&v);   /* skip_current_view, in the scan's place */
}

/* ---- AddressVerificationStartView ---------------------------------------- */

void address_scanned(const char *address, btc_script_t script, btc_net_t net)
{
    address_flow_clear();
    av.active = true;
    snprintf(av.address, sizeof av.address, "%s", address);
    av.script = script;
    av.net = net;
    ui_note("address-start %s %d", av.address, (int)script);
    switch (script) {
    case BTC_SCRIPT_P2SH_P2WPKH:
        /* No way to tell single sig from multisig. */
        AddressVerificationSigTypeView();
        return;
    case BTC_SCRIPT_P2WPKH:
        if (strlen(address) >= 62) {
            /* P2WSH: multisig (mainnet and testnet are 62, regtest 64). */
            verify_multisig(true);
            return;
        }
        break;
    case BTC_SCRIPT_P2PKH:
    case BTC_SCRIPT_P2TR:
        break;
    default:
        ui_pop();
        not_yet("AddressVerificationStartView for this address");
        return;
    }
    av.sig = BTC_SIG_SINGLE;
    if (set_path()) {
        SeedSelectSeedView(true);
    }
}
