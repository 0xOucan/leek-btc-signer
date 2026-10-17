/*
 * QR detection and decoding against SeedSigner's DecodeQR.
 *
 * Every vector in vectors/seedsigner_qr.h was produced by running SeedSigner:
 * the scans in its own tests/test_decodepsbtqr.py, captured frame by frame,
 * and generated scans fed to the same DecodeQR. For each frame the port must
 * return SeedSigner's status, hold SeedSigner's type and report SeedSigner's
 * progress; for each scan it must end complete or not as SeedSigner did and
 * hand back the same PSBT bytes, seed words, address, settings or message.
 *
 * Wallet descriptor scans are decoded with T47; they are only counted here.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "btc-init.h"
#include "qr-decode.h"
#include "wally_psbt.h"
#include "vectors/seedsigner_qr.h"

#define N(a) (sizeof(a) / sizeof((a)[0]))

/* Heap, not static: these suites also run in the QEMU self-test image,
   which has no PSRAM (about 60 KB in one block there). The host uses the
   same 48 KB, so the decoder's capacity, and its results, cannot differ by
   platform; the vectors never need more mixed parts than it holds. */
#define ARENA_SIZE (48 * 1024)
#define PSBT_MAX (16 * 1024)
static qr_decoder_t *dec_p;
#define dec (*dec_p)
static uint8_t *arena;
static uint8_t *psbt;

static void rand_fill(void *p, unsigned n)
{
    memset(p, 0x5A, n);
}

static bool deferred(qr_type_t t)
{
    return t == QR_TYPE_WALLET_SPECTER || t == QR_TYPE_WALLET_GENERIC ||
           t == QR_TYPE_WALLET_CONFIGFILE;
}

/* `b` is the bytes `h` spells in hex. */
static bool hex_is(const uint8_t *b, size_t n, const char *h)
{
    static const char D[] = "0123456789abcdef";
    if (strlen(h) != 2 * n) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (h[2 * i] != D[b[i] >> 4] || h[2 * i + 1] != D[b[i] & 15]) {
            return false;
        }
    }
    return true;
}

static int preview(const char *data, unsigned len, char *out, size_t cap)
{
    size_t k = 0;
    for (unsigned i = 0; i < len && k + 5 < cap && i < 48; i++) {
        unsigned char c = (unsigned char)data[i];
        k += (size_t)snprintf(out + k, cap - k, (c >= 0x20 && c < 0x7f) ? "%c" : "\\x%02x", c);
    }
    out[k] = '\0';
    return (int)k;
}

static void test_detect(void)
{
    printf("== detect_segment_type\n");
    unsigned bad = 0;
    for (size_t i = 0; i < N(QR_DETECT); i++) {
        const qrv_detect_t *v = &QR_DETECT[i];
        const qr_type_t t = qr_detect((const uint8_t *)v->data, v->len);
        if (t != (qr_type_t)v->type && bad++ < 10) {
            char p[256];
            preview(v->data, v->len, p, sizeof p);
            CHECK(0, "detect #%zu \"%s\" (%u bytes): %s, SeedSigner says %s", i, p, v->len,
                  qr_type_name(t), qr_type_name((qr_type_t)v->type));
        }
    }
    CHECK(bad == 0, "%u of %zu detections differ", bad, N(QR_DETECT));
    printf("   %zu frames detected as SeedSigner detects them\n", N(QR_DETECT) - bad);
}

/* True if the hex is a PSBT libwally parses. */
static bool hex_parses(const char *h)
{
    size_t n = strlen(h) / 2;
    if (n > PSBT_MAX) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(h + 2 * i, "%2x", &v);
        psbt[i] = (uint8_t)v;
    }
    struct wally_psbt *p = NULL;
    if (wally_psbt_from_bytes(psbt, n, 0, &p) != WALLY_OK) {
        return false;
    }
    wally_psbt_free(p);
    return true;
}

static void test_scans(void)
{
    printf("== scans, frame by frame\n");
    unsigned frames = 0, scans = 0, skipped = 0, psbts = 0, seeds = 0, addrs = 0, refused_early = 0;
    static char got[256];

    for (size_t i = 0; i < N(QR_SCANS); i++) {
        const qrv_scan_t *s = &QR_SCANS[i];
        if (deferred((qr_type_t)s->type)) {
            skipped++;
            continue;
        }
        qr_decoder_reset(&dec);
        bool ok = true;
        for (unsigned f = 0; f < s->n_frames && ok; f++) {
            const qrv_frame_t *fr = &s->frames[f];
            const qr_status_t st = qr_decoder_add(&dec, (const uint8_t *)fr->data, fr->len);
            frames++;
            if (st != (qr_status_t)fr->status || dec.type != (qr_type_t)fr->type) {
                char p[256];
                preview(fr->data, fr->len, p, sizeof p);
                CHECK(0, "%s frame %u \"%s\": status %d type %s, SeedSigner %d %s", s->name, f, p,
                      st, qr_type_name(dec.type), fr->status, qr_type_name((qr_type_t)fr->type));
                ok = false;
                break;
            }
            if (fr->pct >= 0) {
                const int pct = qr_decoder_percent(&dec, false);
                const int pctw = qr_decoder_percent(&dec, true);
                CHECK(pct == fr->pct, "%s frame %u: %d%%, SeedSigner %d%%", s->name, f, pct, fr->pct);
                /* The weighted estimate sums doubles in a different order; a
                   last-bit difference can move it across a percent. */
                CHECK(abs(pctw - fr->pctw) <= 1, "%s frame %u: weighted %d%%, SeedSigner %d%%",
                      s->name, f, pctw, fr->pctw);
            }
        }
        if (!ok) {
            continue;
        }
        scans++;
        CHECK(dec.complete == s->complete, "%s: complete %d, SeedSigner %d", s->name,
              dec.complete, s->complete);

        if (qr_is_psbt((qr_type_t)s->type)) {
            size_t n = 0;
            /* The bound the screens size their buffer by must suffice. */
            const size_t bound = qr_decoder_psbt_max(&dec);
            const bool have = qr_decoder_psbt(&dec, psbt, bound < PSBT_MAX ? bound : PSBT_MAX, &n);
            if (s->psbt == NULL || s->psbt[0] == '!') {
                CHECK(!have, "%s: gave a PSBT where SeedSigner gives %s", s->name,
                      s->psbt ? s->psbt : "None");
            } else if (!have && !hex_parses(s->psbt)) {
                /* SeedSigner hands over bytes no PSBT parser takes (a
                   truncated zlib stream inflates to a prefix); this refuses
                   them a step earlier. Either way there is no PSBT. */
                refused_early++;
            } else {
                CHECK(have, "%s: no PSBT", s->name);
                if (have) {
                    CHECK(hex_is(psbt, n, s->psbt), "%s: PSBT bytes differ", s->name);
                    psbts++;
                }
            }
        }
        if (s->seed != NULL) {
            uint16_t w[SEED_MAX_WORDS];
            const size_t n = qr_decoder_seed(&dec, w);
            size_t k = 0;
            for (size_t j = 0; j < n; j++) {
                k += (size_t)sprintf(got + k, "%s%s", j ? " " : "", BIP39_WORDLIST[w[j]]);
            }
            got[k] = '\0';
            CHECK(strcmp(got, s->seed) == 0, "%s: seed \"%s\", SeedSigner \"%s\"", s->name, got, s->seed);
            seeds++;
        }
        if (s->address != NULL) {
            btc_script_t script;
            btc_net_t net;
            const char *a = qr_decoder_address(&dec, &script, &net);
            CHECK(a && strcmp(a, s->address) == 0, "%s: address %s, SeedSigner %s", s->name,
                  a ? a : "none", s->address);
            CHECK(a && script == (btc_script_t)s->addr_script && net == (btc_net_t)s->addr_net,
                  "%s: address type differs", s->name);
            addrs++;
        }
        if (s->settings != NULL) {
            const char *t;
            size_t n;
            CHECK(qr_decoder_settings(&dec, &t, &n) && n == strlen(s->settings) &&
                  memcmp(t, s->settings, n) == 0, "%s: settings differ", s->name);
        }
        if (s->msg != NULL) {
            const char *path, *msg;
            size_t pl, ml;
            const bool have = qr_decoder_sign_message(&dec, &path, &pl, &msg, &ml);
            CHECK(have && pl == strlen(s->msg_path) && memcmp(path, s->msg_path, pl) == 0,
                  "%s: message path differs", s->name);
            CHECK(have && ml == strlen(s->msg) && memcmp(msg, s->msg, ml) == 0,
                  "%s: message differs", s->name);
        }
        if (s->ur_cbor != NULL) {
            size_t n = 0;
            const uint8_t *c = qr_decoder_ur_cbor(&dec, &n);
            CHECK(c && hex_is(c, n, s->ur_cbor), "%s: UR CBOR differs", s->name);
        }
    }
    printf("   %u scans, %u frames: every status, type and result as SeedSigner's\n", scans, frames);
    printf("   (%u PSBTs, %u seeds, %u addresses; %u descriptor scans wait for T47)\n",
           psbts, seeds, addrs, skipped);
    printf("   %u unparseable PSBT refused before the parser, as listed in qr-decode.h\n",
           refused_early);
}

/* Where the port is deliberately not SeedSigner. */
static void test_differences(void)
{
    printf("== deliberate differences\n");

    /* A frame of another type mid-scan: SeedSigner raises. */
    qr_decoder_reset(&dec);
    const char *a = "bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu";
    const char *m = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
    CHECK(qr_decoder_add(&dec, (const uint8_t *)a, strlen(a)) == QR_STATUS_COMPLETE, "address");
    CHECK(qr_decoder_add(&dec, (const uint8_t *)m, strlen(m)) == QR_STATUS_TYPE_CHANGED,
          "a type change was not reported");
    CHECK(qr_decoder_address(&dec, NULL, NULL) != NULL, "the stray frame disturbed the result");

    /* Wallet descriptors: detected, not decoded yet. */
    qr_decoder_reset(&dec);
    const char *w = "wsh(sortedmulti(1,xpub/0/*))";
    CHECK(qr_decoder_add(&dec, (const uint8_t *)w, strlen(w)) == QR_STATUS_UNSUPPORTED,
          "a descriptor was not reported unsupported");

    /* No frame at all. */
    CHECK(qr_decoder_add(&dec, NULL, 0) == QR_STATUS_FALSE, "NULL is FALSE");

    /* A frame longer than any QR code can hold is not a PSBT or anything. */
    uint8_t *big = malloc(8000);
    if (big) {
        memset(big, 'A', 8000);
        CHECK(qr_detect(big, 8000) == QR_TYPE_INVALID, "an oversized frame was detected");
        free(big);
    }

    /* A seed never reaches the arena, which may be PSRAM (secmem.h): only
       the decoder, in internal RAM, holds its words. */
    static const uint8_t compact[16] = { 0x5b, 0xbd, 0x9d, 0x71, 0xa8, 0xec, 0x79, 0x90,
                                         0x83, 0x1a, 0xff, 0x35, 0x9d, 0x42, 0x65, 0x45 };
    const char *seedqrs[] = { m, "073318950739065415961602009907670428187212261116",
                              "aban aban aban aban aban aban aban aban aban aban aban abou" };
    for (int k = 0; k < 4; k++) {
        qr_decoder_reset(&dec);
        const qr_status_t st = k < 3 ? qr_decoder_add(&dec, (const uint8_t *)seedqrs[k], strlen(seedqrs[k]))
                                     : qr_decoder_add(&dec, compact, sizeof compact);
        CHECK(st == QR_STATUS_COMPLETE, "seed %d did not decode", k);
        bool untouched = true;
        for (size_t i = 0; i < ARENA_SIZE; i++) {
            untouched = untouched && arena[i] == 0;
        }
        CHECK(untouched, "seed %d was copied into the arena", k);
    }

    /* Reset wipes the arena. */
    qr_decoder_reset(&dec);
    qr_decoder_add(&dec, (const uint8_t *)"settings::v1 name=x", 19);
    qr_decoder_reset(&dec);
    bool clean = true;
    for (size_t i = 0; i < 256; i++) {
        clean = clean && arena[i] == 0;
    }
    CHECK(clean, "reset left the scanned seed in the arena");
}

int main(void)
{
    btc_init(rand_fill);
    dec_p = test_alloc(sizeof *dec_p);
    arena = test_alloc(ARENA_SIZE);
    psbt = test_alloc(PSBT_MAX);
    CHECK(dec_p && arena && psbt, "out of memory");
    if (!dec_p || !arena || !psbt) {
        test_free_all();
        return CHECK_DONE("test_qr_decode");
    }
    qr_decoder_init(&dec, arena, ARENA_SIZE);
    test_detect();
    test_scans();
    test_differences();
    qr_decoder_reset(&dec);
    test_free_all();
    return CHECK_DONE("test_qr_decode");
}
