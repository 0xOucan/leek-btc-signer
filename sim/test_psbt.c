/*
 * T19-T20: psbt-review.c against every PSBT SeedSigner's own test suite
 * parses (sim/vectors/seedsigner_psbt.h, scripts/vectors/gen_psbt_vectors.py).
 * Each case must fail with SeedSigner's error, or produce SeedSigner's
 * review and, once signed, SeedSigner's signed PSBT byte for byte.
 */
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "btc-init.h"
#include "btc.h"
#include "check.h"
#include "psbt-review.h"
#include "vectors/seedsigner_psbt.h"
#include "wally_core.h"
#include "wally_crypto.h"
#include "wally_psbt.h"
#include "wally_transaction.h"

#define N(a) (sizeof(a) / sizeof((a)[0]))

static void fake_rand(void *buf, unsigned len) { memset(buf, 0x42, len); }

/* SeedSigner raises builtin RuntimeError for both mixed inputs and an
 * unsupported policy, and ValueError for a malformed multisig script. */
static bool err_matches(psbt_err_t e, const char *py)
{
    if (strcmp(py, "RuntimeError") == 0)
        return e == PSBT_ERR_UNSUPPORTED || e == PSBT_ERR_MIXED_INPUTS;
    if (strcmp(py, "ValueError") == 0)
        return e == PSBT_ERR_INVALID;
    return strcmp(psbt_err_name(e), py) == 0;
}

static void path_str(const uint32_t *p, size_t n, char *out)
{
    out[0] = '\0';
    for (size_t i = 0; i < n; i++) {
        char part[16];
        if (p[i] >= 0x80000000u) snprintf(part, sizeof part, "%s%uh", i ? "/" : "", (unsigned)(p[i] - 0x80000000u));
        else snprintf(part, sizeof part, "%s%u", i ? "/" : "", (unsigned)p[i]);
        strcat(out, part);
    }
}

static void hex(const uint8_t *b, size_t n, char *out)
{
    for (size_t i = 0; i < n; i++) sprintf(out + 2 * i, "%02x", b[i]);
    out[2 * n] = '\0';
}

static bool tx_equal(const struct wally_tx *a, const struct wally_tx *b)
{
    unsigned char x[4096], y[4096];
    size_t lx = 0, ly = 0;
    return wally_tx_to_bytes(a, 0, x, sizeof x, &lx) == WALLY_OK &&
           wally_tx_to_bytes(b, 0, y, sizeof y, &ly) == WALLY_OK && lx == ly && memcmp(x, y, lx) == 0;
}

/*
 * Taproot signatures depend on the libsecp256k1 build: embit's bundled one
 * predates the BIP-340 change in how an absent aux_rand is treated, so its
 * nonces differ from current libsecp256k1's. Both are valid BIP-340. So
 * for a PSBT whose signatures are Schnorr, require the same transaction and
 * witness shape as SeedSigner's, and that BOTH signatures verify against
 * the input's BIP-341 sighash and its output key. SeedSigner's verifying
 * under our sighash also proves the sighash is right.
 */
static bool taproot_equivalent(const char *unsigned_b64, const char *ours, const char *theirs)
{
    struct wally_psbt *u = NULL, *a = NULL, *b = NULL;
    bool ok = wally_psbt_from_base64(unsigned_b64, 0, &u) == WALLY_OK &&
              wally_psbt_from_base64(ours, 0, &a) == WALLY_OK &&
              wally_psbt_from_base64(theirs, 0, &b) == WALLY_OK &&
              a->num_inputs == b->num_inputs && tx_equal(a->tx, b->tx);
    size_t schnorr = 0;
    for (size_t i = 0; ok && i < a->num_inputs; i++) {
        const struct wally_psbt_input *ai = &a->inputs[i], *bi = &b->inputs[i];
        if (!bi->final_witness) {
            ok = !ai->final_witness && ai->signatures.num_items == bi->signatures.num_items;
            for (size_t k = 0; ok && k < ai->signatures.num_items; k++) {
                ok = ai->signatures.items[k].value_len == bi->signatures.items[k].value_len &&
                     memcmp(ai->signatures.items[k].value, bi->signatures.items[k].value,
                            ai->signatures.items[k].value_len) == 0;
            }
            continue;
        }
        ok = ai->final_witness && ai->final_witness->num_items == 1 && bi->final_witness->num_items == 1 &&
             ai->final_witness->items[0].witness_len == bi->final_witness->items[0].witness_len &&
             ai->final_witness->items[0].witness_len >= 64 && u->inputs[i].witness_utxo &&
             u->inputs[i].witness_utxo->script_len == 34;
        if (!ok) break;
        const unsigned char *spk = u->inputs[i].witness_utxo->script;
        unsigned char h[32];
        ok = wally_psbt_get_input_signature_hash(u, i, u->tx, spk, 34, 0, h, sizeof h) == WALLY_OK &&
             wally_ec_sig_verify(spk + 2, 32, h, sizeof h, EC_FLAG_SCHNORR,
                                 ai->final_witness->items[0].witness, 64) == WALLY_OK &&
             wally_ec_sig_verify(spk + 2, 32, h, sizeof h, EC_FLAG_SCHNORR,
                                 bi->final_witness->items[0].witness, 64) == WALLY_OK;
        schnorr++;
    }
    if (u) wally_psbt_free(u);
    if (a) wally_psbt_free(a);
    if (b) wally_psbt_free(b);
    return ok && schnorr > 0;
}

/* SeedSigner's test_get_total_output_value and test_has_high_fee tables. */
static void test_fee_rules(void)
{
    const uint32_t H = 0x80000000u;
    psbt_change_t recv = { .path = { H + 84, H, H, 0, 0 }, .path_len = 5 };
    psbt_change_t chg = { .path = { H + 84, H, H, 1, 0 }, .path_len = 5 };
    psbt_change_t chg5 = { .path = { H + 84, H, H, 1, 5 }, .path_len = 5 };

    /* get_total_output_value */
    CHECK(psbt_total_sent(10 + 20 + 30, NULL, 0) == 60, "no change");
    chg.amount = 123;
    CHECK(psbt_total_sent(123, &chg, 1) == 0, "only change");
    recv.amount = 200; chg.amount = 300;
    psbt_change_t mix[2] = { recv, chg };
    CHECK(psbt_total_sent(600, mix, 2) == 300, "self-transfer stays, change goes");

    /* has_high_fee: (inputs, outputs, change) */
    chg.amount = 25;
    CHECK(!psbt_fee_is_high(180 - 150, psbt_total_sent(150, &chg, 1)), "24%% is not high");
    CHECK(psbt_fee_is_high(190 - 150, psbt_total_sent(150, &chg, 1)), "32%% is high");
    CHECK(!psbt_fee_is_high(75 - 60, psbt_total_sent(60, NULL, 0)), "exactly 25%% is not high");
    chg5.amount = 123;
    CHECK(!psbt_fee_is_high(130 - 123, psbt_total_sent(123, &chg5, 1)), "only change: never high");
    CHECK(!psbt_fee_is_high(660 - 600, psbt_total_sent(600, mix, 2)), "20%% with a self-transfer");
    CHECK(psbt_fee_is_high(700 - 600, psbt_total_sent(600, mix, 2)), "33%% with a self-transfer");
    CHECK(!psbt_fee_is_high(-5, 100), "a negative fee is not high");
}

int main(void)
{
    CHECK(btc_init(fake_rand), "btc_init");
    test_fee_rules();
    unsigned rejected = 0, reviewed = 0, signed_same = 0, sig_mismatch = 0, stricter = 0, taproot_valid = 0;

    for (size_t i = 0; i < N(VEC_PSBT); i++) {
        const vec_psbt_t *v = &VEC_PSBT[i];
        seed_t seed;
        if (seed_from_mnemonic(&seed, v->mnemonic, v->passphrase) != SEED_OK) {
            CHECK(0, "case %zu: seed", i);
            continue;
        }
        psbt_review_t r;
        psbt_err_t err = psbt_review_base64(v->psbt, &seed, v->net, &r);

        if (v->error) {
            CHECK(err_matches(err, v->error), "case %zu %s: want %s, got %s",
                  i, v->test, v->error, psbt_err_name(err));
            rejected += err_matches(err, v->error);
            seed_clear(&seed);
            continue;
        }
        /* Deliberate difference: a PSBT with no outputs, which SeedSigner
         * reviews as everything to fees without its high-fee warning. */
        if (v->n_dest == 0 && v->n_change == 0 && v->op_return_hex == NULL) {
            CHECK(err == PSBT_ERR_NO_OUTPUTS, "case %zu %s: no outputs must be refused, got %s",
                  i, v->test, psbt_err_name(err));
            stricter += err == PSBT_ERR_NO_OUTPUTS;
            if (err == PSBT_OK) psbt_review_free(&r);
            seed_clear(&seed);
            continue;
        }
        if (err != PSBT_OK) {
            CHECK(0, "case %zu %s: SeedSigner accepts, we refuse with %s", i, v->test, psbt_err_name(err));
            seed_clear(&seed);
            continue;
        }

        const char *pol = policy_type_name(r.policy);
        bool same = (pol == NULL) == (v->policy == NULL) && (!pol || strcmp(pol, v->policy) == 0);
        CHECK(same, "case %zu %s: policy %s vs %s", i, v->test, pol ? pol : "None", v->policy ? v->policy : "None");
        CHECK(r.is_multisig == (v->m >= 0) && (!r.is_multisig || (r.m == v->m && r.n == v->n)),
              "case %zu %s: m-of-n", i, v->test);
        CHECK(r.has_cosigners == v->has_cosigners, "case %zu %s: cosigners", i, v->test);
        CHECK(r.num_inputs == v->num_inputs && r.input_amount == v->input_amount,
              "case %zu %s: inputs", i, v->test);
        CHECK(r.spend_amount == v->spend_amount, "case %zu %s: spend %" PRIu64 " vs %" PRIu64,
              i, v->test, r.spend_amount, v->spend_amount);
        CHECK(r.change_amount == v->change_amount, "case %zu %s: change %" PRIu64 " vs %" PRIu64,
              i, v->test, r.change_amount, v->change_amount);
        CHECK(r.fee == v->fee, "case %zu %s: fee", i, v->test);
        CHECK(r.is_high_fee == v->is_high_fee, "case %zu %s: high fee", i, v->test);

        CHECK(r.num_change == v->n_change, "case %zu %s: %zu change outputs vs %u",
              i, v->test, r.num_change, v->n_change);
        for (size_t k = 0; k < r.num_change && k < v->n_change; k++) {
            char p[128];
            path_str(r.change[k].path, r.change[k].path_len, p);
            CHECK(r.change[k].output_index == v->change[k].output_index &&
                  strcmp(r.change[k].address, v->change[k].address) == 0 &&
                  r.change[k].amount == v->change[k].amount && strcmp(p, v->change[k].path) == 0,
                  "case %zu %s: change %zu (%s at %s)", i, v->test, k, r.change[k].address, p);
        }
        CHECK(r.num_dest == v->n_dest, "case %zu %s: %zu destinations vs %u", i, v->test, r.num_dest, v->n_dest);
        for (size_t k = 0; k < r.num_dest && k < v->n_dest; k++) {
            CHECK(strcmp(r.dest[k].address, v->dest[k].address) == 0 && r.dest[k].amount == v->dest[k].amount,
                  "case %zu %s: destination %zu %s vs %s", i, v->test, k, r.dest[k].address, v->dest[k].address);
        }
        char op[200];
        if (r.has_op_return) hex(r.op_return, r.op_return_len, op);
        CHECK(r.has_op_return == (v->op_return_hex != NULL) &&
              (!r.has_op_return || strcmp(op, v->op_return_hex) == 0), "case %zu %s: OP_RETURN", i, v->test);
        reviewed++;

        char *b64 = NULL;
        size_t added = 0;
        psbt_err_t serr = psbt_sign(&r, &seed, v->net, &b64, &added);
        CHECK(serr == PSBT_OK, "case %zu %s: signing failed: %s", i, v->test, psbt_err_name(serr));
        if (serr == PSBT_OK) {
            CHECK((int)added == v->sigs_added, "case %zu %s: %zu signatures added vs %d",
                  i, v->test, added, v->sigs_added);
            if (v->signed_psbt && strcmp(b64, v->signed_psbt) == 0) {
                signed_same++;
            } else if (v->signed_psbt && taproot_equivalent(v->psbt, b64, v->signed_psbt)) {
                taproot_valid++;
            } else {
                sig_mismatch++;
                if (sig_mismatch <= 3) {
                    printf("  signed PSBT differs, case %zu %s (policy %s)\n    ours %s\n    seed %s\n",
                           i, v->test, pol ? pol : "None", b64, v->signed_psbt);
                }
            }
        }
        wally_free_string(b64);
        psbt_review_free(&r);
        seed_clear(&seed);
    }
    CHECK(sig_mismatch == 0, "%u signed PSBTs differ from SeedSigner's", sig_mismatch);
    printf("  %zu PSBTs: %u rejected as SeedSigner rejects them, %u reviewed alike,\n"
           "  %u signed byte-identical, %u taproot signed validly (see taproot_equivalent),\n"
           "  %u refused where SeedSigner signs a transaction with no outputs\n",
           N(VEC_PSBT), rejected, reviewed, signed_same, taproot_valid, stricter);
    wally_cleanup(0);
    return CHECK_DONE("test_psbt");
}
