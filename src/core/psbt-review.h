/*
 * PSBT review and signing (T19, T20): a C port of SeedSigner's PSBTParser
 * (src/seedsigner/models/psbt_parser.py) and of its signing step
 * (PSBTFinalizeView: sign_with, then trim).
 *
 * A PSBT is written by an untrusted coordinator. Its fingerprints,
 * derivation paths and xpubs are claims; this module re-derives every key
 * that claims to be ours from the seed and refuses the PSBT on any false
 * claim or contradiction, exactly where SeedSigner does. The checks run in
 * SeedSigner's order, so the same PSBT fails with the same error:
 *
 *   1. fill all-zero fingerprints, only where the seed provably owns the key
 *   2. verify every claim of this seed's fingerprint, outputs then inputs
 *   3. refuse if the seed can sign no input
 *   4. every input must resolve to the same policy
 *   5. sort outputs into change (re-derived and matched against the
 *      committed script), external spends, and OP_RETURN
 *
 * Checked against every PSBT SeedSigner's own test suite parses
 * (sim/vectors/seedsigner_psbt.h, sim/test_psbt.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "btc.h"

struct wally_psbt;

/* SeedSigner's exception classes, one for one. */
typedef enum {
    PSBT_OK = 0,
    PSBT_ERR_PARSE,                 /* not a PSBT libwally can read */
    PSBT_ERR_OUTPUT_CLAIM,          /* PSBTOutputOwnershipClaimError */
    PSBT_ERR_INPUT_CLAIM,           /* PSBTInputOwnershipClaimError */
    PSBT_ERR_SURPLUS_PATHS,         /* PSBTSurplusDerivationPathsError */
    PSBT_ERR_MIXED_PATH_TYPES,      /* PSBTMixedDerivationPathTypesError */
    PSBT_ERR_OUTPUT_CONTRADICTION,  /* PSBTOutputOwnershipContradictionError */
    PSBT_ERR_SEED_CANNOT_SIGN,      /* PSBTSeedCannotSignError */
    PSBT_ERR_MIXED_INPUTS,          /* RuntimeError("Mixed inputs ...") */
    PSBT_ERR_UNSUPPORTED,           /* RuntimeError("Unsupported policy type") */
    PSBT_ERR_INVALID,               /* ValueError: a malformed multisig script */
    PSBT_ERR_MISSING_UTXO,          /* an input with neither UTXO (stricter than SeedSigner) */
    PSBT_ERR_NO_OUTPUTS,            /* no outputs: all to fees, consensus-invalid (stricter) */
    PSBT_ERR_NO_MEMORY,
    PSBT_ERR_INTERNAL,
} psbt_err_t;

const char *psbt_err_name(psbt_err_t err);

typedef enum {
    POLICY_NONE = 0,    /* an unrecognised script */
    POLICY_P2PKH, POLICY_P2SH, POLICY_P2SH_P2WPKH, POLICY_P2SH_P2WSH,
    POLICY_P2WPKH, POLICY_P2WSH, POLICY_P2TR,
} policy_type_t;

const char *policy_type_name(policy_type_t t);  /* "p2wpkh"..., NULL for none */

#define PSBT_PATH_MAX 16
#define PSBT_ADDR_MAX 100

typedef struct {
    uint32_t output_index;
    char address[PSBT_ADDR_MAX];
    uint64_t amount;
    uint32_t path[PSBT_PATH_MAX];   /* the path this seed provably owns */
    size_t path_len;
} psbt_change_t;

typedef struct {
    char address[PSBT_ADDR_MAX];
    uint64_t amount;
} psbt_dest_t;

typedef struct {
    policy_type_t policy;
    bool is_multisig;
    uint8_t m, n;
    bool has_cosigners;

    size_t num_inputs;
    uint64_t input_amount;
    uint64_t spend_amount;      /* external destinations */
    uint64_t change_amount;     /* everything back to this seed, receive included */
    int64_t fee;                /* inputs - outputs; negative is reported, not hidden */

    psbt_change_t *change;
    size_t num_change;
    psbt_dest_t *dest;
    size_t num_dest;

    bool has_op_return;
    uint8_t op_return[80];      /* the standard relay limit; longer payloads are cut */
    size_t op_return_len;

    bool is_high_fee;           /* fee > 25% of what is sent (change excluded) */

    struct wally_psbt *psbt;    /* the parsed PSBT, fingerprints filled */
} psbt_review_t;

/* Parse and review `bytes` (a binary PSBT) for `seed`. On success the
 * review owns the parsed PSBT; free it with psbt_review_free(). */
psbt_err_t psbt_review(const uint8_t *bytes, size_t len, const seed_t *seed,
                       btc_net_t net, psbt_review_t *out);
/* Base64 input, as most QR formats carry it. */
psbt_err_t psbt_review_base64(const char *b64, const seed_t *seed,
                              btc_net_t net, psbt_review_t *out);
void psbt_review_free(psbt_review_t *r);

/* True if the path's next-to-last index is the change branch (1). */
bool psbt_is_change_path(const uint32_t *path, size_t len);

/* get_total_output_value(): all outputs minus true change (change-branch
 * paths). Transfers to this seed's receive addresses stay in: the user
 * chose to send there. */
uint64_t psbt_total_sent(uint64_t total_out, const psbt_change_t *change, size_t n);
/* has_high_fee(): fee above 25% of what is sent; never when nothing is. */
bool psbt_fee_is_high(int64_t fee, uint64_t sent);

/* has_matching_input_fingerprint(): a routing hint for choosing a seed,
 * which verifies nothing. True if any input names this seed's fingerprint,
 * or an all-zero one on a key the seed derives. */
bool psbt_has_matching_input_fingerprint(const uint8_t *bytes, size_t len, const seed_t *seed,
                                         btc_net_t net);

/* Sign the reviewed PSBT as SeedSigner does, and return the trimmed result
 * (the unsigned transaction plus only our signatures) as base64 in
 * `*b64_out`, to be freed with wally_free_string(). `*sigs_added` is how
 * many signatures signing added; 0 means signing did nothing, which the UI
 * must report rather than show a QR. */
psbt_err_t psbt_sign(psbt_review_t *r, const seed_t *seed, btc_net_t net,
                     char **b64_out, size_t *sigs_added);
