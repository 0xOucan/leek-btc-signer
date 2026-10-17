/*
 * Scan: SeedSigner's ScanView, on top of the DecodeQR port.
 *
 * Frames arrive through qr-source (the camera on the board, the harness in
 * QEMU) and go to the decoder between key presses. The screen shows how far
 * an animated code has got, as SeedSigner's does (the weighted estimate), and
 * leaves on the first COMPLETE or INVALID, as SeedSigner's scan loop does. A
 * stray frame of another type, which SeedSigner would raise on, is ignored.
 *
 * Where the result goes next is Phase 4's job (load the seed, review the
 * PSBT, verify the address). Until then the result screen says what was
 * scanned, and the decoder, the frames and the seed are wiped on the way out.
 * The flavours mirror ScanView, ScanPSBTView and ScanSeedQRView: the last two
 * refuse a code of the wrong kind with SeedSigner's "Wrong QR Type".
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
#include "theme.h"
#include "wally_core.h"
#include "wally_crypto.h"

typedef enum { SCAN_ANY, SCAN_PSBT, SCAN_SEED } scan_kind_t;

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
static const screen_t SCAN_RESULT = SCREEN_MESSAGE("SCAN_RESULT", result_msg);

static void result(const char *title, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

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
           (scan.kind == SCAN_SEED && qr_is_seed(t));
}

/* Describe a complete scan. Nothing secret goes in the text or the note. */
static void describe(void)
{
    const qr_type_t t = scan.dec->type;
    if (!kind_ok(t)) {
        result("Error", "Wrong QR Type\n\nExpected a %s, received \"%s\" format.",
               scan.kind == SCAN_PSBT ? "transaction" : "SeedQR", qr_type_name(t));
        ui_note("wrong-type %s", qr_type_name(t));
        return;
    }
    if (qr_is_seed(t)) {
        uint16_t words[SEED_MAX_WORDS];
        const size_t n = qr_decoder_seed(scan.dec, words);
        seed_t *s = sec_alloc(sizeof *s);
        const bool ok = s && seed_from_indices(s, words, n, "") == SEED_OK;
        char fp[9] = "";
        if (ok) {
            seed_fingerprint_hex(s, fp);
        }
        if (s) {
            seed_clear(s);
            sec_free(s, sizeof *s);
        }
        wally_bzero(words, sizeof words);
        if (!ok) {
            result("Error", "Invalid seed\n\nThe %u words do not form a valid BIP-39 mnemonic.",
                   (unsigned)n);
            ui_note("seed-invalid %u", (unsigned)n);
            return;
        }
        result("Seed", "%u-word seed\nFingerprint %s\n\nLoading it arrives with T34.",
               (unsigned)n, fp);
        ui_note("seed %u %s", (unsigned)n, fp);
        return;
    }
    if (qr_is_psbt(t)) {
        size_t n = 0;
        const size_t cap = qr_decoder_psbt_max(scan.dec);
        uint8_t *p = cap ? malloc(cap) : NULL;
        const bool ok = p && qr_decoder_psbt(scan.dec, p, cap, &n);
        uint8_t h[32] = { 0 };
        if (ok) {
            wally_sha256(p, n, h, sizeof h);
        }
        free(p);
        if (!ok) {
            result("Error", "The transaction could not be read.");
            ui_note("psbt-unreadable");
            return;
        }
        result("PSBT", "Transaction received\n%u bytes\n\nReview and signing arrive with T37.",
               (unsigned)n);
        ui_note("psbt %u %02x%02x%02x%02x", (unsigned)n, h[0], h[1], h[2], h[3]);
        return;
    }
    if (t == QR_TYPE_BITCOIN_ADDRESS) {
        btc_script_t script;
        btc_net_t net;
        const char *a = qr_decoder_address(scan.dec, &script, &net);
        result("Address", "%s\n\nVerifying it arrives with T40.", a);
        ui_note("address %s %d %d", a, (int)script, (int)net);
        return;
    }
    if (t == QR_TYPE_SETTINGS) {
        result("Settings", "SettingsQR received.\n\nApplying it arrives with T41.");
        ui_note("settings");
        return;
    }
    if (t == QR_TYPE_SIGN_MESSAGE) {
        result("Sign message", "Request received.\n\nMessage signing arrives with T49.");
        ui_note("signmessage");
        return;
    }
    result("Scan", "Received \"%s\".\n\nUsing it arrives with T47.", qr_type_name(t));
    ui_note("other %s", qr_type_name(t));
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
    ui_draw_title(scan.kind == SCAN_PSBT ? "Scan PSBT" : scan.kind == SCAN_SEED ? "Scan SeedQR" : "Scan");
    int y = THEME.top_h + THEME.pad + 2;
    if (scan.no_memory) {
        ui_draw_wrapped(y, "Not enough memory to scan.", C_WARN);
        return;
    }
    y = ui_draw_wrapped(y, scan.kind == SCAN_PSBT ? "Scan Transaction"
                           : scan.kind == SCAN_SEED ? "Scan SeedQR" : "Scan a QR code", C_FG);
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
        if (st == QR_STATUS_COMPLETE) {
            describe();
            ui_replace(&SCAN_RESULT);       /* leaves this screen: wipes */
            return true;
        }
        if (st == QR_STATUS_INVALID) {
            result("Error", "Unknown QR Type\n\nQRCode is invalid or is a data format not yet supported.");
            ui_note("invalid");
            ui_replace(&SCAN_RESULT);
            return true;
        }
        if (st == QR_STATUS_UNSUPPORTED) {
            result("Not yet", "Wallet descriptors arrive with multisig, T47.");
            ui_note("unsupported %s", qr_type_name(scan.dec->type));
            ui_replace(&SCAN_RESULT);
            return true;
        }
        scan.pct = qr_decoder_percent(scan.dec, true);
    }
    return changed;
}

const screen_t SCREEN_SCAN = {
    .name = "SCAN", .enter = scan_enter, .render = scan_render, .leave = scan_leave,
    .tick = scan_tick, .ctx = (void *)(intptr_t)SCAN_ANY,
};
const screen_t SCREEN_SCAN_PSBT = {
    .name = "SCAN_PSBT", .enter = scan_enter, .render = scan_render, .leave = scan_leave,
    .tick = scan_tick, .ctx = (void *)(intptr_t)SCAN_PSBT,
};
const screen_t SCREEN_SCAN_SEED = {
    .name = "SCAN_SEED", .enter = scan_enter, .render = scan_render, .leave = scan_leave,
    .tick = scan_tick, .ctx = (void *)(intptr_t)SCAN_SEED,
};
