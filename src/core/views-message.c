/*
 * SeedSigner's message signing, ported view by view. A `signmessage`
 * request (a derivation path and an ASCII message) is scanned; the setting
 * must be on (OptionDisabledView otherwise) and the path's network must
 * match the network setting (NetworkMismatchErrorView otherwise); the seed
 * is chosen or loaded (SeedSelectSeedView); the message is reviewed page by
 * page, then the address the path gives; and the signature is shown as a QR.
 *
 * SeedSigner pages the message as a View per page; here one screen keeps
 * the page, and BACK on a page returns to the one before, as SeedSigner's
 * back stack does, without spending a stack slot per page.
 */
#include "views-message.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "btc.h"
#include "qr-encode.h"
#include "screens.h"
#include "seed-storage.h"
#include "settings.h"
#include "theme.h"
#include "views-seed.h"
#include "views-settings.h"
#include "views.h"

#define MSG_MAX 2048
#define PAGES_MAX 64

/* The Controller's sign_message_data, and resume_main_flow ==
   FLOW__SIGN_MESSAGE. A message and its signature are public. */
static struct {
    bool resume;
    int seed;                   /* -1: none chosen yet */
    char path[96];
    char *message;              /* malloc'd while the flow lasts */
    btc_msg_path_t parsed;
    int page, n_pages;
    uint16_t page_start[PAGES_MAX + 1];
    qr_encoder_t *enc;
    uint8_t *qr_buf;
} sm = { .seed = -1 };

void message_flow_clear(void)
{
    sm.resume = false;
    sm.seed = -1;
    free(sm.message);
    sm.message = NULL;
}

static void no_back(void)
{
}

static void not_yet_text(const char *text)
{
    const view_spec_t v = { .name = "NotYetImplementedView", .title = "Work In Progress", .text = text,
                            .status = true, .kind = NOTICE_INFO, .buttons = { "OK" }, .n_buttons = 1 };
    view_replace(&v);
}

/* ---- OptionDisabledView ---------------------------------------------------- */

static void option_disabled_select(int i)
{
    if (i == 0) {
        settings_open_entry("message_signing");     /* clear_history */
    } else {
        ui_home_fresh();
    }
}

static void OptionDisabledView(const char *attr_name)
{
    static char text[96];
    const int id = setting_by_attr(attr_name);
    snprintf(text, sizeof text, "\"%s\" is currently disabled in Settings.",
             id >= 0 ? SETTINGS_DEF[id].display_name : attr_name);
    const view_spec_t v = { .name = "OptionDisabledView", .status = true, .kind = NOTICE_WARNING,
                            .title = "Option Disabled", .text = text,
                            .buttons = { "Update setting", "Back to Main Menu" }, .n_buttons = 2,
                            .select = option_disabled_select, .back = no_back };
    view_replace(&v);
}

/* ---- NetworkMismatchErrorView --------------------------------------------- */

static void mismatch_select(int i)
{
    (void)i;
    settings_open_entry("network");                 /* clear_history */
}

static void NetworkMismatchErrorView(const char *path)
{
    static const char *const names[] = { "Mainnet", "Testnet", "Regtest" };
    static char text[160];
    snprintf(text, sizeof text, "Current network setting (%s) doesn't match %s.", names[SETTINGS.network], path);
    const view_spec_t v = { .name = "NetworkMismatchErrorView", .status = true, .kind = NOTICE_ERROR,
                            .title = "Network Mismatch", .text = text, .buttons = { "Change Setting" },
                            .n_buttons = 1, .select = mismatch_select, .back = no_back };
    view_replace(&v);
}

/* ---- SeedSignMessageSignedMessageQRView ------------------------------------ */

static void signed_leave(void)
{
    if (sm.enc) {
        qr_encoder_wipe(sm.enc);
    }
    free(sm.enc);
    free(sm.qr_buf);
    sm.enc = NULL;
    sm.qr_buf = NULL;
    message_flow_clear();
    ui_after(ui_home_fresh);    /* Destination(MainMenuView) */
}

#define SIG_QR_BUF 160

static void SeedSignMessageSignedMessageQRView(void)
{
    const seed_t *s = storage_seed(sm.seed);
    char sig[100];
    if (s == NULL || sm.message == NULL ||
        !btc_sign_message(s, sm.path, (const uint8_t *)sm.message, strlen(sm.message), sig, sizeof sig)) {
        return;
    }
    sm.enc = malloc(sizeof *sm.enc);
    sm.qr_buf = malloc(SIG_QR_BUF);
    if (!sm.enc || !sm.qr_buf || !qr_encode_text(sm.enc, sig, sm.qr_buf, SIG_QR_BUF)) {
        free(sm.enc);
        free(sm.qr_buf);
        sm.enc = NULL;
        sm.qr_buf = NULL;
        return;
    }
    ui_note("message-signed %s", sig);
    ui_show_qr("SeedSignMessageSignedMessageQRView", sm.enc, signed_leave);
}

/* ---- SeedSignMessageConfirmAddressView -------------------------------------- */

static void confirm_address_select(int i)
{
    (void)i;
    SeedSignMessageSignedMessageQRView();
}

static void SeedSignMessageConfirmAddressView(void)
{
    const seed_t *s = storage_seed(sm.seed);
    /* A testnet path takes the setting's network (testnet or regtest). */
    const btc_net_t net = sm.parsed.net == 0 ? BTC_NET_MAIN : SETTINGS.network;
    char xpub[120], address[100];
    if (s == NULL || !btc_xpub(s->bytes, sizeof s->bytes, sm.parsed.wallet, net, 0, xpub, sizeof xpub) ||
        !btc_address(xpub, sm.parsed.script, (uint32_t)sm.parsed.index, sm.parsed.is_change == 1, net, address,
                     sizeof address)) {
        return;
    }
    char text[240];
    snprintf(text, sizeof text, "derivation path\n%s\n\naddress\n%s", sm.path, address);
    const view_spec_t v = { .name = "SeedSignMessageConfirmAddressView", .title = "Confirm Address", .text = text,
                            .buttons = { "Sign message" }, .n_buttons = 1, .select = confirm_address_select };
    view_push(&v);
    ui_note("message-address %s", address);
}

/* ---- SeedSignMessageConfirmMessageView: the message, page by page ---------- */

static int body_lines(void)
{
    const int top = THEME.top_h + THEME.pad;
    const int bottom = GFX_H - THEME.row_box_h - 2;
    const int n = (bottom - top) / THEME.line_h;
    return n < 1 ? 1 : n;
}

/* The width-wrapped line starting at `p`: its length, and where the next
   starts. Breaks at a space or newline, mid-word when a word is wider. */
static size_t line_at(const char *p, const char **next)
{
    const int avail = GFX_W - 2 * THEME.pad;
    size_t fit = 0, brk = 0;
    char probe[96];
    while (p[fit] && p[fit] != '\n' && fit + 1 < sizeof probe) {
        memcpy(probe, p, fit + 1);
        probe[fit + 1] = '\0';
        if (gfx_text_width(probe, THEME.body) > avail) {
            break;
        }
        if (p[fit] == ' ') {
            brk = fit;
        }
        fit++;
    }
    size_t take = (p[fit] && p[fit] != '\n' && brk > 0) ? brk : fit;
    if (take == 0 && p[0] && p[0] != '\n') {
        take = 1;
    }
    const char *q = p + take;
    if (*q == ' ' || *q == '\n') {
        q++;
    }
    *next = q;
    return take;
}

/* Page breaks, as SeedSigner's reflow_text_into_pages does for its screen. */
static void paginate(void)
{
    const int lines = body_lines();
    const char *p = sm.message;
    sm.n_pages = 0;
    sm.page_start[0] = 0;
    while (sm.n_pages < PAGES_MAX) {
        sm.page_start[sm.n_pages++] = (uint16_t)(p - sm.message);
        for (int l = 0; l < lines && *p; l++) {
            const char *next;
            line_at(p, &next);
            p = next;
        }
        if (!*p) {
            break;
        }
    }
    sm.page_start[sm.n_pages] = (uint16_t)strlen(sm.message);
}

static void review_render(const screen_t *s)
{
    (void)s;
    char title[48];
    if (sm.n_pages <= 1) {
        snprintf(title, sizeof title, "Review Message");
    } else {
        snprintf(title, sizeof title, "Message (pt %d/%d)", sm.page + 1, sm.n_pages);
    }
    ui_draw_title(title);
    const char *p = sm.message + sm.page_start[sm.page];
    const char *end = sm.message + sm.page_start[sm.page + 1];
    int y = THEME.top_h + THEME.pad;
    for (int l = 0; l < body_lines() && p < end && *p; l++) {
        const char *next;
        const size_t n = line_at(p, &next);
        char line[96];
        snprintf(line, sizeof line, "%.*s", (int)n, p);
        gfx_text(THEME.pad, y, line, THEME.body, C_FG);
        y += THEME.line_h;
        p = next;
    }
    const int by = GFX_H - THEME.row_box_h - 1;
    gfx_fill_rect(THEME.pad, by, GFX_W - 2 * THEME.pad, THEME.row_box_h, C_SEL_BG);
    gfx_text_centered(0, GFX_W, by + (THEME.row_box_h - gfx_font_height(THEME.body)) / 2, "Next", THEME.body,
                      C_SEL_FG);
}

static bool review_input(const screen_t *s, input_ev_t ev)
{
    (void)s;
    if (ev == IN_OK) {
        if (sm.page + 1 < sm.n_pages) {
            sm.page++;
            ui_note("message page %d/%d", sm.page + 1, sm.n_pages);
        } else {
            SeedSignMessageConfirmAddressView();
        }
        return true;
    }
    if (ev == IN_BACK) {
        if (sm.page > 0) {
            sm.page--;
            ui_note("message page %d/%d", sm.page + 1, sm.n_pages);
        } else {
            /* Leaving the flow entirely. */
            message_flow_clear();
            ui_pop();
        }
        return true;
    }
    return ev == IN_UP || ev == IN_DOWN;
}

static const screen_t CONFIRM_MESSAGE = {
    .name = "SeedSignMessageConfirmMessageView", .render = review_render, .input = review_input,
};

static void SeedSignMessageConfirmMessageView(bool replace)
{
    sm.page = 0;
    paginate();
    ui_note("message page 1/%d", sm.n_pages);
    if (replace) {
        ui_replace(&CONFIRM_MESSAGE);
    } else {
        ui_push(&CONFIRM_MESSAGE);
    }
}

/* ---- SeedSelectSeedView (flow: sign message) ------------------------------- */

enum { SRC_SCAN, SRC_12, SRC_24, SRC_ELECTRUM };

static void select_seed_select(int i)
{
    const int n = storage_count();
    if (i < n) {
        sm.seed = i;
        SeedSignMessageConfirmMessageView(false);
        return;
    }
    sm.resume = true;
    switch (i - n) {
    case SRC_SCAN: ui_push(&SCREEN_SCAN); break;
    case SRC_12:   SeedMnemonicEntryView(12); break;
    case SRC_24:   SeedMnemonicEntryView(24); break;
    default:       SeedElectrumMnemonicStartView(); break;
    }
}

static void SeedSelectSeedView(void)
{
    static char labels[SEED_STORAGE_MAX][9];
    view_spec_t v = { .name = "SeedSelectSeedView", .title = "Sign Message",
                      .text = storage_count() ? "Select seed to sign with" : "Load the seed to sign with",
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
    view_replace(&v);       /* skip_current_view, in the start view's place */
}

/* ---- SeedSignMessageStartView ---------------------------------------------- */

void message_scanned(const char *path, size_t path_len, const char *msg, size_t msg_len)
{
    if (!SETTINGS.message_signing) {
        message_flow_clear();
        OptionDisabledView("message_signing");
        return;
    }
    snprintf(sm.path, sizeof sm.path, "%.*s", (int)path_len, path);
    const char *err = btc_parse_derivation_path(sm.path, &sm.parsed);
    if (err) {
        /* A multisig path, or too short a one: SeedSigner raises. */
        message_flow_clear();
        UnhandledExceptionView(err, "embit_utils.py, parse_derivation_path", sm.path);
        return;
    }
    if (!sm.parsed.clean || sm.parsed.custom) {
        message_flow_clear();
        not_yet_text("Signing messages for custom derivation paths not supported");
        return;
    }
    if ((sm.parsed.net == 0) != (SETTINGS.network == BTC_NET_MAIN)) {
        sm.resume = false;
        NetworkMismatchErrorView(sm.path);
        return;
    }
    free(sm.message);
    const size_t n = msg_len < MSG_MAX ? msg_len : MSG_MAX;
    sm.message = malloc(n + 1);
    if (sm.message == NULL) {
        return;
    }
    memcpy(sm.message, msg, n);
    sm.message[n] = '\0';
    ui_note("signmessage %s %u", sm.path, (unsigned)n);
    if (sm.seed >= 0 && storage_seed(sm.seed)) {
        SeedSignMessageConfirmMessageView(true);
    } else {
        SeedSelectSeedView();
    }
}

void message_from_seed(int seed)
{
    message_flow_clear();
    sm.seed = seed;
    sm.resume = true;
    ui_push(&SCREEN_SCAN);
}

bool message_resume(int seed)
{
    if (!sm.resume || sm.message == NULL) {
        return false;
    }
    sm.resume = false;
    sm.seed = seed;
    SeedSignMessageConfirmMessageView(false);
    return true;
}
