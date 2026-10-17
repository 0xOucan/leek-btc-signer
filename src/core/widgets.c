/* Amount and address components. See widgets.h. */
#include "widgets.h"

#include <stdio.h>
#include <string.h>

#include "gfx.h"
#include "theme.h"

/* ---- Amounts ------------------------------------------------------------ */

/* `v` with a comma every three digits, as Python's "{:,}". */
static void commas(uint64_t v, char *out, size_t cap)
{
    char digits[24];
    const int n = snprintf(digits, sizeof digits, "%llu", (unsigned long long)v);
    size_t k = 0;
    for (int i = 0; i < n && k + 1 < cap; i++) {
        if (i > 0 && (n - i) % 3 == 0 && k + 2 < cap) {
            out[k++] = ',';
        }
        out[k++] = digits[i];
    }
    out[k] = '\0';
}

void btc_amount_text(uint64_t sats, btc_denom_t denom, btc_net_t net, btc_amount_t *out)
{
    const bool main_net = net == BTC_NET_MAIN;
    const char *btc_unit = main_net ? "btc" : "tBtc";
    const char *sats_unit = main_net ? "sats" : "tSats";
    out->main[0] = out->sats[0] = '\0';

    const bool million = sats >= 1000000ull;
    const bool whole_millions = million && sats % 1000000ull == 0;
    if (denom == BTC_DENOM_BTC || (denom == BTC_DENOM_THRESHOLD && million) ||
        (denom == BTC_DENOM_HYBRID && whole_millions) || sats > 10000000000ull) {
        /* Eight decimals; one for whole coins, two for whole millions of
           sats - the same digits SeedSigner's quantize() leaves. */
        char whole[24];
        commas(sats / 100000000ull, whole, sizeof whole);
        char frac[9];
        snprintf(frac, sizeof frac, "%08llu", (unsigned long long)(sats % 100000000ull));
        if (sats >= 100000000ull && sats % 100000000ull == 0) {
            frac[1] = '\0';
        } else if (whole_millions) {
            frac[2] = '\0';
        }
        snprintf(out->main, sizeof out->main, "%s.%s", whole, frac);
        if (strlen(out->main) >= 12) {
            /* Too wide: the reference keeps two decimals and an ellipsis. */
            frac[2] = '\0';
            snprintf(out->main, sizeof out->main, "%s.%s...", whole, frac);
        }
        out->unit = btc_unit;
    } else if (denom == BTC_DENOM_SATS || denom == BTC_DENOM_THRESHOLD ||
               (denom == BTC_DENOM_HYBRID && !million)) {
        commas(sats, out->main, sizeof out->main);
        out->unit = sats_unit;
    } else {
        /* Hybrid, from a million sats, not whole millions, up to 100 btc:
           two decimals of btc, then the last six digits of the comma form
           with its leading zeros dropped (a leading comma stays, as there). */
        snprintf(out->main, sizeof out->main, "%llu.%02llu", (unsigned long long)(sats / 100000000ull),
                 (unsigned long long)(sats % 100000000ull / 1000000ull));
        char all[32];
        commas(sats, all, sizeof all);
        const size_t n = strlen(all);
        const char *tail = n > 7 ? all + n - 7 : all;
        while (*tail == '0') {
            tail++;
        }
        snprintf(out->sats, sizeof out->sats, "%s", tail);
        out->unit = sats_unit;
    }
}

int btc_amount_draw(int x, int y, uint64_t sats, btc_denom_t denom, btc_net_t net)
{
    btc_amount_t a;
    btc_amount_text(sats, denom, net, &a);
    const gfx_color_t unit_c = net == BTC_NET_MAIN ? C_ACCENT : net == BTC_NET_TEST ? C_TESTNET : C_REGTEST;
    x = gfx_text(x, y, a.main, THEME.body, C_FG);
    if (a.sats[0]) {
        x = gfx_text(x, y, "|", THEME.body, C_MUTED);
        x = gfx_text(x, y, a.sats, THEME.body, C_FG);
    }
    x = gfx_text(x, y, " ", THEME.body, C_FG);
    return gfx_text(x, y, a.unit, THEME.body, unit_c);
}

/* ---- Addresses ---------------------------------------------------------- */

/* Python's str.split() on a slice of the display string: the k-th token. */
static bool token(const char *s, int len, int k, int *at, int *n)
{
    int i = 0;
    for (int t = 0; ; t++) {
        while (i < len && s[i] == ' ') i++;
        if (i >= len) return false;
        const int start = i;
        while (i < len && s[i] != ' ') i++;
        if (t == k) {
            *at = start;
            *n = i - start;
            return true;
        }
    }
}

/* Put `n` characters of `src` at column `col` of the line, or just after
   what is already there: at the narrowest widths the reference draws "..."
   and the last seven characters over each other, and here they sit side by
   side instead. */
static void put(addr_line_t *l, int col, const char *src, int n, bool accent)
{
    size_t len = strlen(l->text);
    if ((size_t)col < len) {
        col = (int)len;
    }
    while ((int)len < col && len < ADDR_LINE_MAX) {
        l->text[len++] = ' ';
    }
    for (int i = 0; i < n && col + i < ADDR_LINE_MAX; i++) {
        l->text[col + i] = src[i];
    }
    if ((size_t)(col + n) > len) {
        len = (size_t)(col + n) < ADDR_LINE_MAX ? (size_t)(col + n) : ADDR_LINE_MAX;
    }
    l->text[len] = '\0';
    if (accent && l->n_accents < 2) {
        l->accent_at[l->n_accents] = (uint8_t)col;
        l->accent_len[l->n_accents] = (uint8_t)n;
        l->n_accents++;
    }
}

int addr_layout(const char *address, int chars, int max_lines, addr_line_t lines[ADDR_LINES_MAX])
{
    const int n = 7;
    const int alen = (int)strlen(address);
    if (alen < 2 * n || alen > 2 * ADDR_LINE_MAX || chars < 1) {
        return -1;
    }
    char disp[2 * ADDR_LINE_MAX + 3];
    const int dlen = snprintf(disp, sizeof disp, "%.*s %.*s %s", n, address, alen - 2 * n,
                              address + n, address + alen - n);
    memset(lines, 0, sizeof(addr_line_t) * ADDR_LINES_MAX);

    if (max_lines == 1) {
        put(&lines[0], 0, address, n, true);
        put(&lines[0], n, "...", 3, false);
        put(&lines[0], n + 3, address + alen - n, n, true);
        return 1;
    }

    int num = (dlen + chars - 1) / chars;
    const int per = (dlen + num - 1) / num;      /* evened out */
    if (num > ADDR_LINES_MAX) {
        return -1;
    }
    int out = 0;
    for (int i = 0; i < num; i++) {
        const char *cur = disp + i * per;
        int clen = dlen - i * per;
        if (clen > per) clen = per;
        if (clen < 0) clen = 0;
        addr_line_t *l = &lines[out++];
        int at0, n0, at1, n1;
        if (i == 0) {
            if (!token(cur, clen, 0, &at0, &n0) || !token(cur, clen, 1, &at1, &n1)) {
                return -1;      /* split()[1]: IndexError */
            }
            put(l, 0, cur + at0, n0, true);
            put(l, n + 1, cur + at1, n1, false);
        } else if (i == num - 1) {
            if (!token(cur, clen, 0, &at0, &n0) || !token(cur, clen, 1, &at1, &n1)) {
                return -1;
            }
            put(l, 0, cur + at0, n0, false);
            put(l, clen - n, cur + at1, n1, true);
        } else if (max_lines && i == max_lines - 1) {
            char t[ADDR_LINE_MAX + 4];
            const int keep = clen - n - 3 > 0 ? clen - n - 3 : 0;
            snprintf(t, sizeof t, "%.*s...", keep, cur);
            put(l, 0, t, (int)strlen(t), false);
            put(l, clen - n, address + alen - n, n, true);
            break;
        } else {
            put(l, 0, cur, clen, false);
        }
    }
    return out;
}

int addr_draw(int y, const char *address, int max_lines)
{
    const gfx_font_t *f = THEME.body;
    const int cw = f->advance * f->scale;
    const int chars = (GFX_W - 2 * THEME.pad) / cw;
    addr_line_t lines[ADDR_LINES_MAX];
    int n = addr_layout(address, chars, max_lines, lines);
    if (n < 0) {
        /* A width the reference cannot lay out: fall back to its one-line
           form rather than nothing. */
        n = addr_layout(address, chars, 1, lines);
    }
    for (int i = 0; i < n; i++) {
        const int w = (int)strlen(lines[i].text) * cw;
        const int x = (GFX_W - w) / 2;
        gfx_text(x, y, lines[i].text, f, C_MUTED);
        for (int a = 0; a < lines[i].n_accents; a++) {
            char seg[ADDR_LINE_MAX + 1];
            snprintf(seg, sizeof seg, "%.*s", lines[i].accent_len[a], lines[i].text + lines[i].accent_at[a]);
            const int ax = x + lines[i].accent_at[a] * cw;
            gfx_fill_rect(ax, y, (int)strlen(seg) * cw, gfx_font_height(f), C_BG);
            gfx_text(ax, y, seg, f, C_ACCENT);
            if (GFX_MONO) {
                gfx_hline(ax, y + gfx_font_height(f), (int)strlen(seg) * cw - f->scale, C_ACCENT);
            }
        }
        y += THEME.line_h;
    }
    return y;
}
