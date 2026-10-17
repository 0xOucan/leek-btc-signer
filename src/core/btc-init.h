/*
 * Bitcoin library start-up: libwally and its secp256k1 context.
 */
#pragma once

#include <stdbool.h>

/* Initialise libwally and randomise the secp256k1 context against side
 * channels with 32 bytes from `rand`. Returns false if either step fails;
 * nothing may sign after a false. */
bool btc_init(void (*rand)(void *buf, unsigned len));

/* Random bytes from the source btc_init was given: the platform's RNG on
 * the board, a fixed one in tests. For choices the user sees (the backup
 * test's word order and decoys), never for keys. */
void btc_random(void *buf, unsigned len);
