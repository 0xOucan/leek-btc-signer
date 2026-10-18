/*
 * Multisig descriptors against SeedSigner's own handling
 * (seedsigner_multisig.h, by gen_multisig_vectors.py): every wallet
 * descriptor scan (the type, whether it completes, the descriptor
 * DecodeQR gives back), ScanView's rewrite,
 * whether embit parses the result and calls it a basic multisig, the policy,
 * the keys' fingerprints, every address get_multisig_address() gave, and
 * every PSBTParser.verify_multisig_output() its tests asked.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "btc-init.h"
#include "descriptor.h"
#include "psbt-review.h"
#include "qr-decode.h"
#include "vectors/seedsigner_multisig.h"
#include "wally_core.h"
#include "wally_psbt.h"

#define N(a) (sizeof(a) / sizeof((a)[0]))

static void rand_fill(void *p, unsigned n) { memset(p, 0x5A, n); }

/* Allocated once for the suite: the self-test image has no PSRAM. The
   largest scan here is a few KB. */
#define ARENA_SIZE (16 * 1024)
#define FRAME_MAX 4096
static qr_decoder_t *dec;
static uint8_t *arena, *frame;
static char *desc;

static size_t unhex(const char *h, uint8_t *out, size_t cap)
{
    size_t n = 0;
    for (; h[0] && h[1] && n < cap; h += 2) {
        unsigned b;
        sscanf(h, "%2x", &b);
        out[n++] = (uint8_t)b;
    }
    return n;
}

static const char *st_name(qr_status_t st)
{
    switch (st) {
    case QR_STATUS_PART_COMPLETE: return "PART_COMPLETE";
    case QR_STATUS_PART_EXISTING: return "PART_EXISTING";
    case QR_STATUS_COMPLETE: return "COMPLETE";
    case QR_STATUS_FALSE: return "FALSE";
    case QR_STATUS_INVALID: return "INVALID";
    case QR_STATUS_TYPE_CHANGED:
    case QR_STATUS_ERROR: return "RAISES";
    }
    return "?";
}

static void test_scans(void)
{
    printf("== scans: DecodeQR's type, completion and get_wallet_descriptor()\n");
    dec = malloc(sizeof *dec);
    arena = malloc(ARENA_SIZE);
    frame = malloc(FRAME_MAX);
    desc = malloc(DESC_TEXT_MAX);
    if (!dec || !arena || !frame || !desc) {
        CHECK(0, "out of memory");
        goto out;
    }
    for (size_t i = 0; i < N(VEC_MS_SCANS); i++) {
        const vec_ms_scan_t *v = &VEC_MS_SCANS[i];
        qr_decoder_init(dec, arena, ARENA_SIZE);
        const char *status = NULL;
        for (unsigned f = 0; f < v->n_frames; f++) {
            const vec_ms_frame_t *fr = &v->frames[f];
            size_t n;
            if (fr->text) {
                n = strlen(fr->text);
                CHECK(n <= FRAME_MAX, "%zu: frame too long", i);
                memcpy(frame, fr->text, n < FRAME_MAX ? n : FRAME_MAX);
            } else {
                n = unhex(fr->hex, frame, FRAME_MAX);
            }
            const qr_status_t st = qr_decoder_add(dec, frame, n);
            status = st_name(st);
            if (st == QR_STATUS_ERROR || st == QR_STATUS_TYPE_CHANGED) {
                break;              /* SeedSigner raises: the scan ends */
            }
        }
        CHECK(v->status == NULL || strcmp(status, v->status) == 0, "%zu: status %s, want %s", i, status,
              v->status ? v->status : "");
        CHECK(strcmp(qr_type_name(dec->type), v->qr_type) == 0, "%zu: type %s, want %s", i,
              qr_type_name(dec->type), v->qr_type);
        CHECK(dec->complete == v->complete, "%zu: complete %d, want %d", i, dec->complete, v->complete);
        const bool got = dec->complete && qr_is_wallet_descriptor(dec) && qr_decoder_descriptor(dec, desc, DESC_TEXT_MAX);
        CHECK(got == (v->descriptor != NULL) && (!got || strcmp(desc, v->descriptor) == 0),
              "%zu: descriptor\n    got  %.100s\n    want %.100s", i, got ? desc : "(none)",
              v->descriptor ? v->descriptor : "(none)");
        qr_decoder_reset(dec);
    }
    printf("   %zu scans as SeedSigner's\n", N(VEC_MS_SCANS));
out:
    free(dec);
    free(arena);
    free(frame);
    free(desc);
}

static void test_descriptors(void)
{
    printf("== descriptors: rewrite, parse, policy, fingerprints, addresses\n");
    unsigned addrs = 0;
    for (size_t i = 0; i < N(VEC_MS_DESCRIPTORS); i++) {
        const vec_ms_descriptor_t *v = &VEC_MS_DESCRIPTORS[i];
        char *rw = malloc(DESC_TEXT_MAX);
        CHECK(rw && desc_scanview_rewrite(v->descriptor, rw, DESC_TEXT_MAX) && strcmp(rw, v->rewritten) == 0,
              "%zu: rewrite\n    got  %.120s\n    want %.120s", i, rw ? rw : "", v->rewritten);
        ms_descriptor_t *d = calloc(1, sizeof *d);
        const bool parsed = d && desc_parse(v->rewritten, d);
        CHECK(parsed == (v->error == NULL), "%zu: parse %s, SeedSigner %s (%.80s)", i, parsed ? "ok" : "failed",
              v->error ? v->error : "ok", v->rewritten);
        if (parsed && !v->error) {
            CHECK(d->basic == v->basic, "%zu: is_basic_multisig", i);
            if (v->basic) {
                char t[8], n[8], fps[200] = "";
                snprintf(t, sizeof t, "%d", d->threshold);
                snprintf(n, sizeof n, "%d", d->n);
                CHECK(strcmp(t, v->threshold) == 0 && strcmp(n, v->n) == 0, "%zu: policy %s of %s", i, t, n);
                size_t k = 0;
                for (int j = 0; j < d->n; j++) {
                    k += (size_t)snprintf(fps + k, sizeof fps - k, "%s%02x%02x%02x%02x", j ? "," : "",
                                          d->fingerprints[j][0], d->fingerprints[j][1], d->fingerprints[j][2],
                                          d->fingerprints[j][3]);
                }
                CHECK(strcmp(fps, v->fingerprints) == 0, "%zu: fingerprints %s, want %s", i, fps, v->fingerprints);
            }
            for (unsigned a = 0; a < v->n_addresses; a++) {
                const vec_ms_addr_t *x = &v->addresses[a];
                char addr[100];
                CHECK(desc_address(d, x->index, x->change, x->net, addr, sizeof addr) && strcmp(addr, x->address) == 0,
                      "%zu: %s %u: %s, want %s", i, x->change ? "change" : "receive", x->index, addr, x->address);
                addrs++;
            }
        }
        if (d) desc_free(d);
        free(d);
        free(rw);
    }
    printf("   %zu descriptors, %u addresses as SeedSigner's\n", N(VEC_MS_DESCRIPTORS), addrs);
}

static void test_verify(void)
{
    printf("== verify_multisig_output: every call SeedSigner's tests made\n");
    for (size_t i = 0; i < N(VEC_MS_VERIFY); i++) {
        const vec_ms_verify_t *v = &VEC_MS_VERIFY[i];
        seed_t s;
        CHECK(seed_from_mnemonic(&s, v->mnemonic, v->passphrase) == SEED_OK, "seed");
        psbt_review_t r;
        const psbt_err_t err = psbt_review_base64(v->psbt, &s, v->net, &r);
        CHECK(err == PSBT_OK, "%zu: review %s", i, psbt_err_name(err));
        if (err == PSBT_OK) {
            ms_descriptor_t *d = calloc(1, sizeof *d);
            char *rw = malloc(DESC_TEXT_MAX);
            /* The descriptor as SeedSigner's ScanView holds it. */
            CHECK(d && rw && desc_scanview_rewrite(v->descriptor, rw, DESC_TEXT_MAX) && desc_parse(rw, d),
                  "%zu: descriptor", i);
            const bool got = psbt_verify_multisig_change(&r, d, v->change_num);
            CHECK(got == v->result, "%zu: change %u verified %d, SeedSigner %d", i, v->change_num, got, v->result);
            if (d) desc_free(d);
            free(d);
            free(rw);
            psbt_review_free(&r);
        }
        seed_clear(&s);
    }
    printf("   %zu verifications as SeedSigner's\n", N(VEC_MS_VERIFY));
}

int main(void)
{
    btc_init(rand_fill);
    test_scans();
    test_descriptors();
    test_verify();
    return CHECK_DONE("test_descriptor");
}
