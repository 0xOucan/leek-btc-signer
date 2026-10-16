/*
 * libwally-core build configuration for Leek BTC Signer, host and ESP32-S3.
 * The amalgamation (upstream/src/amalgamation/combined.c) includes this.
 */
#ifndef LIBWALLYCORE_CONFIG_H
#define LIBWALLYCORE_CONFIG_H

#include <stddef.h>

/* The S3's Xtensa LX7 faults on unaligned 32-bit loads: never assume them,
 * and keep the host build identical so it tests the same code paths. */
#define HAVE_UNALIGNED_ACCESS 0

#include "ccan_config.h"

#endif
