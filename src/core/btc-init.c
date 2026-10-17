/*
 * Bitcoin library start-up. See btc-init.h.
 */
#include "btc-init.h"

#include <stdint.h>

#include "wally_core.h"

static void (*rand_fn)(void *buf, unsigned len);

void btc_random(void *buf, unsigned len)
{
    if (rand_fn) {
        rand_fn(buf, len);
    }
}

bool btc_init(void (*rand)(void *buf, unsigned len))
{
    rand_fn = rand;
    if (wally_init(0) != WALLY_OK) {
        return false;
    }
    uint8_t seed[WALLY_SECP_RANDOMIZE_LEN];
    rand(seed, sizeof seed);
    int ret = wally_secp_randomize(seed, sizeof seed);
    wally_bzero(seed, sizeof seed);
    return ret == WALLY_OK;
}
