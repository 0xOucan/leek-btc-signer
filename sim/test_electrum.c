/*
 * Electrum seeds against SeedSigner's own ElectrumSeed
 * (seedsigner_electrum.h, by gen_electrum_vectors.py): which mnemonics it
 * takes, the seed and fingerprint, the custom extension as it normalises
 * it, the xpubs it exports at m/0h and m/1h (static, Specter and the UR
 * crypto-account), the addresses below m/0h that the explorer lists and
 * address verification searches, and messages signed with its seed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "btc-init.h"
#include "qr-encode.h"
#include "vectors/seedsigner_electrum.h"
#include "wally_core.h"

#define N(a) (sizeof(a) / sizeof((a)[0]))

static void rand_fill(void *p, unsigned n) { memset(p, 0x5A, n); }

/* Allocated once for the suite: the self-test image has no PSRAM. */
#define BUF_SIZE 2048
static qr_encoder_t *enc;
static uint8_t *buf;
static char *part;

/* The words' indexes; 0 if one is not on the list. */
static size_t indexes(const char *mnemonic, uint16_t idx[SEED_MAX_WORDS])
{
    char word[16];
    size_t n = 0, wl = 0;
    for (const char *p = mnemonic;; p++) {
        if (*p == ' ' || *p == '\0') {
            word[wl] = '\0';
            const int i = bip39_word_index(word);
            if (i < 0 || n >= SEED_MAX_WORDS) {
                return 0;
            }
            idx[n++] = (uint16_t)i;
            wl = 0;
            if (*p == '\0') {
                return n;
            }
        } else if (wl < sizeof word - 1) {
            word[wl++] = *p;
        }
    }
}

static bool load(seed_t *s, const char *mnemonic, const char *ext)
{
    uint16_t idx[SEED_MAX_WORDS];
    const size_t n = indexes(mnemonic, idx);
    return n && seed_from_indices_electrum(s, idx, n, ext) == SEED_OK;
}

static void hex(const uint8_t *b, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++) {
        sprintf(out + 2 * i, "%02x", b[i]);
    }
    out[2 * n] = '\0';
}

static void test_seeds(void)
{
    printf("== ElectrumSeed: the version check, seed, fingerprint, extension\n");
    unsigned ok = 0, refused = 0;
    for (size_t i = 0; i < N(VEC_ELECTRUM_SEEDS); i++) {
        const vec_electrum_seed_t *v = &VEC_ELECTRUM_SEEDS[i];
        uint16_t idx[SEED_MAX_WORDS];
        const size_t n = indexes(v->mnemonic, idx);
        CHECK(n == 12, "%zu: words", i);
        seed_t s;
        const seed_err_t err = seed_from_indices_electrum(&s, idx, n, v->extension);
        if (v->error) {
            CHECK(err == SEED_ERR_ELECTRUM, "%zu: \"%s\" taken; SeedSigner: %s", i, v->mnemonic, v->error);
            refused++;
            continue;
        }
        CHECK(err == SEED_OK, "%zu: \"%s\" refused (%d)", i, v->mnemonic, err);
        if (err != SEED_OK) {
            continue;
        }
        char h[129], fp[9];
        hex(s.bytes, sizeof s.bytes, h);
        seed_fingerprint_hex(&s, fp);
        CHECK(strcmp(h, v->seed_hex) == 0, "%zu: seed differs", i);
        CHECK(strcmp(fp, v->fingerprint) == 0, "%zu: fingerprint %s, SeedSigner %s", i, fp, v->fingerprint);
        CHECK(strcmp(s.passphrase, v->normalized) == 0, "%zu: extension \"%s\", SeedSigner \"%s\"", i,
              s.passphrase, v->normalized);
        CHECK(s.electrum, "%zu: not marked Electrum", i);

        /* The same extension set afterwards, as SeedAddPassphraseView does,
           and taken off again. */
        seed_t t;
        CHECK(seed_from_indices_electrum(&t, idx, n, "") == SEED_OK, "%zu: bare", i);
        CHECK(seed_set_passphrase(&t, v->extension) == SEED_OK, "%zu: set", i);
        CHECK(memcmp(t.bytes, s.bytes, sizeof s.bytes) == 0 && strcmp(t.passphrase, v->normalized) == 0,
              "%zu: set_passphrase differs from the constructor", i);
        seed_clear(&t);
        seed_clear(&s);
        ok++;
    }
    for (size_t i = 0; i < N(VEC_ELECTRUM_LENGTHS); i++) {
        const vec_electrum_length_t *v = &VEC_ELECTRUM_LENGTHS[i];
        uint16_t idx[32];
        const int regular = bip39_word_index("regular");
        for (unsigned k = 0; k < v->words && k < 32; k++) {
            idx[k] = (uint16_t)regular;
        }
        seed_t s;
        CHECK(seed_from_indices_electrum(&s, idx, v->words, "") == SEED_ERR_WORD_COUNT,
              "%u words taken; SeedSigner: %s", v->words, v->error);
    }
    printf("   %u seeds and %u refusals as SeedSigner's, %zu lengths refused\n", ok, refused,
           N(VEC_ELECTRUM_LENGTHS));
}

static void test_normalize(void)
{
    printf("== normalize_electrum_passphrase\n");
    static const struct { const char *in, *out; } cases[] = {
        { "", "" }, { " ", "" }, { "A", "a" }, { "  a  b  ", "a b" }, { "Hello World", "hello world" },
        { "x   y z", "x y z" }, { "~!@ #$%", "~!@ #$%" }, { "ZZ", "zz" },
    };
    for (size_t i = 0; i < N(cases); i++) {
        char out[SEED_PASSPHRASE_MAX + 1];
        electrum_normalize_passphrase(cases[i].in, out, sizeof out);
        CHECK(strcmp(out, cases[i].out) == 0, "\"%s\": \"%s\", want \"%s\"", cases[i].in, out, cases[i].out);
    }
}

static void test_xpubs(void)
{
    printf("== xpub exports: derivation_override and detect_version\n");
    unsigned n_parts = 0;
    for (size_t i = 0; i < N(VEC_ELECTRUM_XPUBS); i++) {
        const vec_electrum_xpub_t *x = &VEC_ELECTRUM_XPUBS[i];
        seed_t s;
        CHECK(load(&s, x->mnemonic, x->extension), "%zu: seed", i);
        CHECK(strcmp(seed_derivation_override(&s, x->sig), x->path) == 0, "%zu: path %s", i, x->path);

        CHECK(qr_encode_xpub(enc, &s, x->path, x->net, x->sig, buf, BUF_SIZE), "%zu: static", i);
        size_t n = qr_encoder_next_part(enc, (uint8_t *)part, BUF_SIZE - 1);
        part[n] = '\0';
        CHECK(strcmp(part, x->xpub_static) == 0, "%zu: static %s, SeedSigner %s", i, part, x->xpub_static);

        CHECK(qr_encode_xpub_specter(enc, &s, x->path, x->net, x->sig, QR_DENSITY_MEDIUM, buf, BUF_SIZE),
              "%zu: specter", i);
        CHECK(qr_encoder_seq_len(enc) == x->n_specter, "%zu: Specter part count", i);
        for (unsigned k = 0; k < x->n_specter; k++) {
            n = qr_encoder_next_part(enc, (uint8_t *)part, BUF_SIZE - 1);
            part[n] = '\0';
            CHECK(strcmp(part, x->specter[k]) == 0, "%zu: Specter part %u differs", i, k);
            n_parts++;
        }

        const size_t c = qr_crypto_account(&s, x->path, x->net, buf, BUF_SIZE);
        hex(buf, c, part);
        CHECK(strcmp(part, x->cbor_hex) == 0, "%zu %s: crypto-account CBOR differs", i, x->path);
        seed_clear(&s);
    }
    printf("   %zu exports and %u Specter parts as SeedSigner's\n", N(VEC_ELECTRUM_XPUBS), n_parts);
}

static void test_addresses(void)
{
    printf("== addresses below m/0h, every script type\n");
    char xpub[120] = "", addr[100];
    const char *last_m = NULL, *last_e = NULL;
    int last_net = -1;
    seed_t s;
    for (size_t i = 0; i < N(VEC_ELECTRUM_ADDRESSES); i++) {
        const vec_electrum_address_t *a = &VEC_ELECTRUM_ADDRESSES[i];
        if (a->mnemonic != last_m || a->extension != last_e || (int)a->net != last_net) {
            CHECK(load(&s, a->mnemonic, a->extension), "%zu: seed", i);
            /* seed.get_xpub(wallet_path=m/0h): the generic version. */
            CHECK(btc_xpub(s.bytes, sizeof s.bytes, seed_derivation_override(&s, BTC_SIG_SINGLE), a->net, 0,
                           xpub, sizeof xpub), "%zu: xpub", i);
            seed_clear(&s);
            last_m = a->mnemonic;
            last_e = a->extension;
            last_net = (int)a->net;
        }
        CHECK(btc_address(xpub, a->script, a->index, a->change, a->net, addr, sizeof addr) &&
                  strcmp(addr, a->address) == 0,
              "%zu: %s, SeedSigner %s", i, addr, a->address);
    }
    printf("   %zu addresses as SeedSigner's\n", N(VEC_ELECTRUM_ADDRESSES));
}

static void test_signs(void)
{
    printf("== sign_message with an Electrum seed's bytes\n");
    char sig[100];
    for (size_t i = 0; i < N(VEC_ELECTRUM_SIGNS); i++) {
        const vec_electrum_sign_t *v = &VEC_ELECTRUM_SIGNS[i];
        seed_t s;
        CHECK(load(&s, v->mnemonic, ""), "%zu: seed", i);
        CHECK(btc_sign_message(&s, v->path, (const uint8_t *)v->message, strlen(v->message), sig, sizeof sig) &&
                  strcmp(sig, v->signature) == 0,
              "%zu: %s, SeedSigner %s", i, sig, v->signature);
        seed_clear(&s);
    }
    printf("   %zu signatures as SeedSigner's\n", N(VEC_ELECTRUM_SIGNS));
}

int main(void)
{
    btc_init(rand_fill);
    enc = malloc(sizeof *enc);
    buf = malloc(BUF_SIZE);
    part = malloc(BUF_SIZE);
    CHECK(enc && buf && part, "memory");
    if (enc && buf && part) {
        test_seeds();
        test_normalize();
        test_xpubs();
        test_addresses();
        test_signs();
    }
    if (enc) {
        qr_encoder_wipe(enc);
    }
    free(enc);
    free(buf);
    free(part);
    return CHECK_DONE("test_electrum");
}
