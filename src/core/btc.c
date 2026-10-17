/*
 * Bitcoin core. See btc.h.
 */
#include "btc.h"

#include <stdio.h>
#include <string.h>

#include "wally_address.h"
#include "wally_bip32.h"
#include "wally_bip39.h"
#include "wally_bip85.h"
#include "wally_core.h"
#include "wally_crypto.h"
#include "wally_script.h"

/* ---- Bits ------------------------------------------------------------- */

static int get_bit(const uint8_t *buf, size_t bit)
{
    return (buf[bit / 8] >> (7 - bit % 8)) & 1;
}

static void set_bit(uint8_t *buf, size_t bit, int v)
{
    if (v) {
        buf[bit / 8] |= (uint8_t)(0x80 >> (bit % 8));
    } else {
        buf[bit / 8] &= (uint8_t)~(0x80 >> (bit % 8));
    }
}

/* Word indexes to their 11-bit concatenation. */
static void indices_to_bits(const uint16_t *idx, size_t n, uint8_t out[33])
{
    memset(out, 0, 33);
    for (size_t w = 0; w < n; w++) {
        for (int b = 0; b < 11; b++) {
            set_bit(out, w * 11 + (size_t)b, (idx[w] >> (10 - b)) & 1);
        }
    }
}

bool mnemonic_from_entropy(const uint8_t *entropy, size_t len,
                           uint16_t idx[SEED_MAX_WORDS], size_t *n)
{
    if (len != 16 && len != 32) {
        return false;
    }
    uint8_t buf[33], hash[SHA256_LEN];
    memcpy(buf, entropy, len);
    if (wally_sha256(entropy, len, hash, sizeof hash) != WALLY_OK) {
        return false;
    }
    buf[len] = hash[0];     /* the checksum is the hash's first len/4 bits */
    const size_t words = len == 16 ? 12 : 24;
    for (size_t w = 0; w < words; w++) {
        uint16_t v = 0;
        for (int b = 0; b < 11; b++) {
            v = (uint16_t)((v << 1) | get_bit(buf, w * 11 + (size_t)b));
        }
        idx[w] = v;
    }
    *n = words;
    wally_bzero(buf, sizeof buf);
    wally_bzero(hash, sizeof hash);
    return true;
}

/* The entropy part of a 12 or 24 word sequence, checksum ignored. */
static size_t indices_entropy(const uint16_t *idx, size_t n, uint8_t out[32])
{
    uint8_t bits[33];
    indices_to_bits(idx, n, bits);
    const size_t len = n == 12 ? 16 : 32;
    memcpy(out, bits, len);
    wally_bzero(bits, sizeof bits);
    return len;
}

bool mnemonic_checksum_ok(const uint16_t *idx, size_t n)
{
    if (n != 12 && n != 24) {
        return false;
    }
    uint8_t entropy[32];
    uint16_t again[SEED_MAX_WORDS];
    size_t len = indices_entropy(idx, n, entropy), m = 0;
    bool ok = mnemonic_from_entropy(entropy, len, again, &m) && m == n &&
              again[n - 1] == idx[n - 1];
    wally_bzero(entropy, sizeof entropy);
    wally_bzero(again, sizeof again);
    return ok;
}

/* ---- Seeds ------------------------------------------------------------ */

static bool passphrase_ok(const char *p)
{
    size_t len = strlen(p);
    if (len > SEED_PASSPHRASE_MAX) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if ((unsigned char)p[i] < 0x20 || (unsigned char)p[i] > 0x7E) {
            return false;
        }
    }
    return true;
}

size_t seed_mnemonic(const seed_t *s, char *out, size_t cap)
{
    size_t pos = 0;
    for (size_t i = 0; i < s->n_words; i++) {
        const char *w = BIP39_WORDLIST[s->words[i]];
        size_t wl = strlen(w);
        if (pos + wl + 2 > cap) {
            if (cap) {
                wally_bzero(out, cap);
            }
            return 0;
        }
        if (i) {
            out[pos++] = ' ';
        }
        memcpy(out + pos, w, wl);
        pos += wl;
    }
    out[pos] = '\0';
    return pos;
}

/* Recompute the BIP-39 seed and fingerprint from words and passphrase. */
static seed_err_t derive(seed_t *s)
{
    char mnemonic[24 * 9];
    struct ext_key root;
    seed_err_t err = SEED_ERR_INTERNAL;
    if (seed_mnemonic(s, mnemonic, sizeof mnemonic) &&
        bip39_mnemonic_to_seed512(mnemonic, s->passphrase, s->bytes,
                                  sizeof s->bytes) == WALLY_OK &&
        bip32_key_from_seed(s->bytes, sizeof s->bytes, BIP32_VER_MAIN_PRIVATE, 0,
                            &root) == WALLY_OK &&
        bip32_key_get_fingerprint(&root, s->fingerprint,
                                  sizeof s->fingerprint) == WALLY_OK) {
        err = SEED_OK;
    }
    wally_bzero(mnemonic, sizeof mnemonic);
    wally_bzero(&root, sizeof root);
    return err;
}

seed_err_t seed_from_indices(seed_t *s, const uint16_t *idx, size_t n,
                             const char *passphrase)
{
    seed_clear(s);
    if (n != 12 && n != 24) {
        return SEED_ERR_WORD_COUNT;
    }
    for (size_t i = 0; i < n; i++) {
        if (idx[i] >= BIP39_WORDS) {
            return SEED_ERR_UNKNOWN_WORD;
        }
    }
    if (!mnemonic_checksum_ok(idx, n)) {
        return SEED_ERR_CHECKSUM;
    }
    if (passphrase == NULL) {
        passphrase = "";
    }
    if (!passphrase_ok(passphrase)) {
        return SEED_ERR_PASSPHRASE;
    }
    memcpy(s->words, idx, n * sizeof idx[0]);
    s->n_words = (uint8_t)n;
    strcpy(s->passphrase, passphrase);
    seed_err_t err = derive(s);
    if (err != SEED_OK) {
        seed_clear(s);
    }
    return err;
}

/* Split on spaces and commas, as SeedSigner's calculate_checksum does;
 * case-insensitive. Returns the count, or -1 for an unknown word or too
 * many. */
static int parse_words(const char *text, uint16_t *idx, size_t max)
{
    char word[16];
    size_t n = 0, wl = 0;
    for (const char *p = text;; p++) {
        char c = *p;
        if (c == ' ' || c == ',' || c == '\n' || c == '\t' || c == '\0') {
            if (wl) {
                word[wl] = '\0';
                int i = bip39_word_index(word);
                wally_bzero(word, sizeof word);
                if (i < 0 || n >= max) {
                    return -1;
                }
                idx[n++] = (uint16_t)i;
                wl = 0;
            }
            if (c == '\0') {
                break;
            }
        } else {
            if (wl >= sizeof word - 1) {
                wally_bzero(word, sizeof word);
                return -1;
            }
            word[wl++] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        }
    }
    return (int)n;
}

seed_err_t seed_from_mnemonic(seed_t *s, const char *mnemonic,
                              const char *passphrase)
{
    uint16_t idx[SEED_MAX_WORDS + 1];
    int n = parse_words(mnemonic, idx, SEED_MAX_WORDS + 1);
    seed_err_t err = n < 0 ? SEED_ERR_UNKNOWN_WORD
                           : seed_from_indices(s, idx, (size_t)n, passphrase);
    wally_bzero(idx, sizeof idx);
    return err;
}

seed_err_t seed_set_passphrase(seed_t *s, const char *passphrase)
{
    if (passphrase == NULL) {
        passphrase = "";
    }
    if (!passphrase_ok(passphrase)) {
        return SEED_ERR_PASSPHRASE;
    }
    wally_bzero(s->passphrase, sizeof s->passphrase);
    strcpy(s->passphrase, passphrase);
    return derive(s);
}

void seed_clear(seed_t *s)
{
    wally_bzero(s, sizeof *s);
}

void seed_fingerprint_hex(const seed_t *s, char out[9])
{
    snprintf(out, 9, "%02x%02x%02x%02x", s->fingerprint[0], s->fingerprint[1],
             s->fingerprint[2], s->fingerprint[3]);
}

size_t seed_entropy(const seed_t *s, uint8_t out[32])
{
    return indices_entropy(s->words, s->n_words, out);
}

/* ---- Derivation ------------------------------------------------------- */

bool btc_standard_path(btc_net_t net, btc_sig_t sig, btc_script_t script,
                       char *out, size_t cap)
{
    const int coin = net == BTC_NET_MAIN ? 0 : 1;
    int n = -1;
    if (sig == BTC_SIG_SINGLE) {
        static const int purpose[] = { 44, 49, 84, 86 };
        if (script <= BTC_SCRIPT_P2TR) {
            n = snprintf(out, cap, "m/%d'/%d'/0'", purpose[script], coin);
        }
    } else if (sig == BTC_SIG_MULTI) {
        if (script == BTC_SCRIPT_P2PKH) {
            n = snprintf(out, cap, "m/45'");                /* BIP-45 */
        } else if (script == BTC_SCRIPT_P2SH_P2WPKH) {
            n = snprintf(out, cap, "m/48'/%d'/0'/1'", coin);
        } else if (script == BTC_SCRIPT_P2WPKH) {
            n = snprintf(out, cap, "m/48'/%d'/0'/2'", coin);
        }
        /* Taproot multisig: SeedSigner raises "not yet supported". */
    }
    return n > 0 && (size_t)n < cap;
}

/* "m/84'/0'/0'" (or h) to child numbers. Returns the depth, -1 if bad. */
int btc_parse_path(const char *path, uint32_t *out, int max)
{
    const char *p = path;
    if (*p == 'm') {
        p++;
    }
    int depth = 0;
    while (*p) {
        if (*p != '/') {
            return -1;
        }
        p++;
        if (*p == '\0') {
            break;      /* a trailing slash, as embit allows */
        }
        uint64_t v = 0;
        int digits = 0;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (uint64_t)(*p++ - '0');
            if (++digits > 10 || v >= BIP32_INITIAL_HARDENED_CHILD) {
                return -1;
            }
        }
        if (digits == 0 || depth >= max) {
            return -1;
        }
        if (*p == '\'' || *p == 'h' || *p == 'H') {
            v += BIP32_INITIAL_HARDENED_CHILD;
            p++;
        }
        out[depth++] = (uint32_t)v;
    }
    return depth;
}

uint32_t btc_slip132_version(const char *path, btc_net_t net)
{
    /* x, y, z, Y, Z per network: embit's NETWORKS table. */
    static const uint32_t MAIN[] = { 0x0488B21E, 0x049D7CB2, 0x04B24746, 0x0295B43F, 0x02AA7ED3 };
    static const uint32_t TEST[] = { 0x043587CF, 0x044A5262, 0x045F1CF6, 0x024289EF, 0x02575483 };
    const uint32_t *v = net == BTC_NET_MAIN ? MAIN : TEST;
    const uint32_t H = BIP32_INITIAL_HARDENED_CHILD;
    uint32_t p[16];
    int depth = btc_parse_path(path, p, 16);
    if (depth < 0) {
        return 0;
    }
    if (depth >= 1 && p[0] == H + 84) {
        return v[2];
    }
    if (depth >= 1 && p[0] == H + 49) {
        return v[1];
    }
    if (depth >= 4 && p[0] == H + 48) {
        if (p[3] == H + 1) {
            return v[3];
        }
        if (p[3] == H + 2) {
            return v[4];
        }
    }
    return v[0];
}

static bool derive_key(const uint8_t *seed, size_t seed_len, const char *path,
                       btc_net_t net, struct ext_key *out)
{
    struct ext_key root;
    uint32_t p[16];
    int depth = btc_parse_path(path, p, 16);
    bool ok = depth >= 0 &&
              bip32_key_from_seed(seed, seed_len,
                                  net == BTC_NET_MAIN ? BIP32_VER_MAIN_PRIVATE
                                                      : BIP32_VER_TEST_PRIVATE,
                                  0, &root) == WALLY_OK;
    if (ok) {
        if (depth == 0) {
            *out = root;
        } else {
            ok = bip32_key_from_parent_path(&root, p, (size_t)depth,
                                            BIP32_FLAG_KEY_PRIVATE, out) == WALLY_OK;
        }
    }
    wally_bzero(&root, sizeof root);
    return ok;
}

static bool copy_out(char *s, char *out, size_t cap)
{
    bool ok = s && strlen(s) < cap;
    if (ok) {
        strcpy(out, s);
    }
    wally_free_string(s);
    return ok;
}

bool btc_xpub(const uint8_t *seed, size_t seed_len, const char *path,
              btc_net_t net, uint32_t version, char *out, size_t cap)
{
    struct ext_key key;
    uint8_t ser[BIP32_SERIALIZED_LEN];
    char *s = NULL;
    bool ok = derive_key(seed, seed_len, path, net, &key) &&
              bip32_key_serialize(&key, BIP32_FLAG_KEY_PUBLIC, ser, sizeof ser) == WALLY_OK;
    wally_bzero(&key, sizeof key);
    if (ok && version) {
        ser[0] = (uint8_t)(version >> 24);
        ser[1] = (uint8_t)(version >> 16);
        ser[2] = (uint8_t)(version >> 8);
        ser[3] = (uint8_t)version;
    }
    ok = ok && wally_base58_from_bytes(ser, sizeof ser, BASE58_FLAG_CHECKSUM, &s) == WALLY_OK;
    return ok && copy_out(s, out, cap);
}

bool btc_pubkey_at(const seed_t *s, const char *path, btc_net_t net,
                   uint8_t pub[33], uint8_t chain[32], uint8_t parent_fp[4])
{
    struct ext_key key;
    bool ok = derive_key(s->bytes, sizeof s->bytes, path, net, &key);
    if (ok) {
        memcpy(pub, key.pub_key, 33);
        memcpy(chain, key.chain_code, 32);
        memcpy(parent_fp, key.parent160, 4);
    }
    wally_bzero(&key, sizeof key);
    return ok;
}

bool btc_xpub_export(const seed_t *s, const char *path, btc_net_t net,
                     char *out, size_t cap)
{
    char xpub[120], fp[9];
    uint32_t version = btc_slip132_version(path, net);
    if (!version || path[0] != 'm' ||
        !btc_xpub(s->bytes, sizeof s->bytes, path, net, version, xpub, sizeof xpub)) {
        return false;
    }
    seed_fingerprint_hex(s, fp);
    int n = snprintf(out, cap, "[%s%s]%s", fp, path + 1, xpub);
    return n > 0 && (size_t)n < cap;
}

/* ---- Addresses -------------------------------------------------------- */

bool btc_address(const char *xpub, btc_script_t script, uint32_t index,
                 bool change, btc_net_t net, char *out, size_t cap)
{
    static const char *const HRP[] = { "bc", "tb", "bcrt" };
    uint8_t ser[BIP32_SERIALIZED_LEN + 4];
    size_t written = 0;
    struct ext_key parent, child;
    char *s = NULL;

    if (wally_base58_to_bytes(xpub, BASE58_FLAG_CHECKSUM, ser, sizeof ser,
                              &written) != WALLY_OK || written != BIP32_SERIALIZED_LEN) {
        return false;
    }
    /* Any SLIP-132 version (zpub, upub...) to the generic one wally parses. */
    const uint32_t v = net == BTC_NET_MAIN ? BIP32_VER_MAIN_PUBLIC : BIP32_VER_TEST_PUBLIC;
    ser[0] = (uint8_t)(v >> 24);
    ser[1] = (uint8_t)(v >> 16);
    ser[2] = (uint8_t)(v >> 8);
    ser[3] = (uint8_t)v;
    const uint32_t path[2] = { change ? 1u : 0u, index };
    if (index >= BIP32_INITIAL_HARDENED_CHILD ||
        bip32_key_unserialize(ser, BIP32_SERIALIZED_LEN, &parent) != WALLY_OK ||
        bip32_key_from_parent_path(&parent, path, 2, BIP32_FLAG_KEY_PUBLIC, &child) != WALLY_OK) {
        return false;
    }

    const bool main = net == BTC_NET_MAIN;
    int ret = WALLY_EINVAL;
    switch (script) {
    case BTC_SCRIPT_P2PKH:
        ret = wally_bip32_key_to_address(&child, WALLY_ADDRESS_TYPE_P2PKH,
                                         main ? WALLY_ADDRESS_VERSION_P2PKH_MAINNET
                                              : WALLY_ADDRESS_VERSION_P2PKH_TESTNET, &s);
        break;
    case BTC_SCRIPT_P2SH_P2WPKH:
        ret = wally_bip32_key_to_address(&child, WALLY_ADDRESS_TYPE_P2SH_P2WPKH,
                                         main ? WALLY_ADDRESS_VERSION_P2SH_MAINNET
                                              : WALLY_ADDRESS_VERSION_P2SH_TESTNET, &s);
        break;
    case BTC_SCRIPT_P2WPKH:
        ret = wally_bip32_key_to_addr_segwit(&child, HRP[net], 0, &s);
        break;
    case BTC_SCRIPT_P2TR: {
        /* A compressed key is tweaked per BIP-341 with no script tree: the
         * BIP-86 key-path output SeedSigner's embit p2tr() makes. */
        uint8_t spk[WALLY_SCRIPTPUBKEY_P2TR_LEN];
        size_t len = 0;
        ret = wally_scriptpubkey_p2tr_from_bytes(child.pub_key, EC_PUBLIC_KEY_LEN, 0,
                                                 spk, sizeof spk, &len);
        if (ret == WALLY_OK) {
            ret = wally_addr_segwit_from_bytes(spk, len, HRP[net], 0, &s);
        }
        break;
    }
    default:
        break;
    }
    return ret == WALLY_OK && copy_out(s, out, cap);
}

/* ---- SeedQR ----------------------------------------------------------- */

bool seedqr_encode_standard(const seed_t *s, char *out, size_t cap)
{
    if (cap < (size_t)s->n_words * 4 + 1) {
        return false;
    }
    for (size_t i = 0; i < s->n_words; i++) {
        unsigned v = s->words[i];       /* < 2048: always four digits */
        for (int d = 3; d >= 0; d--) {
            out[i * 4 + (size_t)d] = (char)('0' + v % 10);
            v /= 10;
        }
    }
    out[(size_t)s->n_words * 4] = '\0';
    return true;
}

size_t seedqr_encode_compact(const seed_t *s, uint8_t out[32])
{
    return seed_entropy(s, out);
}

size_t seedqr_decode_standard(const char *digits, uint16_t idx[SEED_MAX_WORDS])
{
    size_t len = strlen(digits);
    if (len != 48 && len != 96) {
        return 0;
    }
    for (size_t w = 0; w < len / 4; w++) {
        unsigned v = 0;
        for (int d = 0; d < 4; d++) {
            char c = digits[w * 4 + (size_t)d];
            if (c < '0' || c > '9') {
                return 0;
            }
            v = v * 10 + (unsigned)(c - '0');
        }
        if (v >= BIP39_WORDS) {
            return 0;
        }
        idx[w] = (uint16_t)v;
    }
    return len / 4;
}

size_t seedqr_decode_compact(const uint8_t *bytes, size_t len,
                             uint16_t idx[SEED_MAX_WORDS])
{
    size_t n = 0;
    return mnemonic_from_entropy(bytes, len, idx, &n) ? n : 0;
}

/* ---- New seeds -------------------------------------------------------- */

static bool from_hashed_string(const char *text, size_t short_len,
                               uint16_t idx[SEED_MAX_WORDS], size_t *n)
{
    uint8_t hash[SHA256_LEN];
    const size_t len = strlen(text);
    if (len == 0 ||
        wally_sha256((const uint8_t *)text, len, hash, sizeof hash) != WALLY_OK) {
        return false;
    }
    bool ok = mnemonic_from_entropy(hash, len == short_len ? 16 : 32, idx, n);
    wally_bzero(hash, sizeof hash);
    return ok;
}

bool mnemonic_from_dice(const char *rolls, uint16_t idx[SEED_MAX_WORDS], size_t *n)
{
    for (const char *p = rolls; *p; p++) {
        if (*p < '1' || *p > '6') {
            return false;
        }
    }
    return from_hashed_string(rolls, 50, idx, n);
}

bool mnemonic_from_coin_flips(const char *flips, uint16_t idx[SEED_MAX_WORDS], size_t *n)
{
    for (const char *p = flips; *p; p++) {
        if (*p != '0' && *p != '1') {
            return false;
        }
    }
    return from_hashed_string(flips, 128, idx, n);
}

bool mnemonic_complete_checksum(const uint16_t *idx, size_t n,
                                uint16_t out[SEED_MAX_WORDS], size_t *out_n)
{
    uint16_t words[SEED_MAX_WORDS];
    if (n == 11 || n == 23) {
        memcpy(words, idx, n * sizeof idx[0]);
        words[n++] = 0;     /* "abandon", the temporary final word */
    } else if (n == 12 || n == 24) {
        memcpy(words, idx, n * sizeof idx[0]);
    } else {
        return false;
    }
    uint8_t entropy[32];
    size_t len = indices_entropy(words, n, entropy);
    bool ok = mnemonic_from_entropy(entropy, len, out, out_n);
    wally_bzero(entropy, sizeof entropy);
    wally_bzero(words, sizeof words);
    return ok;
}

int mnemonic_partial_final_word(const char *flips)
{
    size_t len = strlen(flips);
    if (len == 0 || len > 11) {
        return -1;
    }
    int v = 0;
    for (size_t i = 0; i < 11; i++) {
        int bit = 0;
        if (i < len) {
            if (flips[i] != '0' && flips[i] != '1') {
                return -1;
            }
            bit = flips[i] == '1';
        }
        v = (v << 1) | bit;
    }
    return v;
}

/* ---- BIP-85 ----------------------------------------------------------- */

bool bip85_child(const seed_t *s, uint32_t index, size_t words,
                 uint16_t idx[SEED_MAX_WORDS], size_t *n)
{
    struct ext_key root;
    uint8_t entropy[HMAC_SHA512_LEN];
    size_t written = 0;
    bool ok = (words == 12 || words == 24) &&
              bip32_key_from_seed(s->bytes, sizeof s->bytes, BIP32_VER_MAIN_PRIVATE, 0,
                                  &root) == WALLY_OK &&
              bip85_get_bip39_entropy(&root, NULL, (uint32_t)words, index, entropy,
                                      sizeof entropy, &written) == WALLY_OK &&
              mnemonic_from_entropy(entropy, written, idx, n);
    wally_bzero(&root, sizeof root);
    wally_bzero(entropy, sizeof entropy);
    return ok;
}
