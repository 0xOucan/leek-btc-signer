/*
 * Bitcoin library start-up. See btc-init.h.
 */
#include "btc-init.h"

#include <stdint.h>

#include "wally_core.h"

bool btc_init(void (*rand)(void *buf, unsigned len))
{
    if (wally_init(0) != WALLY_OK) {
        return false;
    }
    uint8_t seed[WALLY_SECP_RANDOMIZE_LEN];
    rand(seed, sizeof seed);
    int ret = wally_secp_randomize(seed, sizeof seed);
    wally_bzero(seed, sizeof seed);
    return ret == WALLY_OK;
}
