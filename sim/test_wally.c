/*
 * T12 smoke test: libwally and secp256k1 built with our settings give the
 * published answers. Vectors from SeedSigner's tests/test_embit_utils.py
 * (MIT, Copyright (c) 2021 SeedSigner), which takes them from BIP-32 and
 * BIP-84. The full ports come with T14-T16.
 */
#include <string.h>

#include "btc-init.h"
#include "check.h"
#include "wally_bip32.h"
#include "wally_bip39.h"
#include "wally_core.h"

static void fake_rand(void *buf, unsigned len) { memset(buf, 0x42, len); }

static void xpub_at(const uint8_t *seed, size_t seed_len, const char *path,
                    uint32_t version, const char *want)
{
    struct ext_key *root = NULL, *child = NULL;
    char *got = NULL;
    uint8_t ser[BIP32_SERIALIZED_LEN];
    CHECK(bip32_key_from_seed_alloc(seed, seed_len, BIP32_VER_MAIN_PRIVATE, 0, &root) == WALLY_OK,
          "master key");
    /* wally's path parser takes "m/..." but not a bare "m": the master
     * key is the root itself. */
    const struct ext_key *key = root;
    if (strcmp(path, "m") != 0) {
        CHECK(bip32_key_from_parent_path_str_alloc(root, path, 0,
                  BIP32_FLAG_KEY_PRIVATE | BIP32_FLAG_STR_WILDCARD, &child) == WALLY_OK,
              "derive %s", path);
        key = child;
    }
    if (key == NULL || bip32_key_serialize(key, BIP32_FLAG_KEY_PUBLIC, ser, sizeof ser) != WALLY_OK) {
        CHECK(0, "serialize %s", path);
        bip32_key_free(root);
        return;
    }
    /* Swap in the requested version bytes (xpub, zpub...). */
    ser[0] = (uint8_t)(version >> 24); ser[1] = (uint8_t)(version >> 16);
    ser[2] = (uint8_t)(version >> 8);  ser[3] = (uint8_t)version;
    CHECK(wally_base58_from_bytes(ser, sizeof ser, BASE58_FLAG_CHECKSUM, &got) == WALLY_OK,
          "base58");
    CHECK(got && strcmp(got, want) == 0, "%s\n    got  %s\n    want %s", path, got, want);
    wally_free_string(got);
    bip32_key_free(child);
    bip32_key_free(root);
}

int main(void)
{
    CHECK(btc_init(fake_rand), "btc_init");

    /* BIP-32 test vector 1, seed 000102...0f. */
    uint8_t v1[16];
    for (int i = 0; i < 16; i++) v1[i] = (uint8_t)i;
    xpub_at(v1, sizeof v1, "m", 0x0488B21E,
            "xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8");
    xpub_at(v1, sizeof v1, "m/0'/1/2'/2/1000000000", 0x0488B21E,
            "xpub6H1LXWLaKsWFhvm6RVpEL9P4KfRZSW7abD2ttkWP3SSQvnyA8FSVqNTEcYFgJS2UaFcxupHiYkro49S8yGasTvXEYBVPamhGW6cFJodrTHy");

    /* BIP-84: "abandon x11 about", m/84'/0'/0' as a zpub. */
    const char *mnemonic = "abandon abandon abandon abandon abandon abandon "
                           "abandon abandon abandon abandon abandon about";
    CHECK(bip39_mnemonic_validate(NULL, mnemonic) == WALLY_OK, "checksum");
    uint8_t seed[BIP39_SEED_LEN_512];
    CHECK(bip39_mnemonic_to_seed512(mnemonic, NULL, seed, sizeof seed) == WALLY_OK, "seed");
    xpub_at(seed, sizeof seed, "m/84'/0'/0'", 0x04B24746,
            "zpub6rFR7y4Q2AijBEqTUquhVz398htDFrtymD9xYYfG1m4wAcvPhXNfE3EfH1r1ADqtfSdVCToUG868RvUUkgDKf31mGDtKsAYz2oz2AGutZYs");

    /* Master fingerprint 73c5da0a, as SeedSigner shows for this seed. */
    struct ext_key *root = NULL;
    uint8_t fp[BIP32_KEY_FINGERPRINT_LEN];
    bip32_key_from_seed_alloc(seed, sizeof seed, BIP32_VER_MAIN_PRIVATE, 0, &root);
    CHECK(bip32_key_get_fingerprint(root, fp, sizeof fp) == WALLY_OK, "fingerprint");
    CHECK(fp[0] == 0x73 && fp[1] == 0xc5 && fp[2] == 0xda && fp[3] == 0x0a,
          "fingerprint %02x%02x%02x%02x", fp[0], fp[1], fp[2], fp[3]);
    bip32_key_free(root);

    /* A wrong checksum is refused. */
    CHECK(bip39_mnemonic_validate(NULL, "abandon abandon abandon abandon abandon abandon "
                                        "abandon abandon abandon abandon abandon abandon") != WALLY_OK,
          "bad checksum accepted");

    wally_bzero(seed, sizeof seed);
    wally_cleanup(0);
    return CHECK_DONE("test_wally");
}
