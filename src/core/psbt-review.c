/*
 * PSBT review and signing. See psbt-review.h.
 *
 * The structure follows SeedSigner's psbt_parser.py function by function,
 * and the comments name the Python each block corresponds to, so the two
 * can be read side by side. Where this file is deliberately stricter than
 * SeedSigner it says so.
 */
#include "psbt-review.h"

#include <stdlib.h>
#include <string.h>

#include "wally_address.h"
#include "wally_bip32.h"
#include "wally_core.h"
#include "wally_crypto.h"
#include "wally_map.h"
#include "wally_psbt.h"
#include "wally_psbt_members.h"
#include "wally_script.h"
#include "wally_transaction.h"

/* BIP-174 / BIP-371 field types, as stored in libwally's psbt_fields. */
#define PSBT_IN_REDEEM_SCRIPT   0x04
#define PSBT_IN_WITNESS_SCRIPT  0x05
#define PSBT_IN_TAP_KEY_SIG     0x13
#define PSBT_OUT_REDEEM_SCRIPT  0x00
#define PSBT_OUT_WITNESS_SCRIPT 0x01

#define OP_RETURN 0x6a

/* Most claims of this seed's fingerprint one scope may carry. A real scope
 * has at most one per cosigner (16); more is refused, which SeedSigner does
 * not do. */
#define MAX_VERIFIED 20

/* SeedSigner: HIGH_FEES_WARNING_THRESHOLD = 25 (percent). */
#define HIGH_FEE_PERCENT 25

const char *psbt_err_name(psbt_err_t err)
{
    static const char *const names[] = {
        [PSBT_OK] = "OK",
        [PSBT_ERR_PARSE] = "ParseError",
        [PSBT_ERR_OUTPUT_CLAIM] = "PSBTOutputOwnershipClaimError",
        [PSBT_ERR_INPUT_CLAIM] = "PSBTInputOwnershipClaimError",
        [PSBT_ERR_SURPLUS_PATHS] = "PSBTSurplusDerivationPathsError",
        [PSBT_ERR_MIXED_PATH_TYPES] = "PSBTMixedDerivationPathTypesError",
        [PSBT_ERR_OUTPUT_CONTRADICTION] = "PSBTOutputOwnershipContradictionError",
        [PSBT_ERR_SEED_CANNOT_SIGN] = "PSBTSeedCannotSignError",
        [PSBT_ERR_MIXED_INPUTS] = "MixedInputs",
        [PSBT_ERR_UNSUPPORTED] = "UnsupportedPolicy",
        [PSBT_ERR_INVALID] = "InvalidScript",
        [PSBT_ERR_MISSING_UTXO] = "MissingUtxo",
        [PSBT_ERR_NO_OUTPUTS] = "NoOutputs",
        [PSBT_ERR_NO_MEMORY] = "NoMemory",
        [PSBT_ERR_INTERNAL] = "Internal",
    };
    return (unsigned)err < sizeof names / sizeof names[0] ? names[err] : "?";
}

const char *policy_type_name(policy_type_t t)
{
    static const char *const names[] = {
        [POLICY_NONE] = NULL, [POLICY_P2PKH] = "p2pkh", [POLICY_P2SH] = "p2sh",
        [POLICY_P2SH_P2WPKH] = "p2sh-p2wpkh", [POLICY_P2SH_P2WSH] = "p2sh-p2wsh",
        [POLICY_P2WPKH] = "p2wpkh", [POLICY_P2WSH] = "p2wsh", [POLICY_P2TR] = "p2tr",
    };
    return (unsigned)t < sizeof names / sizeof names[0] ? names[t] : NULL;
}

/* ---- Scopes: an input or an output, as the parser sees it ------------- */

typedef struct {
    struct wally_map *keypaths;     /* 33-byte pubkey -> fingerprint + path */
    struct wally_map *tap_paths;    /* 32-byte x-only key -> fingerprint + path */
    struct wally_map *tap_hashes;   /* 32-byte x-only key -> leaf hashes */
    const struct wally_map_item *redeem;
    const struct wally_map_item *witness;
    bool is_input;
} scope_t;

static scope_t input_scope(struct wally_psbt_input *in)
{
    scope_t s = {
        &in->keypaths, &in->taproot_leaf_paths, &in->taproot_leaf_hashes,
        wally_map_get_integer(&in->psbt_fields, PSBT_IN_REDEEM_SCRIPT),
        wally_map_get_integer(&in->psbt_fields, PSBT_IN_WITNESS_SCRIPT),
        true,
    };
    return s;
}

static scope_t output_scope(struct wally_psbt_output *out)
{
    scope_t s = {
        &out->keypaths, &out->taproot_leaf_paths, &out->taproot_leaf_hashes,
        wally_map_get_integer(&out->psbt_fields, PSBT_OUT_REDEEM_SCRIPT),
        wally_map_get_integer(&out->psbt_fields, PSBT_OUT_WITNESS_SCRIPT),
        false,
    };
    return s;
}

typedef struct {
    uint8_t fp[4];
    uint32_t path[PSBT_PATH_MAX];
    size_t len;
} keypath_t;

static bool parse_keypath(const unsigned char *v, size_t len, keypath_t *kp)
{
    if (len < 4 || (len - 4) % 4 != 0 || (len - 4) / 4 > PSBT_PATH_MAX) {
        return false;
    }
    memcpy(kp->fp, v, 4);
    kp->len = (len - 4) / 4;
    for (size_t i = 0; i < kp->len; i++) {
        const unsigned char *p = v + 4 + 4 * i;
        kp->path[i] = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
                      (uint32_t)p[3] << 24;
    }
    return true;
}

/* Leaf hashes recorded for a taproot key; none means it is the internal key. */
static size_t leaf_hash_count(const scope_t *s, const struct wally_map_item *path_item)
{
    const struct wally_map_item *h = wally_map_get(s->tap_hashes, path_item->key,
                                                   path_item->key_len);
    return h ? h->value_len / 32 : 0;
}

/* ---- Keys ------------------------------------------------------------- */

/* _derive_with_cache_via_indices, without the cache: the device parses
 * PSBTs of a size where re-deriving is acceptable. */
static bool derive_pub(const struct ext_key *root, const uint32_t *path, size_t len,
                       uint8_t out[EC_PUBLIC_KEY_LEN])
{
    if (len == 0) {
        memcpy(out, root->pub_key, EC_PUBLIC_KEY_LEN);
        return true;
    }
    struct ext_key child;
    bool ok = bip32_key_from_parent_path(root, path, len,
                                         BIP32_FLAG_KEY_PRIVATE | BIP32_FLAG_SKIP_HASH,
                                         &child) == WALLY_OK;
    if (ok) {
        memcpy(out, child.pub_key, EC_PUBLIC_KEY_LEN);
    }
    wally_bzero(&child, sizeof child);
    return ok;
}

/* seed_owns_pubkey: re-derive and compare real key material. ECDSA keys
 * compare in full, parity included; taproot keys are x-only in the PSBT, so
 * only the x coordinate is information. */
static bool seed_owns(const struct ext_key *root, const keypath_t *kp,
                      const unsigned char *key, size_t key_len)
{
    uint8_t pub[EC_PUBLIC_KEY_LEN];
    if (!derive_pub(root, kp->path, kp->len, pub)) {
        return false;
    }
    if (key_len == EC_XONLY_PUBLIC_KEY_LEN) {
        return memcmp(pub + 1, key, EC_XONLY_PUBLIC_KEY_LEN) == 0;
    }
    return key_len == EC_PUBLIC_KEY_LEN && memcmp(pub, key, EC_PUBLIC_KEY_LEN) == 0;
}

/* ---- Step 1: _fill_missing_fingerprints ------------------------------- */

static void fill_map(const struct ext_key *root, const uint8_t fp[4], struct wally_map *m)
{
    static const uint8_t zero[4] = { 0 };
    for (size_t i = 0; i < m->num_items; i++) {
        struct wally_map_item *it = &m->items[i];
        keypath_t kp;
        if (!parse_keypath(it->value, it->value_len, &kp) || memcmp(kp.fp, zero, 4) != 0) {
            continue;
        }
        if (seed_owns(root, &kp, it->key, it->key_len)) {
            memcpy(it->value, fp, 4);
        }
    }
}

static void fill_missing_fingerprints(struct wally_psbt *p, const struct ext_key *root,
                                      const uint8_t fp[4])
{
    for (size_t i = 0; i < p->num_inputs; i++) {
        fill_map(root, fp, &p->inputs[i].keypaths);
        fill_map(root, fp, &p->inputs[i].taproot_leaf_paths);
    }
    for (size_t i = 0; i < p->num_outputs; i++) {
        fill_map(root, fp, &p->outputs[i].keypaths);
        fill_map(root, fp, &p->outputs[i].taproot_leaf_paths);
    }
}

/* ---- Step 2: _get_seed_derivation_paths -------------------------------- */

typedef struct {
    size_t n;
    keypath_t kp[MAX_VERIFIED];
} verified_t;

static psbt_err_t check_claims(const struct ext_key *root, const uint8_t fp[4],
                               const scope_t *s, struct wally_map *m, verified_t *v)
{
    for (size_t i = 0; i < m->num_items; i++) {
        const struct wally_map_item *it = &m->items[i];
        keypath_t kp;
        if (!parse_keypath(it->value, it->value_len, &kp)) {
            return PSBT_ERR_INVALID;
        }
        if (memcmp(kp.fp, fp, 4) != 0) {
            continue;   /* claims another key: nothing to prove here */
        }
        if (!seed_owns(root, &kp, it->key, it->key_len)) {
            return s->is_input ? PSBT_ERR_INPUT_CLAIM : PSBT_ERR_OUTPUT_CLAIM;
        }
        if (v->n >= MAX_VERIFIED) {
            return PSBT_ERR_INVALID;
        }
        v->kp[v->n++] = kp;
    }
    return PSBT_OK;
}

static psbt_err_t seed_derivation_paths(const struct ext_key *root, const uint8_t fp[4],
                                        const scope_t *s, verified_t *v)
{
    v->n = 0;
    psbt_err_t err = check_claims(root, fp, s, s->keypaths, v);
    if (err == PSBT_OK) {
        err = check_claims(root, fp, s, s->tap_paths, v);
    }
    /* After the loops, so a false claim is still what gets reported. */
    if (err == PSBT_OK && s->keypaths->num_items && s->tap_paths->num_items) {
        err = PSBT_ERR_MIXED_PATH_TYPES;
    }
    return err;
}

/* ---- Policies: _get_policy, _parse_multisig, _get_cosigners ------------ */

/* embit's Script.script_type(). */
static policy_type_t script_type(const unsigned char *d, size_t len)
{
    if (len == 25 && d[0] == 0x76 && d[1] == 0xa9 && d[2] == 0x14 && d[23] == 0x88 && d[24] == 0xac) {
        return POLICY_P2PKH;
    }
    if (len == 23 && d[0] == 0xa9 && d[1] == 0x14 && d[22] == 0x87) {
        return POLICY_P2SH;
    }
    if (len == 22 && d[0] == 0x00 && d[1] == 0x14) {
        return POLICY_P2WPKH;
    }
    if (len == 34 && d[0] == 0x00 && d[1] == 0x20) {
        return POLICY_P2WSH;
    }
    if (len == 34 && d[0] == 0x51 && d[1] == 0x20) {
        return POLICY_P2TR;
    }
    return POLICY_NONE;
}

#define MULTISIG_MAX 16

typedef struct {
    uint8_t m, n;
    uint8_t pubkeys[MULTISIG_MAX][EC_PUBLIC_KEY_LEN];
} multisig_t;

/* OP_m <33:pubkey>... OP_n OP_CHECKMULTISIG */
static bool parse_multisig(const unsigned char *d, size_t len, multisig_t *ms)
{
    if (len < 37 || d[len - 1] != 0xAE) {
        return false;
    }
    int m = d[0] - 0x50, n = d[len - 2] - 0x50;
    if (m < 1 || m > 16 || n < m || n > 16) {
        return false;
    }
    size_t pos = 1;
    for (int i = 0; i < n; i++) {
        if (pos + 34 > len || d[pos] != 0x21 ||
            wally_ec_public_key_verify(d + pos + 1, EC_PUBLIC_KEY_LEN) != WALLY_OK) {
            return false;
        }
        memcpy(ms->pubkeys[i], d + pos + 1, EC_PUBLIC_KEY_LEN);
        pos += 34;
    }
    /* Exactly OP_n OP_CHECKMULTISIG left. */
    if (pos != len - 2) {
        return false;
    }
    ms->m = (uint8_t)m;
    ms->n = (uint8_t)n;
    return true;
}

static bool multisig_contains(const multisig_t *ms, const uint8_t pub[EC_PUBLIC_KEY_LEN])
{
    for (int i = 0; i < ms->n; i++) {
        if (memcmp(ms->pubkeys[i], pub, EC_PUBLIC_KEY_LEN) == 0) {
            return true;
        }
    }
    return false;
}

typedef struct {
    policy_type_t type;
    bool has_mn;
    uint8_t m, n;
    bool has_cosigners;
    uint8_t ncos;
    uint8_t cos[MULTISIG_MAX][BIP32_SERIALIZED_LEN];
} policy_t;

static int cmp_xpub(const void *a, const void *b)
{
    return memcmp(a, b, BIP32_SERIALIZED_LEN);
}

/* Trace every key in the script to a global xpub that derives it. Any gap
 * fails the whole list, as SeedSigner's raise does. */
static bool get_cosigners(const multisig_t *ms, const struct wally_map *derivations,
                          const struct wally_map *xpubs, policy_t *pol)
{
    if (xpubs->num_items == 0 || derivations->num_items == 0) {
        return false;
    }
    pol->ncos = 0;
    for (int k = 0; k < ms->n; k++) {
        const struct wally_map_item *d = wally_map_get(derivations, ms->pubkeys[k],
                                                       EC_PUBLIC_KEY_LEN);
        keypath_t der;
        if (!d || !parse_keypath(d->value, d->value_len, &der)) {
            return false;
        }
        const size_t prefix = der.len >= 2 ? der.len - 2 : 0;
        for (size_t x = 0; x < xpubs->num_items; x++) {
            const struct wally_map_item *xi = &xpubs->items[x];
            keypath_t origin;
            if (xi->key_len != BIP32_SERIALIZED_LEN ||
                !parse_keypath(xi->value, xi->value_len, &origin) || origin.len != prefix ||
                memcmp(origin.path, der.path, prefix * sizeof der.path[0]) != 0) {
                continue;
            }
            uint8_t ser[BIP32_SERIALIZED_LEN];
            memcpy(ser, xi->key, sizeof ser);
            /* Any SLIP-132 version: derivation does not depend on it. */
            const uint32_t v = ser[45] == 0 ? BIP32_VER_MAIN_PRIVATE : BIP32_VER_MAIN_PUBLIC;
            ser[0] = (uint8_t)(v >> 24); ser[1] = (uint8_t)(v >> 16);
            ser[2] = (uint8_t)(v >> 8);  ser[3] = (uint8_t)v;
            struct ext_key parent, child;
            if (bip32_key_unserialize(ser, sizeof ser, &parent) != WALLY_OK) {
                return false;
            }
            int ret = bip32_key_from_parent_path(&parent, der.path + prefix, der.len - prefix,
                                                 BIP32_FLAG_KEY_PUBLIC | BIP32_FLAG_SKIP_HASH,
                                                 &child);
            wally_bzero(&parent, sizeof parent);
            if (ret != WALLY_OK) {
                return false;   /* embit raises on a hardened step from an xpub */
            }
            bool match = memcmp(child.pub_key, ms->pubkeys[k], EC_PUBLIC_KEY_LEN) == 0;
            if (match) {
                memcpy(pol->cos[pol->ncos++], xi->key, BIP32_SERIALIZED_LEN);
                break;
            }
        }
    }
    if (pol->ncos != ms->n) {
        return false;
    }
    qsort(pol->cos, pol->ncos, BIP32_SERIALIZED_LEN, cmp_xpub);
    return true;
}

static psbt_err_t get_policy(const scope_t *s, const unsigned char *spk, size_t spk_len,
                             const struct wally_map *xpubs, policy_t *pol)
{
    memset(pol, 0, sizeof *pol);
    policy_type_t t = script_type(spk, spk_len);
    if (t == POLICY_P2SH) {
        if (s->witness) {
            t = POLICY_P2SH_P2WSH;
        } else if (s->redeem &&
                   script_type(s->redeem->value, s->redeem->value_len) == POLICY_P2WPKH) {
            t = POLICY_P2SH_P2WPKH;
        }
    }
    pol->type = t;

    const struct wally_map_item *script = NULL;
    if ((t == POLICY_P2WSH || t == POLICY_P2SH_P2WSH) && s->witness) {
        script = s->witness;
    } else if (t == POLICY_P2SH && s->redeem) {
        script = s->redeem;
    }
    if (script) {
        multisig_t ms;
        if (!parse_multisig(script->value, script->value_len, &ms)) {
            return PSBT_ERR_INVALID;    /* outside SeedSigner's try: it raises */
        }
        pol->has_mn = true;
        pol->m = ms.m;
        pol->n = ms.n;
        pol->has_cosigners = get_cosigners(&ms, s->keypaths, xpubs, pol);
        if (!pol->has_cosigners) {
            pol->ncos = 0;
        }
    }
    return PSBT_OK;
}

/* Python dict equality of two policies. */
static bool policy_equal(const policy_t *a, const policy_t *b)
{
    if (a->type != b->type || a->has_mn != b->has_mn || a->has_cosigners != b->has_cosigners) {
        return false;
    }
    if (a->has_mn && (a->m != b->m || a->n != b->n)) {
        return false;
    }
    return !a->has_cosigners ||
           (a->ncos == b->ncos && memcmp(a->cos, b->cos, (size_t)a->ncos * BIP32_SERIALIZED_LEN) == 0);
}

/* ---- Transaction access, PSBT v0 and v2 ------------------------------- */

static bool output_at(const struct wally_psbt *p, size_t i, const unsigned char **spk,
                      size_t *len, uint64_t *amount)
{
    if (p->tx) {
        if (i >= p->tx->num_outputs) {
            return false;
        }
        *spk = p->tx->outputs[i].script;
        *len = p->tx->outputs[i].script_len;
        *amount = p->tx->outputs[i].satoshi;
        return true;
    }
    if (i >= p->num_outputs || !p->outputs[i].has_amount) {
        return false;
    }
    *spk = p->outputs[i].script;
    *len = p->outputs[i].script_len;
    *amount = p->outputs[i].amount;
    return true;
}

static bool input_utxo(const struct wally_psbt *p, size_t i, const unsigned char **spk,
                       size_t *len, uint64_t *amount)
{
    const struct wally_psbt_input *in = &p->inputs[i];
    if (in->witness_utxo) {
        *spk = in->witness_utxo->script;
        *len = in->witness_utxo->script_len;
        *amount = in->witness_utxo->satoshi;
        return true;
    }
    if (in->utxo) {
        uint32_t index = p->tx ? p->tx->inputs[i].index : in->index;
        if (index >= in->utxo->num_outputs) {
            return false;
        }
        *spk = in->utxo->outputs[index].script;
        *len = in->utxo->outputs[index].script_len;
        *amount = in->utxo->outputs[index].satoshi;
        return true;
    }
    return false;
}

static bool script_address(const unsigned char *spk, size_t len, btc_net_t net,
                           char out[PSBT_ADDR_MAX])
{
    static const char *const HRP[] = { "bc", "tb", "bcrt" };
    static const uint32_t NETWORK[] = { WALLY_NETWORK_BITCOIN_MAINNET,
                                        WALLY_NETWORK_BITCOIN_TESTNET,
                                        WALLY_NETWORK_BITCOIN_REGTEST };
    char *s = NULL;
    int ret;
    policy_type_t t = script_type(spk, len);
    if (t == POLICY_P2PKH || t == POLICY_P2SH) {
        ret = wally_scriptpubkey_to_address(spk, len, NETWORK[net], &s);
    } else {
        ret = wally_addr_segwit_from_bytes(spk, len, HRP[net], 0, &s);
    }
    bool ok = ret == WALLY_OK && s && strlen(s) < PSBT_ADDR_MAX;
    if (ok) {
        strcpy(out, s);
    }
    wally_free_string(s);
    return ok;
}

/* ---- Script rebuilding ------------------------------------------------ */

typedef struct {
    uint8_t d[64];
    size_t len;
} script_t;

/* _build_singlesig_script */
static bool singlesig_script(policy_type_t t, const uint8_t pub[EC_PUBLIC_KEY_LEN], script_t *s)
{
    uint8_t wpkh[WALLY_SCRIPTPUBKEY_P2WPKH_LEN];
    size_t n = 0;
    switch (t) {
    case POLICY_P2PKH:
        return wally_scriptpubkey_p2pkh_from_bytes(pub, EC_PUBLIC_KEY_LEN, WALLY_SCRIPT_HASH160,
                                                   s->d, sizeof s->d, &s->len) == WALLY_OK;
    case POLICY_P2SH_P2WPKH:
        return wally_witness_program_from_bytes(pub, EC_PUBLIC_KEY_LEN, WALLY_SCRIPT_HASH160,
                                                wpkh, sizeof wpkh, &n) == WALLY_OK &&
               wally_scriptpubkey_p2sh_from_bytes(wpkh, n, WALLY_SCRIPT_HASH160,
                                                  s->d, sizeof s->d, &s->len) == WALLY_OK;
    case POLICY_P2WPKH:
        return wally_witness_program_from_bytes(pub, EC_PUBLIC_KEY_LEN, WALLY_SCRIPT_HASH160,
                                                s->d, sizeof s->d, &s->len) == WALLY_OK;
    case POLICY_P2TR:
        return wally_scriptpubkey_p2tr_from_bytes(pub, EC_PUBLIC_KEY_LEN, 0,
                                                  s->d, sizeof s->d, &s->len) == WALLY_OK;
    default:
        return false;
    }
}

static bool multisig_spk(policy_type_t t, const struct wally_map_item *ms, script_t *s)
{
    uint8_t wsh[WALLY_SCRIPTPUBKEY_P2WSH_LEN];
    size_t n = 0;
    switch (t) {
    case POLICY_P2WSH:
        return wally_witness_program_from_bytes(ms->value, ms->value_len, WALLY_SCRIPT_SHA256,
                                                s->d, sizeof s->d, &s->len) == WALLY_OK;
    case POLICY_P2SH_P2WSH:
        return wally_witness_program_from_bytes(ms->value, ms->value_len, WALLY_SCRIPT_SHA256,
                                                wsh, sizeof wsh, &n) == WALLY_OK &&
               wally_scriptpubkey_p2sh_from_bytes(wsh, n, WALLY_SCRIPT_HASH160,
                                                  s->d, sizeof s->d, &s->len) == WALLY_OK;
    case POLICY_P2SH:
        return wally_scriptpubkey_p2sh_from_bytes(ms->value, ms->value_len, WALLY_SCRIPT_HASH160,
                                                  s->d, sizeof s->d, &s->len) == WALLY_OK;
    default:
        return false;
    }
}

/* ---- The parse -------------------------------------------------------- */

typedef struct {
    struct wally_psbt *p;
    struct ext_key root;
    uint8_t fp[4];
    btc_net_t net;
    policy_t policy;
    verified_t *out_verified;   /* one per output */
    psbt_review_t *r;
} parse_t;

/* _is_change_candidate */
static bool is_change_candidate(const parse_t *ps, const scope_t *out, const policy_t *op)
{
    if (ps->policy.type == POLICY_P2SH_P2WPKH && op->type == POLICY_P2SH && !out->redeem) {
        return true;
    }
    if (op->type != ps->policy.type || op->has_mn != ps->policy.has_mn) {
        return false;
    }
    return !op->has_mn || (op->m == ps->policy.m && op->n == ps->policy.n);
}

static bool keypath_equal(const keypath_t *a, const keypath_t *b)
{
    return memcmp(a->fp, b->fp, 4) == 0 && a->len == b->len &&
           memcmp(a->path, b->path, a->len * sizeof a->path[0]) == 0;
}

/* One output of _parse_outputs: decide change, spend or contradiction. */
static psbt_err_t classify_output(parse_t *ps, size_t i, bool *is_change)
{
    struct wally_psbt_output *out = &ps->p->outputs[i];
    const scope_t sc = output_scope(out);
    const verified_t *verified = &ps->out_verified[i];
    const unsigned char *spk;
    size_t spk_len;
    uint64_t amount;
    policy_t op;
    *is_change = false;

    if (!output_at(ps->p, i, &spk, &spk_len, &amount)) {
        return PSBT_ERR_INVALID;
    }
    psbt_err_t err = get_policy(&sc, spk, spk_len, &ps->p->global_xpubs, &op);
    if (err != PSBT_OK || !is_change_candidate(ps, &sc, &op)) {
        return err;
    }

    keypath_t singlesig;
    bool have_singlesig = false;
    const struct wally_map_item *ms_script = NULL;
    script_t rebuilt = { .len = 0 };

    switch (ps->policy.type) {
    case POLICY_P2WSH:
    case POLICY_P2SH_P2WSH:
        ms_script = sc.witness;
        if (!ms_script || !multisig_spk(ps->policy.type, ms_script, &rebuilt)) {
            return PSBT_ERR_INTERNAL;
        }
        break;
    case POLICY_P2SH:
        ms_script = sc.redeem;
        if (!ms_script || !multisig_spk(POLICY_P2SH, ms_script, &rebuilt)) {
            return PSBT_ERR_INTERNAL;
        }
        break;
    case POLICY_P2PKH:
    case POLICY_P2SH_P2WPKH:
    case POLICY_P2WPKH:
        if (sc.keypaths->num_items > 1) {
            return PSBT_ERR_SURPLUS_PATHS;
        }
        if (sc.keypaths->num_items == 1) {
            uint8_t pub[EC_PUBLIC_KEY_LEN];
            if (!parse_keypath(sc.keypaths->items[0].value, sc.keypaths->items[0].value_len,
                               &singlesig) ||
                !derive_pub(&ps->root, singlesig.path, singlesig.len, pub) ||
                !singlesig_script(ps->policy.type, pub, &rebuilt)) {
                return PSBT_ERR_INVALID;
            }
            have_singlesig = true;
        }
        break;
    case POLICY_P2TR: {
        size_t entries = sc.tap_paths->num_items, internal = 0;
        for (size_t k = 0; k < entries; k++) {
            internal += leaf_hash_count(&sc, &sc.tap_paths->items[k]) == 0;
        }
        if (internal > 1) {
            return PSBT_ERR_SURPLUS_PATHS;
        }
        if (entries == 1 && internal == 1) {
            uint8_t pub[EC_PUBLIC_KEY_LEN];
            if (!parse_keypath(sc.tap_paths->items[0].value, sc.tap_paths->items[0].value_len,
                               &singlesig) ||
                !derive_pub(&ps->root, singlesig.path, singlesig.len, pub) ||
                !singlesig_script(POLICY_P2TR, pub, &rebuilt)) {
                return PSBT_ERR_INVALID;
            }
            have_singlesig = true;
        }
        /* Leaf keys: the script tree is not parsed, so the rebuild cannot
         * match and the output is an external spend. As SeedSigner. */
        break;
    }
    default:
        return PSBT_ERR_UNSUPPORTED;
    }

    const bool matches = rebuilt.len == spk_len && memcmp(rebuilt.d, spk, spk_len) == 0;
    if (matches) {
        if (have_singlesig) {
            if (verified->n == 0) {
                /* Pays this seed but claims another fingerprint there. */
                return PSBT_ERR_OUTPUT_CONTRADICTION;
            }
            if (verified->n != 1 || !keypath_equal(&verified->kp[0], &singlesig)) {
                return PSBT_ERR_INTERNAL;   /* unreachable, as in SeedSigner */
            }
            *is_change = true;
        } else if (ms_script) {
            multisig_t ms;
            if (!parse_multisig(ms_script->value, ms_script->value_len, &ms)) {
                return PSBT_ERR_INVALID;
            }
            if (verified->n == 0) {
                /* Does our seed hide in the script behind another fingerprint? */
                for (size_t k = 0; k < sc.keypaths->num_items; k++) {
                    keypath_t kp;
                    uint8_t pub[EC_PUBLIC_KEY_LEN];
                    if (!parse_keypath(sc.keypaths->items[k].value,
                                       sc.keypaths->items[k].value_len, &kp) ||
                        !derive_pub(&ps->root, kp.path, kp.len, pub)) {
                        return PSBT_ERR_INVALID;
                    }
                    if (multisig_contains(&ms, pub)) {
                        return PSBT_ERR_OUTPUT_CONTRADICTION;
                    }
                }
                /* None of our keys in the script: an external spend. */
            } else {
                for (size_t k = 0; k < verified->n; k++) {
                    uint8_t pub[EC_PUBLIC_KEY_LEN];
                    if (!derive_pub(&ps->root, verified->kp[k].path, verified->kp[k].len, pub)) {
                        return PSBT_ERR_INVALID;
                    }
                    if (!multisig_contains(&ms, pub)) {
                        return PSBT_ERR_OUTPUT_CONTRADICTION;
                    }
                }
                if (sc.keypaths->num_items > ps->policy.n) {
                    return PSBT_ERR_SURPLUS_PATHS;
                }
                *is_change = true;
                /* Real change has the inputs' cosigners. */
                if (ps->policy.has_cosigners &&
                    (!op.has_cosigners || op.ncos != ps->policy.ncos ||
                     memcmp(op.cos, ps->policy.cos,
                            (size_t)op.ncos * BIP32_SERIALIZED_LEN) != 0)) {
                    *is_change = false;
                }
            }
        }
    } else if (verified->n != 0 && ps->policy.type != POLICY_P2TR) {
        /* Claims this seed, pays something else. Taproot is exempt: a
         * script tree tweaks our key, which looks the same from here. */
        return PSBT_ERR_OUTPUT_CONTRADICTION;
    }
    return PSBT_OK;
}

static psbt_err_t parse_outputs(parse_t *ps)
{
    psbt_review_t *r = ps->r;
    const size_t n = ps->p->tx ? ps->p->tx->num_outputs : ps->p->num_outputs;
    r->change = calloc(n ? n : 1, sizeof *r->change);
    r->dest = calloc(n ? n : 1, sizeof *r->dest);
    if (!r->change || !r->dest) {
        return PSBT_ERR_NO_MEMORY;
    }
    uint64_t total_out = 0;
    for (size_t i = 0; i < n; i++) {
        const unsigned char *spk;
        size_t spk_len;
        uint64_t amount;
        bool is_change;
        psbt_err_t err = classify_output(ps, i, &is_change);
        if (err != PSBT_OK) {
            return err;
        }
        if (!output_at(ps->p, i, &spk, &spk_len, &amount) || spk_len == 0) {
            return PSBT_ERR_INVALID;
        }
        total_out += amount;
        if (spk[0] == OP_RETURN) {
            /* SeedSigner reads OP_RETURN OP_PUSHDATA1 <len> <payload>. */
            r->has_op_return = true;
            r->op_return_len = spk_len > 3 ? spk_len - 3 : 0;
            if (r->op_return_len > sizeof r->op_return) {
                r->op_return_len = sizeof r->op_return;
            }
            memcpy(r->op_return, spk + 3, r->op_return_len);
        } else if (is_change) {
            psbt_change_t *c = &r->change[r->num_change];
            const keypath_t *kp = &ps->out_verified[i].kp[0];
            c->output_index = (uint32_t)i;
            c->amount = amount;
            memcpy(c->path, kp->path, kp->len * sizeof kp->path[0]);
            c->path_len = kp->len;
            if (!script_address(spk, spk_len, ps->net, c->address)) {
                return PSBT_ERR_INVALID;
            }
            r->num_change++;
            r->change_amount += amount;
        } else {
            psbt_dest_t *d = &r->dest[r->num_dest];
            d->amount = amount;
            if (!script_address(spk, spk_len, ps->net, d->address)) {
                return PSBT_ERR_INVALID;
            }
            r->num_dest++;
            r->spend_amount += amount;
        }
    }
    r->fee = (int64_t)r->input_amount - (int64_t)total_out;

    r->is_high_fee = psbt_fee_is_high(r->fee, psbt_total_sent(total_out, r->change, r->num_change));
    return PSBT_OK;
}

uint64_t psbt_total_sent(uint64_t total_out, const psbt_change_t *change, size_t n)
{
    uint64_t true_change = 0;
    for (size_t k = 0; k < n; k++) {
        if (psbt_is_change_path(change[k].path, change[k].path_len)) {
            true_change += change[k].amount;
        }
    }
    return total_out - true_change;
}

bool psbt_fee_is_high(int64_t fee, uint64_t sent)
{
    /* fee > 25/100 * sent, in integers: no rounding at the boundary. */
    return sent > 0 && fee > 0 && (uint64_t)fee * 100 > (uint64_t)HIGH_FEE_PERCENT * sent;
}

bool psbt_is_change_path(const uint32_t *path, size_t len)
{
    return len >= 2 && path[len - 2] == 1;
}

static psbt_err_t run(parse_t *ps)
{
    struct wally_psbt *p = ps->p;
    psbt_review_t *r = ps->r;
    const size_t n_out = p->tx ? p->tx->num_outputs : p->num_outputs;

    /* Stricter than SeedSigner, which reviews such a PSBT as a spend of
     * nothing with everything to fees - and, nothing being "sent", without
     * its high-fee warning. Consensus rejects a transaction with no
     * outputs anyway. */
    if (n_out == 0) {
        return PSBT_ERR_NO_OUTPUTS;
    }

    /* 1. */
    fill_missing_fingerprints(p, &ps->root, ps->fp);

    /* 2. Outputs before inputs: a false claim on an output is the attack. */
    ps->out_verified = calloc(n_out ? n_out : 1, sizeof *ps->out_verified);
    if (!ps->out_verified) {
        return PSBT_ERR_NO_MEMORY;
    }
    for (size_t i = 0; i < n_out && i < p->num_outputs; i++) {
        scope_t sc = output_scope(&p->outputs[i]);
        psbt_err_t err = seed_derivation_paths(&ps->root, ps->fp, &sc, &ps->out_verified[i]);
        if (err != PSBT_OK) {
            return err;
        }
    }
    bool can_sign = false;
    for (size_t i = 0; i < p->num_inputs; i++) {
        scope_t sc = input_scope(&p->inputs[i]);
        verified_t v;
        psbt_err_t err = seed_derivation_paths(&ps->root, ps->fp, &sc, &v);
        if (err != PSBT_OK) {
            return err;
        }
        can_sign |= v.n > 0;
    }

    /* 3. */
    if (!can_sign) {
        return PSBT_ERR_SEED_CANNOT_SIGN;
    }

    /* 4. _parse_inputs */
    r->num_inputs = p->num_inputs;
    for (size_t i = 0; i < p->num_inputs; i++) {
        const unsigned char *spk;
        size_t len;
        uint64_t amount;
        if (!input_utxo(p, i, &spk, &len, &amount)) {
            return PSBT_ERR_MISSING_UTXO;
        }
        r->input_amount += amount;
        scope_t sc = input_scope(&p->inputs[i]);
        policy_t pol;
        psbt_err_t err = get_policy(&sc, spk, len, &p->global_xpubs, &pol);
        if (err != PSBT_OK) {
            return err;
        }
        if (i == 0) {
            ps->policy = pol;
        } else if (!policy_equal(&ps->policy, &pol)) {
            return PSBT_ERR_MIXED_INPUTS;
        }
    }
    r->policy = ps->policy.type;
    r->is_multisig = ps->policy.has_mn;
    r->m = ps->policy.m;
    r->n = ps->policy.n;
    r->has_cosigners = ps->policy.has_cosigners;

    /* 5. */
    return parse_outputs(ps);
}

psbt_err_t psbt_review(const uint8_t *bytes, size_t len, const seed_t *seed,
                       btc_net_t net, psbt_review_t *out)
{
    memset(out, 0, sizeof *out);
    parse_t *ps = calloc(1, sizeof *ps);    /* ~2 KB of policy; off the stack */
    if (!ps) {
        return PSBT_ERR_NO_MEMORY;
    }
    ps->r = out;
    ps->net = net;
    psbt_err_t err = PSBT_ERR_PARSE;
    if (wally_psbt_from_bytes(bytes, len, 0, &ps->p) == WALLY_OK && ps->p->num_inputs > 0) {
        err = PSBT_ERR_INTERNAL;
        if (bip32_key_from_seed(seed->bytes, sizeof seed->bytes,
                                net == BTC_NET_MAIN ? BIP32_VER_MAIN_PRIVATE
                                                    : BIP32_VER_TEST_PRIVATE,
                                0, &ps->root) == WALLY_OK &&
            bip32_key_get_fingerprint(&ps->root, ps->fp, sizeof ps->fp) == WALLY_OK) {
            err = run(ps);
        }
    }
    wally_bzero(&ps->root, sizeof ps->root);
    free(ps->out_verified);
    out->psbt = ps->p;
    free(ps);
    if (err != PSBT_OK) {
        psbt_review_free(out);
    }
    return err;
}

psbt_err_t psbt_review_base64(const char *b64, const seed_t *seed, btc_net_t net,
                              psbt_review_t *out)
{
    size_t max = strlen(b64) * 3 / 4 + 3, written = 0;
    uint8_t *buf = malloc(max);
    if (!buf) {
        memset(out, 0, sizeof *out);
        return PSBT_ERR_NO_MEMORY;
    }
    psbt_err_t err;
    if (wally_base64_to_bytes(b64, 0, buf, max, &written) != WALLY_OK || written > max) {
        memset(out, 0, sizeof *out);
        err = PSBT_ERR_PARSE;
    } else {
        err = psbt_review(buf, written, seed, net, out);
    }
    free(buf);
    return err;
}

void psbt_review_free(psbt_review_t *r)
{
    if (r->psbt) {
        wally_psbt_free(r->psbt);
    }
    free(r->change);
    free(r->dest);
    memset(r, 0, sizeof *r);
}

/* ---- Signing: sign_with, then trim ------------------------------------ */

/* PSBTParser.sig_count: a final witness counts once (taproot), otherwise
 * each partial signature. */
static size_t sig_count(const struct wally_psbt *p)
{
    size_t n = 0;
    for (size_t i = 0; i < p->num_inputs; i++) {
        n += p->inputs[i].final_witness ? 1 : p->inputs[i].signatures.num_items;
    }
    return n;
}

psbt_err_t psbt_sign(psbt_review_t *r, const seed_t *seed, btc_net_t net,
                     char **b64_out, size_t *sigs_added)
{
    struct wally_psbt *p = r->psbt;
    struct wally_psbt *trimmed = NULL;
    struct ext_key root;
    *b64_out = NULL;
    *sigs_added = 0;
    if (!p || !p->tx) {
        return PSBT_ERR_UNSUPPORTED;    /* PSBT v2: not signed yet */
    }
    /* embit's sign_with only signs SIGHASH_ALL (DEFAULT for taproot); an
     * input asking for anything else is not signed. Refuse the PSBT
     * instead: a partial signing the user did not see is worse. */
    for (size_t i = 0; i < p->num_inputs; i++) {
        uint32_t sh = p->inputs[i].sighash;
        if (sh != 0 && sh != WALLY_SIGHASH_ALL) {
            return PSBT_ERR_UNSUPPORTED;
        }
    }
    const size_t before = sig_count(p);
    if (bip32_key_from_seed(seed->bytes, sizeof seed->bytes,
                            net == BTC_NET_MAIN ? BIP32_VER_MAIN_PRIVATE : BIP32_VER_TEST_PRIVATE,
                            0, &root) != WALLY_OK) {
        return PSBT_ERR_INTERNAL;
    }
    /* EC_FLAG_GRIND_R: embit grinds ECDSA signatures to 70 bytes or less. */
    int ret = wally_psbt_sign_bip32(p, &root, EC_FLAG_GRIND_R);
    wally_bzero(&root, sizeof root);
    if (ret != WALLY_OK) {
        return PSBT_ERR_INTERNAL;
    }

    /* trim: the unsigned transaction, plus each input's partial signatures,
     * or for taproot the key-path signature as the final witness, which is
     * where embit's sign_input_with_tapkey puts it. */
    psbt_err_t err = PSBT_ERR_NO_MEMORY;
    if (wally_psbt_init_alloc(WALLY_PSBT_VERSION_0, p->num_inputs, p->tx->num_outputs, 0, 0,
                              &trimmed) != WALLY_OK ||
        wally_psbt_set_global_tx(trimmed, p->tx) != WALLY_OK) {
        goto done;
    }
    for (size_t i = 0; i < p->num_inputs; i++) {
        const struct wally_psbt_input *in = &p->inputs[i];
        const struct wally_map_item *tap = wally_map_get_integer(&in->psbt_fields, PSBT_IN_TAP_KEY_SIG);
        if (tap) {
            struct wally_tx_witness_stack *wit = NULL;
            ret = wally_tx_witness_stack_init_alloc(1, &wit);
            if (ret == WALLY_OK) {
                ret = wally_tx_witness_stack_add(wit, tap->value, tap->value_len);
            }
            if (ret == WALLY_OK) {
                ret = wally_psbt_set_input_final_witness(trimmed, i, wit);
            }
            wally_tx_witness_stack_free(wit);
        } else if (in->final_witness) {
            ret = wally_psbt_set_input_final_witness(trimmed, i, in->final_witness);
        } else {
            ret = wally_psbt_set_input_signatures(trimmed, i, &in->signatures);
        }
        if (ret != WALLY_OK) {
            goto done;
        }
    }
    const size_t after = sig_count(trimmed);
    *sigs_added = after > before ? after - before : 0;
    err = wally_psbt_to_base64(trimmed, 0, b64_out) == WALLY_OK ? PSBT_OK : PSBT_ERR_INTERNAL;
done:
    if (trimmed) {
        wally_psbt_free(trimmed);
    }
    return err;
}
