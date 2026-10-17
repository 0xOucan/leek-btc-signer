/*
 * Message signing against SeedSigner's own code (seedsigner_message.h, by
 * gen_message_vectors.py): embit_utils.parse_derivation_path() for every
 * path, its exceptions included, and for each seed, path, network and
 * message the address SeedSignMessageConfirmAddressView shows and the
 * signature sign_message() makes.
 */
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "btc.h"
#include "btc-init.h"
#include "vectors/seedsigner_message.h"

#define N(a) (sizeof(a) / sizeof((a)[0]))

static void rand_fill(void *p, unsigned n) { memset(p, 0x5A, n); }

static const char *script_code(const btc_msg_path_t *p)
{
    if (p->custom) return "cus";
    switch (p->script) {
    case BTC_SCRIPT_P2PKH:       return "leg";
    case BTC_SCRIPT_P2SH_P2WPKH: return "nes";
    case BTC_SCRIPT_P2WPKH:      return "nat";
    case BTC_SCRIPT_P2TR:        return "tr";
    default:                     return "?";
    }
}

static void test_paths(void)
{
    printf("== parse_derivation_path\n");
    for (size_t i = 0; i < N(VEC_MSG_PATHS); i++) {
        const vec_msg_path_t *v = &VEC_MSG_PATHS[i];
        btc_msg_path_t p;
        const char *err = btc_parse_derivation_path(v->path, &p);
        CHECK((err == NULL) == (v->error == NULL) && (!err || strcmp(err, v->error) == 0),
              "%s: error %s, want %s", v->path, err ? err : "none", v->error ? v->error : "none");
        if (err || v->error) continue;
        const char *net = p.net == 0 ? "M" : p.net == 1 ? "T,R" : NULL;
        CHECK(strcmp(script_code(&p), v->script_type) == 0, "%s: script %s", v->path, script_code(&p));
        CHECK((net == NULL) == (v->network == NULL) && (!net || strcmp(net, v->network) == 0), "%s: network",
              v->path);
        CHECK(p.is_change == v->is_change && p.index == v->index, "%s: change %d index %ld", v->path,
              p.is_change, p.index);
        CHECK(strcmp(p.wallet, v->wallet ? v->wallet : "") == 0, "%s: wallet %s", v->path, p.wallet);
        CHECK(p.clean == v->clean, "%s: clean_match", v->path);
    }
}

static void test_signatures(void)
{
    printf("== addresses and signatures\n");
    unsigned n = 0;
    for (size_t i = 0; i < N(VEC_MSG_SIGNS); i++) {
        const vec_msg_sign_t *v = &VEC_MSG_SIGNS[i];
        seed_t s;
        CHECK(seed_from_mnemonic(&s, v->mnemonic, "") == SEED_OK, "seed");
        btc_msg_path_t p;
        btc_parse_derivation_path(v->path, &p);
        char xpub[120], addr[100], sig[100];
        CHECK(btc_xpub(s.bytes, sizeof s.bytes, p.wallet, v->net, 0, xpub, sizeof xpub) &&
              btc_address(xpub, p.script, (uint32_t)p.index, p.is_change == 1, v->net, addr, sizeof addr) &&
              strcmp(addr, v->address) == 0, "%zu: address %s, want %s", i, addr, v->address);
        CHECK(btc_sign_message(&s, v->path, (const uint8_t *)v->message, strlen(v->message), sig, sizeof sig) &&
              strcmp(sig, v->signature) == 0, "%zu %s: signature\n    got  %s\n    want %s", i, v->path, sig,
              v->signature);
        seed_clear(&s);
        n++;
    }
    printf("   %u signatures and addresses as SeedSigner's\n", n);
}

int main(void)
{
    btc_init(rand_fill);
    test_paths();
    test_signatures();
    return CHECK_DONE("test_message");
}
