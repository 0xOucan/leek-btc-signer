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

static uint8_t buf[1 << 18];
static uint8_t part[1 << 14];
static uint8_t arena[1 << 20];
static qr_decoder_t dec;
static qr_encoder_t enc;
static struct quirc *qr;

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
    static uint8_t modules[4096];
    QRCode code;
    const int v = qrcode_fit(&code, modules, QRCODE_MAX_VERSION, ECC_LOW, data, (uint16_t)len);
    if (v < 0) {
        return false;
    }
    versions_seen[v]++;
    const int side = (code.size + 8) * scale;
    if (quirc_resize(qr, side, side) < 0) {
        return false;
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
    for (int i = 0; i < quirc_count(qr); i++) {
        struct quirc_code qc;
        static struct quirc_data qd;
        quirc_extract(qr, i, &qc);
        if (quirc_decode(&qc, &qd) == QUIRC_SUCCESS && (size_t)qd.payload_len == len &&
            memcmp(qd.payload, data, len) == 0) {
            return true;
        }
    }
    return false;
}

static void test_seedqr(void)
{
    printf("== SeedQR and CompactSeedQR\n");
    static char h[200];
    seed_t s;
    for (size_t i = 0; i < N(QR_SEEDQR); i++) {
        const qrv_seedqr_t *v = &QR_SEEDQR[i];
        CHECK(load(&s, v->mnemonic, ""), "seed %zu", i);

        CHECK(qr_encode_seedqr(&enc, &s, buf, sizeof buf), "SeedQR %zu", i);
        size_t n = qr_encoder_next_part(&enc, part, sizeof part);
        CHECK(n == strlen(v->standard) && memcmp(part, v->standard, n) == 0,
              "SeedQR %zu differs from SeedSigner's", i);
        CHECK(qr_encoder_seq_len(&enc) == 1, "SeedQR is static");
        CHECK(round_trip(part, n, 2), "SeedQR %zu did not read back", i);
        qr_decoder_reset(&dec);
        CHECK(qr_decoder_add(&dec, part, n) == QR_STATUS_COMPLETE, "SeedQR %zu did not decode", i);

        CHECK(qr_encode_compact_seedqr(&enc, &s, buf, sizeof buf), "Compact %zu", i);
        n = qr_encoder_next_part(&enc, part, sizeof part);
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
        const size_t n = qr_encoder_next_part(&enc, part, sizeof part - 1);
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
    static char h[4096];
    unsigned parts = 0, read_back = 0;
    seed_t s;
    for (size_t i = 0; i < N(QR_XPUBS); i++) {
        const qrv_xpub_t *x = &QR_XPUBS[i];
        CHECK(load(&s, x->mnemonic, x->passphrase), "seed %zu", i);

        CHECK(qr_encode_xpub(&enc, &s, x->path, x->net, buf, sizeof buf), "xpub %zu", i);
        size_t n = qr_encoder_next_part(&enc, part, sizeof part - 1);
        part[n] = '\0';
        CHECK(strcmp((char *)part, x->xpub_static) == 0, "%s net %d: static %s, SeedSigner %s",
              x->path, x->net, part, x->xpub_static);
        if (i % 4 == 0) {
            CHECK(round_trip(part, n, 2), "static xpub %zu did not read back", i);
            read_back++;
        }

        for (int d = 0; d < 3; d++) {
            CHECK(qr_encode_xpub_specter(&enc, &s, x->path, x->net, x->specter[d].density, buf, sizeof buf),
                  "specter %zu", i);
            CHECK(qr_encoder_seq_len(&enc) == x->specter[d].n_parts, "%s: Specter part count", x->path);
            CHECK(parts_match(&x->specter[d], &parts), "%s net %d density %d: Specter parts differ",
                  x->path, x->net, d);
            /* A second cycle starts again at part 1. */
            n = qr_encoder_next_part(&enc, part, sizeof part - 1);
            CHECK(n == strlen(x->specter[d].parts[0]) && memcmp(part, x->specter[d].parts[0], n) == 0,
                  "Specter parts do not cycle");

            const size_t c = qr_crypto_account(&s, x->path, x->net, buf, sizeof buf);
            hex(buf, c, h);
            CHECK(strcmp(h, x->ur[d].cbor_hex) == 0, "%s net %d: crypto-account CBOR differs",
                  x->path, x->net);
            CHECK(qr_encode_xpub_ur(&enc, &s, x->path, x->net, x->ur[d].density, buf, sizeof buf),
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
    static uint8_t raw[1 << 16], got[1 << 16];
    unsigned parts = 0, read_back = 0;
    for (size_t i = 0; i < N(QR_PSBT_OUT); i++) {
        const qrv_psbt_out_t *v = &QR_PSBT_OUT[i];
        const size_t len = unhex(v->psbt_hex, raw);
        CHECK(qr_encode_psbt_ur(&enc, raw, len, v->density, buf, sizeof buf), "psbt %zu", i);
        const qrv_density_t d = { v->density, NULL, v->parts, v->n_parts };
        CHECK(parts_match(&d, &parts), "PSBT %zu density %d: parts differ", i, v->density);

        /* Every part through a QR code and quirc, then into the decoder,
           starting late and dropping every third frame. */
        qr_encoder_restart(&enc);
        qr_decoder_reset(&dec);
        const uint32_t seq = qr_encoder_seq_len(&enc);
        qr_status_t st = QR_STATUS_FALSE;
        for (uint32_t k = 0; k < seq * 4 + 10 && st != QR_STATUS_COMPLETE; k++) {
            const size_t n = qr_encoder_next_part(&enc, part, sizeof part);
            if (k < seq / 3 || k % 3 == 2) {
                continue;
            }
            if (k < 12) {
                CHECK(round_trip(part, n, 2), "PSBT %zu part %u did not read back", i, k);
                read_back++;
            }
            st = qr_decoder_add(&dec, part, n);
        }
        size_t n = 0;
        CHECK(st == QR_STATUS_COMPLETE && qr_decoder_psbt(&dec, got, sizeof got, &n) &&
              n == len && memcmp(got, raw, len) == 0, "PSBT %zu did not come back whole", i);
    }
    printf("   %zu encodings, %u parts identical to SeedSigner's, %u read back\n",
           N(QR_PSBT_OUT), parts, read_back);
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
    static uint8_t modules[4096];
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
    qr_decoder_init(&dec, arena, sizeof arena);
    test_seedqr();
    test_xpubs();
    test_psbts();
    test_qr_symbols();
    quirc_destroy(qr);
    return CHECK_DONE("test_qr_encode");
}
