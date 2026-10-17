/*
 * The Scan screens, driven as the device drives them: frames go into the
 * qr-source mailbox, the UI loop ticks, and the screen's note says what it
 * concluded. Every complete scan SeedSigner recorded in
 * vectors/seedsigner_qr.h is replayed through the real screen, so the path
 * from camera to result is checked, not only the decoder beneath it.
 */
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "btc-init.h"
#include "qr-source.h"
#include "screens.h"
#include "secmem.h"
#include "ui.h"
#include "wally_crypto.h"
#include "vectors/seedsigner_qr.h"

#define N(a) (sizeof(a) / sizeof((a)[0]))

static char last_note[200], seed_note[200];

static void note(const char *n)
{
    snprintf(last_note, sizeof last_note, "%s", n);
    if (strncmp(n, "seed", 4) == 0) {
        snprintf(seed_note, sizeof seed_note, "%s", n);
    }
}

/* A recording allocator: what the screens ask sec_alloc() for. */
static unsigned sec_calls, sec_internal, sec_live;
static void *rec_alloc(size_t n, bool internal_only)
{
    sec_calls++;
    sec_internal += internal_only;
    sec_live++;
    return calloc(1, n);
}
static void rec_free(void *p)
{
    sec_live--;
    free(p);
}
static const sec_backend_t RECORDING = { rec_alloc, rec_free };

static void rand_fill(void *p, unsigned n)
{
    memset(p, 0x5A, n);
}

/* What the main loop does for one scanned frame. */
static void feed(const char *data, unsigned len)
{
    CHECK(qr_source_push((const uint8_t *)data, len), "frame dropped");
    for (int i = 0; i < 4; i++) {
        ui_tick();
        ui_render();
    }
}

static void open_scan(void)
{
    ui_init(&SCREEN_HOME);
    ui_input(IN_OK);
    CHECK(strcmp(ui_top()->name, "ScanView") == 0, "Scan did not open");
}

static uint8_t *psbt;

static void test_recorded_scans(void)
{
    printf("== every complete PSBT and seed scan SeedSigner recorded, through the screen\n");
    unsigned psbts = 0, seeds = 0;
    for (size_t i = 0; i < N(QR_SCANS); i++) {
        const qrv_scan_t *s = &QR_SCANS[i];
        const bool is_psbt = s->psbt && s->psbt[0] != '!';
        if (!s->complete || !(is_psbt || s->seed)) {
            continue;
        }
        open_scan();
        for (unsigned f = 0; f < s->n_frames && strcmp(ui_top()->name, "ScanView") == 0; f++) {
            feed(s->frames[f].data, s->frames[f].len);
        }
        if (is_psbt) {
            const bool readable = strcmp(s->name, "bbqr_truncated_zlib") != 0;
            /* A PSBT goes on to choose its signer, as from SeedSigner's ScanView. */
            CHECK(strcmp(ui_top()->name, readable ? "PSBTSelectSeedView" : "ErrorView") == 0,
                  "%s: on %s", s->name, ui_top()->name);
            const size_t n = strlen(s->psbt) / 2;
            for (size_t k = 0; k < n; k++) {
                unsigned v;
                sscanf(s->psbt + 2 * k, "%2x", &v);
                psbt[k] = (uint8_t)v;
            }
            uint8_t h[32];
            wally_sha256(psbt, n, h, sizeof h);
            char want[64];
            snprintf(want, sizeof want, "psbt %u %02x%02x%02x%02x", (unsigned)n, h[0], h[1], h[2], h[3]);
            /* The one deliberate difference on this path (qr-decode.h): a
               truncated zlib stream gives no PSBT, where SeedSigner hands
               its parser a prefix. Everything else arrives as SeedSigner
               got it, unparseable or not; review is where that is judged. */
            if (strcmp(s->name, "bbqr_truncated_zlib") == 0) {
                snprintf(want, sizeof want, "psbt-unreadable");
            }
            CHECK(strcmp(last_note, want) == 0, "%s: note \"%s\", want \"%s\"", s->name, last_note, want);
            psbts++;
        } else {
            const bool valid = strncmp(seed_note, "seed ", 5) == 0;
            CHECK(valid || strncmp(seed_note, "seed-invalid", 12) == 0, "%s: note \"%s\"", s->name, seed_note);
            /* A valid seed goes on to be finalised, as from SeedSigner's ScanView. */
            CHECK(strcmp(ui_top()->name, valid ? "SeedFinalizeView" : "ErrorView") == 0,
                  "%s: on %s", s->name, ui_top()->name);
            seeds++;
        }
        ui_input(IN_BACK);
        CHECK(strcmp(ui_top()->name, "MainMenuView") == 0, "%s: BACK did not go home", s->name);
    }
    printf("   %u PSBT scans and %u seed scans end on the right result\n", psbts, seeds);
}

static void test_flows(void)
{
    printf("== seed, wrong type, invalid, stray frames, leaving\n");
    static const uint8_t zero[16] = { 0 };

    open_scan();
    feed((const char *)zero, sizeof zero);
    CHECK(strcmp(seed_note, "seed 12 73c5da0a") == 0, "CompactSeedQR: %s", seed_note);

    /* Scan a SeedQR refuses an address with SeedSigner's message. */
    ui_init(&SCREEN_HOME);
    ui_input(IN_DOWN);
    ui_input(IN_OK);
    ui_input(IN_OK);
    CHECK(strcmp(ui_top()->name, "ScanSeedQRView") == 0, "Scan a SeedQR did not open");
    const char *a = "bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu";
    feed(a, (unsigned)strlen(a));
    CHECK(strcmp(last_note, "wrong-type bitcoin_address") == 0, "wrong type: %s", last_note);
    ui_input(IN_BACK);
    CHECK(strcmp(ui_top()->name, "LoadSeedView") == 0, "BACK from the result");

    open_scan();
    feed("hello world", 11);
    CHECK(strcmp(last_note, "invalid") == 0, "invalid: %s", last_note);

    /* A frame of another type mid-scan is ignored; the scan goes on. */
    const qrv_scan_t *ur = NULL;
    for (size_t i = 0; i < N(QR_SCANS) && !ur; i++) {
        if (strcmp(QR_SCANS[i].name, "ur_psbt_3_medium_in_order") == 0) {
            ur = &QR_SCANS[i];
        }
    }
    CHECK(ur && ur->n_frames > 2, "vector missing");
    if (ur) {
        open_scan();
        feed(ur->frames[0].data, ur->frames[0].len);
        feed(a, (unsigned)strlen(a));
        CHECK(strcmp(ui_top()->name, "ScanView") == 0, "a stray frame ended the scan");
        CHECK(strncmp(last_note, "frame 6 ", 8) == 0, "stray frame note: %s", last_note);
        for (unsigned f = 1; f < ur->n_frames; f++) {
            feed(ur->frames[f].data, ur->frames[f].len);
        }
        CHECK(strncmp(last_note, "psbt ", 5) == 0, "scan did not finish after a stray frame");
    }

    /* BACK during a scan leaves it, and HOLD from anywhere goes home. */
    open_scan();
    if (ur) {
        feed(ur->frames[0].data, ur->frames[0].len);
    }
    ui_input(IN_BACK);
    CHECK(strcmp(ui_top()->name, "MainMenuView") == 0, "BACK did not leave the scan");
    open_scan();
    ui_input(IN_HOLD);
    CHECK(strcmp(ui_top()->name, "MainMenuView") == 0, "HOLD did not go home");

    /* Frames that arrived before the screen opened are not decoded. */
    ui_init(&SCREEN_HOME);
    qr_source_push(zero, sizeof zero);
    last_note[0] = '\0';
    open_scan();
    ui_tick();
    CHECK(last_note[0] == '\0', "a stale frame was decoded: %s", last_note);
}

static void test_secret_placement(void)
{
    printf("== secrets come from internal RAM and go back wiped\n");
    static const uint8_t zero[16] = { 0 };
    ui_init(&SCREEN_HOME);      /* close any scan the last test left open */
    sec_set_backend(&RECORDING);
    sec_calls = sec_internal = sec_live = 0;
    open_scan();
    feed((const char *)zero, sizeof zero);
    CHECK(strcmp(seed_note, "seed 12 73c5da0a") == 0, "seed scan: %s", seed_note);
    CHECK(sec_calls >= 2, "the decoder and the seed did not come from sec_alloc (%u calls)", sec_calls);
    CHECK(sec_internal == sec_calls, "a secret was allowed outside internal RAM");
    CHECK(sec_live == 0, "%u secret allocations were not freed", sec_live);
    ui_input(IN_BACK);
    sec_set_backend(NULL);
}

int main(void)
{
    btc_init(rand_fill);
    ui_set_note_hook(note);
    psbt = test_alloc(10 * 1024);
    if (psbt == NULL) {
        CHECK(0, "out of memory");
        return CHECK_DONE("test_scan");
    }
    test_flows();
    test_secret_placement();
    test_recorded_scans();
    test_free_all();
    return CHECK_DONE("test_scan");
}
