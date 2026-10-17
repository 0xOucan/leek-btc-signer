/*
 * The amount and address components against what SeedSigner's own
 * BtcAmount and FormattedAddress draw (vectors/seedsigner_ui.h, recorded by
 * running them headless).
 */
#include <stdio.h>
#include <string.h>

#include "check.h"
#include "widgets.h"
#include "vectors/seedsigner_ui.h"

#define N(a) (sizeof(a) / sizeof((a)[0]))

static void test_amounts(void)
{
    printf("== BtcAmount: every denomination and network\n");
    unsigned same = 0, fixed = 0;
    for (size_t i = 0; i < N(UI_AMOUNTS); i++) {
        const uiv_amount_t *v = &UI_AMOUNTS[i];
        btc_amount_t a;
        btc_amount_text(v->sats, v->denom, v->net, &a);
        char got[96];
        if (a.sats[0]) {
            snprintf(got, sizeof got, "%s\t|\t%s\t %s", a.main, a.sats, a.unit);
        } else {
            snprintf(got, sizeof got, "%s\t %s", a.main, a.unit);
        }
        if (strchr(v->drawn, 'E') != NULL) {
            /* The deliberate difference (widgets.h): Decimal's "9.9E-7" is
               written out in full. */
            char want[96];
            snprintf(want, sizeof want, "0.%08llu\t %s", (unsigned long long)v->sats, a.unit);
            CHECK(strcmp(got, want) == 0, "%llu sats: \"%s\", want \"%s\"",
                  (unsigned long long)v->sats, got, want);
            fixed++;
            continue;
        }
        /* The unit is always " <unit>" there; its text box drops the
           space when it wraps at a narrow width. */
        char want[96];
        snprintf(want, sizeof want, "%s", v->drawn);
        char *tab = strrchr(want, '\t');
        if (tab && tab[1] != ' ') {
            memmove(tab + 2, tab + 1, strlen(tab + 1) + 1);
            tab[1] = ' ';
        }
        CHECK(strcmp(got, want) == 0, "%llu sats denom %d net %d: \"%s\", SeedSigner \"%s\"",
              (unsigned long long)v->sats, v->denom, v->net, got, v->drawn);
        same += strcmp(got, want) == 0;
    }
    printf("   %u identical to SeedSigner, %u small btc amounts written out in full\n", same, fixed);
}

static void test_addresses(void)
{
    printf("== FormattedAddress: line widths and limits\n");
    unsigned same = 0, raises = 0;
    for (size_t i = 0; i < N(UI_ADDRESSES); i++) {
        const uiv_address_t *v = &UI_ADDRESSES[i];
        addr_line_t lines[ADDR_LINES_MAX];
        const int n = addr_layout(v->address, v->chars, v->max_lines, lines);
        if (v->lines[0] == '!') {
            CHECK(n < 0, "%s at %d: laid out where SeedSigner raises", v->address, v->chars);
            raises += n < 0;
            continue;
        }
        char got[512];
        size_t k = 0;
        for (int l = 0; l < n; l++) {
            if (l) got[k++] = '\n';
            for (size_t c = 0; c <= strlen(lines[l].text); c++) {
                for (int a = 0; a < lines[l].n_accents; a++) {
                    if (lines[l].accent_at[a] + lines[l].accent_len[a] == c) got[k++] = ']';
                }
                for (int a = 0; a < lines[l].n_accents; a++) {
                    if (lines[l].accent_at[a] == c) got[k++] = '[';
                }
                if (c < strlen(lines[l].text)) got[k++] = lines[l].text[c];
            }
        }
        got[k] = '\0';
        CHECK(n > 0 && strcmp(got, v->lines) == 0, "%s at %d chars, max %d:\n%s\nSeedSigner:\n%s",
              v->address, v->chars, v->max_lines, got, v->lines);
        same += n > 0 && strcmp(got, v->lines) == 0;
    }
    printf("   %u layouts identical, %u widths where both refuse\n", same, raises);
}

int main(void)
{
    test_amounts();
    test_addresses();
    return CHECK_DONE("test_widgets");
}
