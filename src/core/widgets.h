/*
 * Components shared by the review screens: ports of SeedSigner's BtcAmount
 * and FormattedAddress (seedsigner/gui/components.py), and their drawing.
 *
 * The text logic is pure and checked against what SeedSigner's own
 * components draw (sim/vectors/seedsigner_ui.h, by gen_ui_vectors.py). The
 * drawing is ours: one bitmap font, and on a monochrome panel the accented
 * parts of an address are underlined, since there is no second colour.
 *
 * One deliberate difference: SeedSigner formats a small amount in the btc
 * denomination through Python's Decimal, which prints 99 sats as "9.9E-7"
 * and zero as "0E-8". Here they read 0.00000099 and 0.00000000.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "btc.h"

/* SeedSigner's SETTING__BTC_DENOMINATION. */
typedef enum {
    BTC_DENOM_BTC,
    BTC_DENOM_SATS,
    BTC_DENOM_THRESHOLD,    /* btc from 0.01 btc, sats below; SeedSigner's default */
    BTC_DENOM_HYBRID,       /* "0.01|,001 sats" */
} btc_denom_t;

typedef struct {
    char main[48];      /* "0.12345678", "12,345", or the btc side of hybrid */
    char sats[32];      /* the sats side of hybrid, else "" */
    const char *unit;   /* "btc" "sats" "tBtc" "tSats" */
} btc_amount_t;

/* BtcAmount's text for `sats`. */
void btc_amount_text(uint64_t sats, btc_denom_t denom, btc_net_t net, btc_amount_t *out);

/* Draw an amount on one line from (x, y); returns the x past it. The unit
 * takes the network's colour role, as SeedSigner's icon does. */
int btc_amount_draw(int x, int y, uint64_t sats, btc_denom_t denom, btc_net_t net);

/* ---- Addresses --------------------------------------------------------- */

#define ADDR_LINES_MAX 8
#define ADDR_LINE_MAX 72

typedef struct {
    char text[ADDR_LINE_MAX + 1];   /* with spaces where nothing is drawn */
    uint8_t accent_at[2], accent_len[2];
    uint8_t n_accents;
} addr_line_t;

/*
 * FormattedAddress's layout: "{first 7} {middle} {last 7}" cut into evened
 * lines of at most `chars` characters, the first and last seven accented;
 * `max_lines` (0 for none) truncates with "...", and 1 shows only the ends.
 * Returns the line count, or -1 where SeedSigner's layout raises (a line
 * width that leaves the first or last line without its second part).
 */
int addr_layout(const char *address, int chars, int max_lines, addr_line_t lines[ADDR_LINES_MAX]);

/* Draw an address centred across the screen from y, in as many lines as it
 * needs at the body font; returns the y below it. */
int addr_draw(int y, const char *address, int max_lines);
