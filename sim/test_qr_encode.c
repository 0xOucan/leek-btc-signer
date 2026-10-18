/*
 * QR output against SeedSigner's encoders, and read back.
 *
 * Every part SeedSigner's SeedQrEncoder, CompactSeedQrEncoder,
 * StaticXpubQrEncoder, SpecterLegacyXPubQrEncoder, UrXpubQrEncoder and
 * UrPsbtQrEncoder produce for the vectors in vectors/seedsigner_qr.h must
 * come out of the port byte for byte; fountain parts past the first cycle
 * included.
 *
 * Then the parts are drawn as QR codes with src/core/qrcode.c and read back
 * by quirc, an independent decoder, and the whole stream is fed to the
 * port's own decoder: what the device shows is what a camera gets, and what
 * a camera gets is what was meant.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "btc-init.h"
#include "qr-decode.h"
#include "qr-encode.h"
#include "qrcode.h"
#include "quirc.h"
#include "vectors/seedsigner_qr.h"

#define N(a) (sizeof(a) / sizeof((a)[0]))

/* Sized for the vectors (the largest PSBT is 8.3 KB) and kept off the static
   image where large, so the suite also runs in the QEMU self-test image. */
#define ARENA_SIZE (48 * 1024)
/* Sized to the vectors (the largest PSBT is 5,663 bytes): the self-test
   image holds every suite in internal RAM, without PSRAM, and this suite's
   buffers are most of what is free when it runs. */
#define BUF_SIZE (8 * 1024)
#define PART_SIZE 4096
#define PSBT_SIZE (6 * 1024)
static uint8_t *buf, *part, *raw, *got, *modules;
static char *h;
static uint8_t *arena;
static qr_decoder_t *dec_p;
static qr_encoder_t *enc_p;
#define dec (*dec_p)
#define enc (*enc_p)
static struct quirc *qr;
/* quirc's canvas, sized once at the start for the largest code read back
   here (version 13 at two pixels a module, with its quiet zone) and drawn
   into from the top-left. Resizing per code reallocated about 90 KB each
   time, which the self-test image, holding every suite's state in internal
   RAM without PSRAM, could not always get: read-backs then failed on
   memory, not on decoding. */
#define CANVAS ((17 + 4 * 13 + 8) * 2)
static int canvas = CANVAS;
/* quirc's decode result (about 9 KB), allocated once with the rest: taken
   per code, it was what ran out on Xtensa once the suite's own buffers
   held most of the internal RAM. */
static struct quirc_data *qd;

static void rand_fill(void *p, unsigned n)
{
    memset(p, 0x5A, n);
}

static size_t unhex(const char *h, uint8_t *out)
{
    size_t n = strlen(h) / 2;
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(h + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
    return n;
}

static void hex(const uint8_t *b, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++) {
        sprintf(out + 2 * i, "%02x", b[i]);
    }
    out[2 * n] = '\0';
}

static bool load(seed_t *s, const char *mnemonic, const char *pass)
{
    return seed_from_mnemonic(s, mnemonic, pass) == SEED_OK;
}

/* Draw `data` as a QR code at `scale` pixels per module with a four-module
   quiet zone, and read it back with quirc. True if the payload matches. */
static unsigned versions_seen[QRCODE_MAX_VERSION + 1];
static bool round_trip(const uint8_t *data, size_t len, int scale)
{
    QRCode code;
    const int v = qrcode_fit(&code, modules, QRCODE_MAX_VERSION, ECC_LOW, data, (uint16_t)len);
    if (v < 0) {
        return false;
    }
    versions_seen[v]++;
    const int side = (code.size + 8) * scale;
    if (side > canvas) {
        if (quirc_resize(qr, side, side) < 0) {
            CHECK(0, "out of memory for a %d px QR canvas", side);
            return false;
        }
        canvas = side;
    }
    int w, h;
    uint8_t *img = quirc_begin(qr, &w, &h);
    memset(img, 255, (size_t)w * (size_t)h);
    for (int y = 0; y < code.size; y++) {
        for (int x = 0; x < code.size; x++) {
            if (!qrcode_getModule(&code, (uint8_t)x, (uint8_t)y)) {
                continue;
            }
            for (int dy = 0; dy < scale; dy++) {
                memset(img + (size_t)((y + 4) * scale + dy) * (size_t)w + (size_t)(x + 4) * (size_t)scale,
                       0, (size_t)scale);
            }
        }
    }
    quirc_end(qr);
    /* Random data can look like a finder pattern, so quirc may report a
       phantom code beside the real one: any code that decodes right counts. */
    bool found = false;
    for (int i = 0; qd != NULL && !found && i < quirc_count(qr); i++) {
        struct quirc_code qc;
        quirc_extract(qr, i, &qc);
        found = quirc_decode(&qc, qd) == QUIRC_SUCCESS && (size_t)qd->payload_len == len &&
                memcmp(qd->payload, data, len) == 0;
    }
    return found;
}

static void test_seedqr(void)
{
    printf("== SeedQR and CompactSeedQR\n");
    seed_t s;
    for (size_t i = 0; i < N(QR_SEEDQR); i++) {
        const qrv_seedqr_t *v = &QR_SEEDQR[i];
        CHECK(load(&s, v->mnemonic, ""), "seed %zu", i);

        CHECK(qr_encode_seedqr(&enc, &s, buf, BUF_SIZE), "SeedQR %zu", i);
        size_t n = qr_encoder_next_part(&enc, part, PART_SIZE);
        CHECK(n == strlen(v->standard) && memcmp(part, v->standard, n) == 0,
              "SeedQR %zu differs from SeedSigner's", i);
        CHECK(qr_encoder_seq_len(&enc) == 1, "SeedQR is static");
        CHECK(round_trip(part, n, 2), "SeedQR %zu did not read back", i);
        qr_decoder_reset(&dec);
        CHECK(qr_decoder_add(&dec, part, n) == QR_STATUS_COMPLETE, "SeedQR %zu did not decode", i);

        CHECK(qr_encode_compact_seedqr(&enc, &s, buf, BUF_SIZE), "Compact %zu", i);
        n = qr_encoder_next_part(&enc, part, PART_SIZE);
        hex(part, n, h);
        CHECK(strcmp(h, v->compact_hex) == 0, "CompactSeedQR %zu differs from SeedSigner's", i);
        CHECK(round_trip(part, n, 2), "CompactSeedQR %zu did not read back", i);
        qr_decoder_reset(&dec);
        CHECK(qr_decoder_add(&dec, part, n) == QR_STATUS_COMPLETE, "Compact %zu did not decode", i);
        uint16_t words[SEED_MAX_WORDS];
        CHECK(qr_decoder_seed(&dec, words) == s.n_words &&
              memcmp(words, s.words, s.n_words * sizeof words[0]) == 0,
              "CompactSeedQR %zu decoded to other words", i);

        qr_encoder_wipe(&enc);
        bool clean = true;
        for (size_t k = 0; k < 64; k++) {
            clean = clean && buf[k] == 0;
        }
        CHECK(clean, "wipe left the SeedQR in the buffer");
    }
    seed_clear(&s);
    printf("   %zu seeds\n", N(QR_SEEDQR));
}

static bool parts_match(const qrv_density_t *d, unsigned *count)
{
    for (unsigned k = 0; k < d->n_parts; k++) {
        const size_t n = qr_encoder_next_part(&enc, part, PART_SIZE - 1);
        part[n] = '\0';
        if (n != strlen(d->parts[k]) || memcmp(part, d->parts[k], n) != 0) {
            printf("  part %u: %s\n  SeedSigner: %s\n", k, part, d->parts[k]);
            return false;
        }
        (*count)++;
    }
    return true;
}

static void test_xpubs(void)
{
    printf("== xpubs: static, Specter, UR crypto-account\n");
    unsigned parts = 0, read_back = 0;
    seed_t s;
    for (size_t i = 0; i < N(QR_XPUBS); i++) {
        const qrv_xpub_t *x = &QR_XPUBS[i];
        CHECK(load(&s, x->mnemonic, x->passphrase), "seed %zu", i);

        CHECK(qr_encode_xpub(&enc, &s, x->path, x->net, BTC_SIG_SINGLE, buf, BUF_SIZE), "xpub %zu", i);
        size_t n = qr_encoder_next_part(&enc, part, PART_SIZE - 1);
        part[n] = '\0';
        CHECK(strcmp((char *)part, x->xpub_static) == 0, "%s net %d: static %s, SeedSigner %s",
              x->path, x->net, part, x->xpub_static);
        if (i % 4 == 0) {
            CHECK(round_trip(part, n, 2), "static xpub %zu did not read back", i);
            read_back++;
        }

        for (int d = 0; d < 3; d++) {
            CHECK(qr_encode_xpub_specter(&enc, &s, x->path, x->net, BTC_SIG_SINGLE, x->specter[d].density, buf, BUF_SIZE),
                  "specter %zu", i);
            CHECK(qr_encoder_seq_len(&enc) == x->specter[d].n_parts, "%s: Specter part count", x->path);
            CHECK(parts_match(&x->specter[d], &parts), "%s net %d density %d: Specter parts differ",
                  x->path, x->net, d);
            /* A second cycle starts again at part 1. */
            n = qr_encoder_next_part(&enc, part, PART_SIZE - 1);
            CHECK(n == strlen(x->specter[d].parts[0]) && memcmp(part, x->specter[d].parts[0], n) == 0,
                  "Specter parts do not cycle");

            const size_t c = qr_crypto_account(&s, x->path, x->net, buf, BUF_SIZE);
            hex(buf, c, h);
            CHECK(strcmp(h, x->ur[d].cbor_hex) == 0, "%s net %d: crypto-account CBOR differs",
                  x->path, x->net);
            CHECK(qr_encode_xpub_ur(&enc, &s, x->path, x->net, x->ur[d].density, buf, BUF_SIZE),
                  "ur %zu", i);
            CHECK(parts_match(&x->ur[d], &parts), "%s net %d density %d: UR parts differ",
                  x->path, x->net, d);
        }
    }
    seed_clear(&s);
    printf("   %zu xpubs, %u parts identical to SeedSigner's, %u read back\n",
           N(QR_XPUBS), parts, read_back);
}

static void test_psbts(void)
{
    printf("== PSBTs: UR crypto-psbt, out and back in\n");
    unsigned parts = 0, read_back = 0;
    for (size_t i = 0; i < N(QR_PSBT_OUT); i++) {
        const qrv_psbt_out_t *v = &QR_PSBT_OUT[i];
        const size_t len = unhex(v->psbt_hex, raw);
        CHECK(qr_encode_psbt_ur(&enc, raw, len, v->density, buf, BUF_SIZE), "psbt %zu", i);
        const qrv_density_t d = { v->density, NULL, v->parts, v->n_parts };
        CHECK(parts_match(&d, &parts), "PSBT %zu density %d: parts differ", i, v->density);

        /* Every part through a QR code and quirc, then into the decoder,
           starting late and dropping every third frame. */
        qr_encoder_restart(&enc);
        qr_decoder_reset(&dec);
        const uint32_t seq = qr_encoder_seq_len(&enc);
        qr_status_t st = QR_STATUS_FALSE;
        for (uint32_t k = 0; k < seq * 4 + 10 && st != QR_STATUS_COMPLETE; k++) {
            const size_t n = qr_encoder_next_part(&enc, part, PART_SIZE);
            if (k < seq / 3 || k % 3 == 2) {
                continue;
            }
            if (k < 12) {
                CHECK(round_trip(part, n, 2), "PSBT %zu part %u did not read back", i, (unsigned)k);
                read_back++;
            }
            st = qr_decoder_add(&dec, part, n);
        }
        size_t n = 0;
        CHECK(st == QR_STATUS_COMPLETE && qr_decoder_psbt(&dec, got, PSBT_SIZE, &n) &&
              n == len && memcmp(got, raw, len) == 0, "PSBT %zu did not come back whole", i);
    }
    printf("   %zu encodings, %u parts identical to SeedSigner's, %u read back\n",
           N(QR_PSBT_OUT), parts, read_back);
}

/* Module for module against python-qrcode, SeedSigner's encoder: the same
   version chosen, and at the same mask the same symbol. */
static void test_matrices(void)
{
    printf("== QR symbols identical to python-qrcode, versions 1-25\n");
    unsigned same = 0;
    for (size_t i = 0; i < N(QR_MATRICES); i++) {
        const qrv_matrix_t *m = &QR_MATRICES[i];
        QRCode code;
        const int v = qrcode_fit(&code, modules, QRCODE_MAX_VERSION, ECC_LOW,
                                 (const uint8_t *)m->data, (uint16_t)m->len);
        CHECK(v == m->version, "matrix %zu: version %d, python-qrcode %u", i, v, m->version);
        if (qrcode_initBytesMask(&code, modules, m->version, ECC_LOW, (const uint8_t *)m->data,
                                 (uint16_t)m->len, (int8_t)m->mask) != 0) {
            CHECK(0, "matrix %zu: did not encode", i);
            continue;
        }
        bool ok = true;
        for (int y = 0; y < code.size && ok; y++) {
            for (int x = 0; x < code.size && ok; x++) {
                const size_t k = (size_t)y * code.size + (size_t)x;
                unsigned byte;
                sscanf(m->modules_hex + 2 * (k / 8), "%2x", &byte);
                ok = qrcode_getModule(&code, (uint8_t)x, (uint8_t)y) == (((byte << (k % 8)) & 0x80) != 0);
            }
        }
        CHECK(ok, "matrix %zu (version %u, mask %u) differs", i, m->version, m->mask);
        same += ok;
    }
    printf("   %u of %zu symbols identical\n", same, N(QR_MATRICES));
}

static void test_qr_symbols(void)
{
    printf("== QR symbols: byte mode, NUL bytes, the version limit\n");
    const uint8_t zeros[32] = { 0 };
    CHECK(round_trip(zeros, sizeof zeros, 1), "32 zero bytes did not read back");
    const uint8_t mixed[16] = { 0, 0xFF, 0x80, 0x7F, 'a', '0', 0, 0, 1, 2, 3, 4, 5, 6, 7, 8 };
    CHECK(round_trip(mixed, sizeof mixed, 1), "binary did not read back");
    const char *digits = "0115132511540127119007710415074212891906200808700266134314202016179206140896192903001524080106";
    CHECK(round_trip((const uint8_t *)digits, strlen(digits), 1), "numeric did not read back");

    /* Random bytes up to version 13, each read back. Past that quirc itself
       misses some perfect symbols (measured: about 1 in 1000 at v18-v22,
       1 in 50 at v23, 1 in 5 at v24, 1 in 2 at v25), which zbar - SeedSigner's
       reader - reads; there the encoder was checked bit for bit against
       python-qrcode, SeedSigner's encoder, at versions 1-25. */
    QRCode code;
    static uint8_t big[1300];
    srand(7);
    const size_t sizes[] = { 60, 150, 300, 425 };
    for (size_t i = 0; i < N(sizes); i++) {
        for (size_t k = 0; k < sizes[i]; k++) {
            big[k] = (uint8_t)rand();
        }
        CHECK(round_trip(big, sizes[i], 1), "%zu random bytes did not read back", sizes[i]);
    }
    memset(big, 'x', sizeof big);
    CHECK(qrcode_fit(&code, modules, QRCODE_MAX_VERSION, ECC_LOW, big, 1273) == 25,
          "1273 bytes should need version 25");
    CHECK(qrcode_fit(&code, modules, QRCODE_MAX_VERSION, ECC_LOW, big, 1274) < 0,
          "past version 25 was not refused");

    unsigned total = 0;
    for (int v = 1; v <= QRCODE_MAX_VERSION; v++) {
        total += versions_seen[v];
    }
    printf("   %u codes read back by quirc, versions", total);
    for (int v = 1; v <= QRCODE_MAX_VERSION; v++) {
        if (versions_seen[v]) {
            printf(" %d", v);
        }
    }
    printf("\n");
}

int main(void)
{
    btc_init(rand_fill);
    qr = quirc_new();
    if (qr && quirc_resize(qr, CANVAS, CANVAS) < 0) {
        quirc_destroy(qr);
        qr = NULL;
    }
    arena = test_alloc(ARENA_SIZE);
    dec_p = test_alloc(sizeof *dec_p);
    enc_p = test_alloc(sizeof *enc_p);
    buf = test_alloc(BUF_SIZE);
    part = test_alloc(PART_SIZE);
    raw = test_alloc(PSBT_SIZE);
    got = test_alloc(PSBT_SIZE);
    modules = test_alloc(4096);
    qd = test_alloc(sizeof *qd);
    h = test_alloc(4096);
    CHECK(qr && arena && dec_p && enc_p && buf && part && raw && got && modules && h && qd, "out of memory");
    if (!qr || !arena || !dec_p || !enc_p || !buf || !part || !raw || !got || !modules || !h || !qd) {
        if (qr) quirc_destroy(qr);
        test_free_all();
        return CHECK_DONE("test_qr_encode");
    }
    qr_decoder_init(&dec, arena, ARENA_SIZE);
    test_seedqr();
    test_xpubs();
    test_psbts();
    test_matrices();
    test_qr_symbols();
    quirc_destroy(qr);
    test_free_all();
    return CHECK_DONE("test_qr_encode");
}
