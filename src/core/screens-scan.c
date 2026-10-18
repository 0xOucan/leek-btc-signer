/*
 * Scan: SeedSigner's ScanView, on top of the DecodeQR port.
 *
 * Frames arrive through qr-source (the camera on the board, the harness in
 * QEMU) and go to the decoder between key presses. The screen shows how far
 * an animated code has got, as SeedSigner's does (the weighted estimate), and
 * leaves on the first COMPLETE or INVALID, as SeedSigner's scan loop does. A
 * stray frame of another type, which SeedSigner would raise on, is ignored.
 *
 * A seed goes on to be finalised, a PSBT to be reviewed and signed, a
 * wallet descriptor to be loaded (views-seed.c, views-psbt.c,
 * views-multisig.c), and so on. A crypto-output UR's descriptor is not
 * decoded (qr-decode.h); the result screen says so. The decoder, the frames
 * and the seed are wiped on the way out.
 * The flavours mirror ScanView, ScanPSBTView, ScanSeedQRView,
 * ScanAddressView and ScanWalletDescriptorView: all but the first refuse a
 * code of the wrong kind with SeedSigner's "Wrong QR Type".
 */
#include "screens.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "btc.h"
#include "qr-decode.h"
#include "qr-source.h"
#include "secmem.h"
#include "seed-storage.h"
#include "settings.h"
#include "views-address.h"
#include "views-message.h"
#include "views-multisig.h"
#include "views-settings.h"
#include "views-psbt.h"
#include "views-seed.h"
#include "theme.h"
#include "wally_core.h"
#include "wally_crypto.h"

/* SCAN_CONFIRM is SeedTranscribeSeedQRConfirmScanView: any code ends it,
   and views-seed.c judges whether it is the SeedQR just transcribed. */
typedef enum { SCAN_ANY, SCAN_PSBT, SCAN_SEED, SCAN_CONFIRM, SCAN_ADDRESS, SCAN_DESCRIPTOR } scan_kind_t;

/* Largest arena tried first: PSRAM on the board (ESP-IDF's malloc puts
   anything over 16 KB there); QEMU has none, so it falls back. */
#define SCAN_ARENA_BIG   (1024 * 1024)
#define SCAN_ARENA_SMALL (96 * 1024)
#define SCAN_ARENA_MIN   (40 * 1024)

static struct {
    scan_kind_t kind;
    qr_decoder_t *dec;
    uint8_t *arena;
    size_t arena_cap;
    uint8_t frame[QR_SOURCE_FRAME_MAX];
    int pct;
    bool no_memory;
} scan;

/* ---- The result --------------------------------------------------------- */

static char result_title[24];
static char result_body[200];
static const message_t result_msg = { result_title, result_body };
/* Named after the SeedSigner View it stands for, set with the text. */
/* ScanInvalidQRTypeView ends at the main menu, dropping any flow in
   progress, as SeedSigner's does; the other results go back. */
static bool result_input(const screen_t *s, input_ev_t ev)
{
    if (ev == IN_OK && strcmp(s->name, "ScanInvalidQRTypeView") == 0) {
        ui_home_fresh();
        return true;
    }
    return message_input(s, ev);
}

static screen_t SCAN_RESULT = {
    .name = "NotYetImplementedView", .render = message_render, .input = result_input,
    .ctx = (void *)&result_msg,
};

static void result(const char *title, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void result_view(const char *view)
{
    SCAN_RESULT.name = view;
}

static void result(const char *title, const char *fmt, ...)
{
    snprintf(result_title, sizeof result_title, "%s", title);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(result_body, sizeof result_body, fmt, ap);
    va_end(ap);
}

static bool kind_ok(qr_type_t t)
{
    return scan.kind == SCAN_ANY || (scan.kind == SCAN_PSBT && qr_is_psbt(t)) ||
           (scan.kind == SCAN_SEED && qr_is_seed(t)) ||
           (scan.kind == SCAN_ADDRESS && t == QR_TYPE_BITCOIN_ADDRESS) ||
           (scan.kind == SCAN_DESCRIPTOR && qr_is_wallet_descriptor(scan.dec));
}

/* Each flavour's invalid_qr_type_message. */
static const char *expected(void)
{
    switch (scan.kind) {
    case SCAN_PSBT:       return "Expected a transaction";
    case SCAN_ADDRESS:    return "Expected an address QR";
    case SCAN_DESCRIPTOR: return "Expected a wallet descriptor QR";
    default:              return "Expected a SeedQR";
    }
}

typedef enum { ROUTE_RESULT, ROUTE_SEED, ROUTE_DONE } route_t;

/* Route a complete scan. A seed goes on to SeedFinalizeView and a PSBT to
   PSBTSelectSeedView, as from SeedSigner's ScanView; anything else gets a
   result screen. Nothing secret goes in a text or a note. */
static route_t describe(void)
{
    const qr_type_t t = scan.dec->type;
    if (!kind_ok(t)) {
        /* The QRType as SeedSigner words it: "__" as ": ", "_" as " ". */
        char type[40];
        size_t k = 0;
        for (const char *p = qr_type_name(t); *p && k + 2 < sizeof type; p++) {
            if (p[0] == '_' && p[1] == '_') {
                type[k++] = ':';
                type[k++] = ' ';
                p++;
            } else {
                type[k++] = *p == '_' ? ' ' : *p;
            }
        }
        type[k] = '\0';
        result("Error", "Wrong QR Type\n\n%s, received \"%s\" format", expected(), type);
        result_view("ErrorView");
        ui_note("wrong-type %s", qr_type_name(t));
        return ROUTE_RESULT;
    }
    if (qr_is_seed(t)) {
        uint16_t words[SEED_MAX_WORDS];
        const size_t n = qr_decoder_seed(scan.dec, words);
        seed_t *s = sec_alloc(sizeof *s);
        const bool ok = s && seed_from_indices(s, words, n, "") == SEED_OK;
        char fp[9] = "";
        if (ok) {
            seed_fingerprint_hex(s, fp);
            storage_set_pending(s);     /* set_pending_seed() */
        }
        if (s) {
            seed_clear(s);
            sec_free(s, sizeof *s);
        }
        wally_bzero(words, sizeof words);
        if (!ok) {
            /* SeedSigner raises here (Seed() refuses the checksum). */
            result("Error", "Invalid seed\n\nThe %u words do not form a valid BIP-39 mnemonic.",
                   (unsigned)n);
            result_view("ErrorView");
            ui_note("seed-invalid %u", (unsigned)n);
            return ROUTE_RESULT;
        }
        ui_note("seed %u %s", (unsigned)n, fp);
        return ROUTE_SEED;
    }
    if (qr_is_psbt(t)) {
        size_t n = 0;
        /* The bound is generous for a zlib BBQr (17 times its compressed
           size); without PSRAM it may not be there, so take the largest
           buffer below it that is. */
        size_t cap = qr_decoder_psbt_max(scan.dec);
        uint8_t *p = NULL;
        while (cap > 0 && (p = malloc(cap)) == NULL && cap >= 2048) {
            cap /= 2;
        }
        const bool ok = p && qr_decoder_psbt(scan.dec, p, cap, &n);
        uint8_t h[32] = { 0 };
        if (ok) {
            wally_sha256(p, n, h, sizeof h);
        }
        if (!ok) {
            free(p);
            result("Error", "The transaction could not be read.");
            result_view("ErrorView");
            ui_note("psbt-unreadable");
            return ROUTE_RESULT;
        }
        ui_note("psbt %u %02x%02x%02x%02x", (unsigned)n, h[0], h[1], h[2], h[3]);
        /* This replaces the scan screen, which wipes the decoder. */
        const bool routed = psbt_flow_scanned(p, n);
        free(p);
        if (!routed) {
            result("Error", "Not enough memory for the transaction.");
            result_view("ErrorView");
            return ROUTE_RESULT;
        }
        return ROUTE_DONE;
    }
    if (qr_is_wallet_descriptor(scan.dec)) {
        if (t == QR_TYPE_OUTPUT_UR) {
            result("Scan", "A crypto-output descriptor is not decoded by this port.");
            result_view("NotYetImplementedView");
            ui_note("other %s", qr_type_name(t));
            return ROUTE_RESULT;
        }
        char *text = malloc(DESC_TEXT_MAX);
        const bool ok = text && qr_decoder_descriptor(scan.dec, text, DESC_TEXT_MAX);
        ui_note("descriptor-scan %s %s", qr_type_name(t), ok ? "ok" : "none");
        /* Leaves this screen, which wipes the decoder; the text is ours. */
        multisig_scanned(ok ? text : NULL);
        free(text);
        return ROUTE_DONE;
    }
    if (t == QR_TYPE_BITCOIN_ADDRESS) {
        btc_script_t script;
        btc_net_t net;
        const char *a = qr_decoder_address(scan.dec, &script, &net);
        ui_note("address %s %d %d", a, (int)script, (int)net);
        /* AddressVerificationStartView, in this screen's place. */
        address_scanned(a, script, net);
        return ROUTE_DONE;
    }
    if (t == QR_TYPE_SETTINGS) {
        const char *text;
        size_t len;
        if (qr_decoder_settings(scan.dec, &text, &len)) {
            ui_note("settings");
            /* SettingsIngestSettingsQRView, in this screen's place. */
            settings_scanned(text, len);
            return ROUTE_DONE;
        }
        result("Error", "The SettingsQR could not be read.");
        result_view("ErrorView");
        return ROUTE_RESULT;
    }
    if (t == QR_TYPE_SIGN_MESSAGE) {
        const char *path, *msg;
        size_t path_len, msg_len;
        if (qr_decoder_sign_message(scan.dec, &path, &path_len, &msg, &msg_len)) {
            /* SeedSignMessageStartView, in this screen's place. */
            message_scanned(path, path_len, msg, msg_len);
            return ROUTE_DONE;
        }
        result("Error", "The sign message request could not be read.");
        result_view("ErrorView");
        return ROUTE_RESULT;
    }
    result("Scan", "Received \"%s\".\n\nThis port does nothing with it yet.", qr_type_name(t));
    result_view("NotYetImplementedView");
    ui_note("other %s", qr_type_name(t));
    return ROUTE_RESULT;
}

/* ---- The scan screen ---------------------------------------------------- */

static void scan_free(void)
{
    if (scan.dec) {
        qr_decoder_reset(scan.dec);
        sec_free(scan.dec, sizeof *scan.dec);
    }
    free(scan.arena);
    scan.dec = NULL;
    scan.arena = NULL;
    wally_bzero(scan.frame, sizeof scan.frame);
    qr_source_drain();
}

static void scan_enter(const screen_t *s)
{
    scan.kind = (scan_kind_t)(intptr_t)s->ctx;
    scan.pct = 0;
    scan.no_memory = false;
    qr_source_drain();          /* nothing seen before the screen opened */
    /* The decoder holds a scanned seed's words: internal RAM only. The
       arena never holds a seed (qr-decode.h) and may be PSRAM. */
    scan.dec = sec_alloc(sizeof *scan.dec);
    scan.arena_cap = SCAN_ARENA_BIG;
    scan.arena = malloc(scan.arena_cap);
    if (scan.arena == NULL) {
        scan.arena_cap = SCAN_ARENA_SMALL;
        scan.arena = malloc(scan.arena_cap);
    }
    if (scan.arena == NULL) {
        scan.arena_cap = SCAN_ARENA_MIN;
        scan.arena = malloc(scan.arena_cap);
    }
    if (scan.dec == NULL || scan.arena == NULL) {
        scan_free();
        scan.no_memory = true;
        return;
    }
    qr_decoder_init(scan.dec, scan.arena, scan.arena_cap);
}

static void scan_leave(const screen_t *s)
{
    (void)s;
    scan_free();
}

static void scan_render(const screen_t *s)
{
    (void)s;
    ui_draw_title(scan.kind == SCAN_PSBT ? "Scan PSBT" : scan.kind == SCAN_SEED ? "Scan SeedQR"
                  : scan.kind == SCAN_CONFIRM ? "Confirm SeedQR"
                  : scan.kind == SCAN_ADDRESS ? "Verify Address"
                  : scan.kind == SCAN_DESCRIPTOR ? "Multisig" : "Scan");
    int y = THEME.top_h + THEME.pad + 2;
    if (scan.no_memory) {
        ui_draw_wrapped(y, "Not enough memory to scan.", C_WARN);
        return;
    }
    y = ui_draw_wrapped(y, scan.kind == SCAN_PSBT ? "Scan Transaction"
                           : scan.kind == SCAN_SEED ? "Scan SeedQR"
                           : scan.kind == SCAN_CONFIRM ? "Scan your SeedQR"
                           : scan.kind == SCAN_ADDRESS ? "Scan address QR"
                           : scan.kind == SCAN_DESCRIPTOR ? "Scan descriptor" : "Scan a QR code", C_FG);
    if (scan.dec && scan.dec->type != QR_TYPE_NONE && scan.pct > 0) {
        /* SeedSigner's progress bar, under the text. */
        const int bar_y = GFX_H - THEME.line_h - 8;
        const int w = GFX_W - 2 * THEME.pad;
        gfx_fill_rect(THEME.pad, bar_y, w, 6, C_MUTED);
        gfx_fill_rect(THEME.pad, bar_y, w * scan.pct / 100, 6, C_ACCENT);
        char pct[8];
        snprintf(pct, sizeof pct, "%d%%", scan.pct);
        gfx_text(THEME.pad, bar_y + 9, pct, THEME.body, C_FG);
    }
    (void)y;
}

static bool scan_tick(const screen_t *s)
{
    (void)s;
    if (scan.dec == NULL) {
        return false;
    }
    bool changed = false;
    size_t len;
    /* A couple of frames per tick keeps the keys responsive. */
    for (int i = 0; i < 2 && qr_source_next(scan.frame, sizeof scan.frame, &len); i++) {
        const qr_status_t st = qr_decoder_add(scan.dec, scan.frame, len);
        wally_bzero(scan.frame, len);
        ui_note("frame %d %s %d", (int)st, qr_type_name(scan.dec->type),
                qr_decoder_percent(scan.dec, true));
        changed = true;
        if (scan.kind == SCAN_CONFIRM &&
            (st == QR_STATUS_COMPLETE || st == QR_STATUS_INVALID)) {
            uint16_t words[SEED_MAX_WORDS];
            size_t n = 0;
            if (st == QR_STATUS_COMPLETE && qr_is_seed(scan.dec->type)) {
                n = qr_decoder_seed(scan.dec, words);
            }
            ui_note("confirm-scan %s", n ? "seed" : qr_type_name(scan.dec->type));
            /* Replaces this screen, which wipes the decoder; the words are
               compared, then wiped. */
            transcribe_confirm_scanned(words, n);
            wally_bzero(words, sizeof words);
            return true;
        }
        if (st == QR_STATUS_COMPLETE) {
            const route_t r = describe();
            if (r == ROUTE_RESULT) {
                ui_replace(&SCAN_RESULT);   /* leaves this screen: wipes */
            } else if (r == ROUTE_SEED) {
                ui_pop();
                if (SETTINGS.passphrase == OPT_REQUIRED) {
                    SeedAddPassphraseView();
                } else {
                    SeedFinalizeView();
                }
            }
            return true;
        }
        if (st == QR_STATUS_INVALID) {
            result_view("ScanInvalidQRTypeView");
            result("Error", "Unknown QR Type\n\nQRCode is invalid or is a data format not yet supported.");
            ui_note("invalid");
            ui_replace(&SCAN_RESULT);
            return true;
        }
        scan.pct = qr_decoder_percent(scan.dec, true);
    }
    return changed;
}

const screen_t SCREEN_SCAN = {
    .name = "ScanView", .enter = scan_enter, .render = scan_render, .leave = scan_leave,
    .tick = scan_tick, .ctx = (void *)(intptr_t)SCAN_ANY,
};
const screen_t SCREEN_SCAN_PSBT = {
    .name = "ScanPSBTView", .enter = scan_enter, .render = scan_render, .leave = scan_leave,
    .tick = scan_tick, .ctx = (void *)(intptr_t)SCAN_PSBT,
};
const screen_t SCREEN_SCAN_ADDRESS = {
    .name = "ScanAddressView", .enter = scan_enter, .render = scan_render, .leave = scan_leave,
    .tick = scan_tick, .ctx = (void *)(intptr_t)SCAN_ADDRESS,
};
const screen_t SCREEN_SCAN_DESCRIPTOR = {
    .name = "ScanWalletDescriptorView", .enter = scan_enter, .render = scan_render, .leave = scan_leave,
    .tick = scan_tick, .ctx = (void *)(intptr_t)SCAN_DESCRIPTOR,
};
const screen_t SCREEN_SCAN_CONFIRM_SEEDQR = {
    .name = "SeedTranscribeSeedQRConfirmScanView", .enter = scan_enter, .render = scan_render,
    .leave = scan_leave, .tick = scan_tick, .ctx = (void *)(intptr_t)SCAN_CONFIRM,
};
const screen_t SCREEN_SCAN_SEED = {
    .name = "ScanSeedQRView", .enter = scan_enter, .render = scan_render, .leave = scan_leave,
    .tick = scan_tick, .ctx = (void *)(intptr_t)SCAN_SEED,
};
