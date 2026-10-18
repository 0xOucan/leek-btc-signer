/*
 * Multisig wallet descriptors, as SeedSigner handles them (T47): ScanView's
 * rewrite of "/0/(star)" so the descriptor covers change too, embit's
 * Descriptor (is_basic_multisig, the keys' fingerprints), embit_utils'
 * get_multisig_policy() and get_multisig_address(), and Descriptor.owns(),
 * which PSBTParser.verify_multisig_output() asks of a change output.
 * Parsing and scripts are libwally's. See descriptor.c.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "btc.h"

struct wally_descriptor;
struct wally_psbt_output;

#define DESC_TEXT_MAX 1600
#define DESC_KEYS_MAX 16

typedef struct ms_descriptor {
    struct wally_descriptor *wd;    /* libwally's parse */
    char text[DESC_TEXT_MAX];       /* as SeedSigner holds it (rewritten) */
    bool basic;                     /* is_basic_multisig */
    int threshold, n;               /* get_multisig_policy() */
    uint8_t fingerprints[DESC_KEYS_MAX][4];
} ms_descriptor_t;

/* ScanView's rewrite: "/0/(star)" after a key origin becomes "/{0,1}/(star)"; or, if
 * there is none, a key with no child path gets "/{0,1}/(star)". False if `out`
 * is too small. */
bool desc_scanview_rewrite(const char *in, char *out, size_t cap);

/* Descriptor.from_string() on a descriptor already rewritten. False where
 * embit raises. */
bool desc_parse(const char *text, ms_descriptor_t *out);
void desc_free(ms_descriptor_t *d);

/* get_multisig_address(): the address at <change>/<index> on `net`. */
bool desc_address(const ms_descriptor_t *d, uint32_t index, bool change, btc_net_t net, char *out,
                  size_t cap);

/* Descriptor.owns(): does a PSBT output (its keypaths and script) belong to
 * this wallet? */
bool desc_owns(const ms_descriptor_t *d, const struct wally_psbt_output *out, const uint8_t *spk,
               size_t spk_len);

/* Descriptor.from_string() as a check: GenericWalletQrDecoder's validation
 * of a scanned descriptor, as it was scanned. */
bool desc_check(const char *text, size_t len);

/* DecodeQR.multisig_setup_file_to_descriptor(): a multisig setup file (the
 * Coldcard/Keystone export) as a descriptor, NUL-terminated in `out`. False
 * where SeedSigner raises. Lowercasing is ASCII. */
bool desc_from_setup_file(const char *text, size_t len, char *out, size_t cap);
