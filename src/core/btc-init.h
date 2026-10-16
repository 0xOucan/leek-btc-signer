/*
 * Bitcoin library start-up: libwally and its secp256k1 context.
 */
#pragma once

#include <stdbool.h>

/* Initialise libwally and randomise the secp256k1 context against side
 * channels with 32 bytes from `rand`. Returns false if either step fails;
 * nothing may sign after a false. */
bool btc_init(void (*rand)(void *buf, unsigned len));
