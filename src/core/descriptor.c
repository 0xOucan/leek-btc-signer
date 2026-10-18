/*
 * Multisig wallet descriptors. See descriptor.h.
 *
 * SeedSigner holds descriptors in embit's form; libwally parses the same
 * descriptors once embit's multipath "{0,1}" is written as "<0;1>".
 */
#include "descriptor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pytext.h"
#include "wally_address.h"
#include "wally_bip32.h"
#include "wally_core.h"
#include "wally_crypto.h"
#include "wally_descriptor.h"
#include "wally_map.h"
#include "wally_psbt.h"
#include "wally_script.h"

/* ---- ScanView's rewrite ---------------------------------------------------- */

/* The character classes of scan_views.py's patterns, commas included, as
   written there. */
static bool in_hex(char c)  /* [0-9,a-f,A-F] */
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == ',';
}

static bool in_path(char c) /* [0-9,\/,h\'] */
{
    return (c >= '0' && c <= '9') || c == ',' || c == '/' || c == 'h' || c == '\'';
}

static bool in_key(char c)  /* [a-z,A-Z,0-9] */
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ',';
}

/* `\[[hex]+?\/[path]+?\]` at s: the length matched, or 0. */
static size_t origin_at(const char *s)
{
    if (s[0] != '[') return 0;
    size_t i = 1;
    while (in_hex(s[i])) i++;
    if (i == 1 || s[i] != '/') return 0;
    /* [hex]+? stops at the first "/", which the class excludes. */
    const size_t slash = i;
    i++;
    while (in_path(s[i]) && s[i] != ']') i++;
    if (i == slash + 1 || s[i] != ']') return 0;
    return i + 1;
}

/* Pattern 1: origin, then `.*?` (no newline) to the first "/0/(star)". Returns
   the offset of that "/0/(star)" from s, or -1. */
static long pattern1_at(const char *s)
{
    const size_t o = origin_at(s);
    if (o == 0) return -1;
    for (const char *p = s + o; *p && *p != '\n'; p++) {
        if (strncmp(p, "/0/*", 4) == 0) {
            return (long)(p - s);
        }
    }
    return -1;
}

/* Pattern 2: origin, then `[key]*?` up to the first "," or ")". Returns the
   offset of that terminator, or -1. */
static long pattern2_at(const char *s)
{
    const size_t o = origin_at(s);
    if (o == 0) return -1;
    for (const char *p = s + o;; p++) {
        if (*p == ',' || *p == ')') return (long)(p - s);
        if (!in_key(*p)) return -1;
    }
}

bool desc_scanview_rewrite(const char *in, char *out, size_t cap)
{
    bool any1 = false;
    for (const char *p = in; *p && !any1; p++) {
        any1 = pattern1_at(p) >= 0;
    }
    size_t k = 0;
#define PUT(c) do { if (k + 1 >= cap) return false; out[k++] = (c); } while (0)
#define PUTS(str) do { for (const char *q_ = (str); *q_; q_++) PUT(*q_); } while (0)
    const char *p = in;
    if (any1) {
        /* re.sub: each match runs from an origin to the next "/0/(star)". */
        while (*p) {
            const long m = pattern1_at(p);
            if (m >= 0) {
                for (long i = 0; i < m; i++) PUT(p[i]);
                PUTS("/{0,1}/*");
                p += m + 4;
            } else {
                PUT(*p);
                p++;
            }
        }
    } else {
        while (*p) {
            const long m = pattern2_at(p);
            if (m >= 0) {
                for (long i = 0; i < m; i++) PUT(p[i]);
                PUTS("/{0,1}/*");
                PUT(p[m]);
                p += m + 1;
            } else {
                PUT(*p);
                p++;
            }
        }
    }
    out[k] = '\0';
    return true;
#undef PUT
#undef PUTS
}

/* ---- Parsing ------------------------------------------------------------------ */

/* embit's "{a,b}" multipath, as libwally writes it: "<a;b>". */
static bool to_libwally(const char *in, char *out, size_t cap)
{
    size_t k = 0;
    bool brace = false;
    /* Descriptor.from_string() ignores whatever follows a '#', so a checksum
     * left stale by ScanView's rewrite is dropped rather than checked. */
    for (const char *p = in; *p && *p != '#'; p++) {
        char c = *p;
        if (c == '{') { c = '<'; brace = true; }
        else if (c == '}') { c = '>'; brace = false; }
        else if (c == ',' && brace) { c = ';'; }
        if (k + 1 >= cap) return false;
        out[k++] = c;
    }
    out[k] = '\0';
    return true;
}

/* is_basic_multisig: multi or sortedmulti, directly in wsh(), sh(wsh()) or
   sh(). */
static bool basic_multisig(const char *t, int *threshold)
{
    static const char *const prefixes[] = {
        "wsh(sortedmulti(", "wsh(multi(", "sh(wsh(sortedmulti(", "sh(wsh(multi(", "sh(sortedmulti(", "sh(multi(",
    };
    for (size_t i = 0; i < sizeof prefixes / sizeof prefixes[0]; i++) {
        const size_t n = strlen(prefixes[i]);
        if (strncmp(t, prefixes[i], n) == 0) {
            *threshold = atoi(t + n);
            return true;
        }
    }
    return false;
}

bool desc_parse(const char *text, ms_descriptor_t *out)
{
    memset(out, 0, sizeof *out);
    snprintf(out->text, sizeof out->text, "%s", text);
    char *lw = malloc(DESC_TEXT_MAX);
    if (lw == NULL || !to_libwally(text, lw, DESC_TEXT_MAX) ||
        wally_descriptor_parse(lw, NULL, WALLY_NETWORK_NONE, 0, &out->wd) != WALLY_OK) {
        free(lw);
        out->wd = NULL;
        return false;
    }
    free(lw);
    out->basic = basic_multisig(text, &out->threshold);
    uint32_t n = 0;
    wally_descriptor_get_num_keys(out->wd, &n);
    out->n = (int)n;
    for (uint32_t i = 0; i < n && i < DESC_KEYS_MAX; i++) {
        wally_descriptor_get_key_origin_fingerprint(out->wd, i, out->fingerprints[i], 4);
    }
    return true;
}

void desc_free(ms_descriptor_t *d)
{
    if (d->wd) {
        wally_descriptor_free(d->wd);
    }
    memset(d, 0, sizeof *d);
}

bool desc_check(const char *text, size_t len)
{
    if (memchr(text, '\0', len) != NULL || len >= DESC_TEXT_MAX) {
        return false;
    }
    char *in = malloc(len + 1), *lw = malloc(DESC_TEXT_MAX);
    struct wally_descriptor *wd = NULL;
    bool ok = false;
    if (in && lw) {
        memcpy(in, text, len);
        in[len] = '\0';
        ok = to_libwally(in, lw, DESC_TEXT_MAX) &&
             wally_descriptor_parse(lw, NULL, WALLY_NETWORK_NONE, 0, &wd) == WALLY_OK;
    }
    if (wd) {
        wally_descriptor_free(wd);
    }
    free(in);
    free(lw);
    return ok;
}

/* ---- DecodeQR.multisig_setup_file_to_descriptor() --------------------------------- */

#define SETUP_KEYS_MAX 9

typedef struct {
    const char *s;
    size_t n;
} span_t;

static bool is_ascii_digit(char c)
{
    return c >= '0' && c <= '9';
}

/* int() of a run of ASCII digits; too long to matter is refused. */
static bool digits_value(const char *s, size_t n, long *out)
{
    long v = 0;
    for (size_t i = 0; i < n; i++) {
        if (v > 100000000L) {
            return false;
        }
        v = v * 10 + (s[i] - '0');
    }
    *out = v;
    return true;
}

/* re.search(r'(\d+)\D*(\d+)', value): the first digit run and the next one;
   with none after it, the run itself split before its last digit, as the
   regex backtracks. */
static bool policy_numbers(span_t v, long *m, long *n)
{
    size_t i = 0;
    while (i < v.n && !is_ascii_digit(v.s[i])) i++;
    size_t j = i;
    while (j < v.n && is_ascii_digit(v.s[j])) j++;
    if (i == j) {
        return false;
    }
    size_t k = j;
    while (k < v.n && !is_ascii_digit(v.s[k])) k++;
    if (k < v.n) {
        size_t e = k;
        while (e < v.n && is_ascii_digit(v.s[e])) e++;
        return digits_value(v.s + i, j - i, m) && digits_value(v.s + k, e - k, n);
    }
    if (j - i < 2) {
        return false;
    }
    return digits_value(v.s + i, j - i - 1, m) && digits_value(v.s + j - 1, 1, n);
}

/* The code points in `s`. */
static size_t code_points(span_t s)
{
    size_t n = 0;
    for (size_t i = 0; i < s.n; i++) {
        n += ((uint8_t)s.s[i] & 0xC0) != 0x80;
    }
    return n;
}

static bool span_ieq(span_t s, const char *lit)
{
    const size_t n = strlen(lit);
    if (s.n != n) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        char c = s.s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != lit[i]) {
            return false;
        }
    }
    return true;
}

bool desc_from_setup_file(const char *text, size_t len, char *out, size_t cap)
{
    long m = 0, n = 0;
    span_t xfp[SETUP_KEYS_MAX], key[SETUP_KEYS_MAX];
    size_t x = 0;
    bool have_xpubs = false;        /* xpubs = [None] * n, once */
    long slots = 0;                 /* len(xpubs) */
    span_t derivation = { "", 0 };
    int format = -1;                /* 0: p2wsh, 1: p2sh-p2wsh */

    size_t pos = 0;
    for (;;) {
        /* text.split('\n') */
        const char *nl = memchr(text + pos, '\n', len - pos);
        const size_t end = nl ? (size_t)(nl - text) : len;
        span_t l = { text + pos, end - pos };
        const bool last = nl == NULL;
        pos = end + 1;

        if (l.n > 0 && l.s[0] == '#') {
            goto next;              /* a comment */
        }
        py_strip(&l.s, &l.n);
        const char *colon = memchr(l.s, ':', l.n);
        if (colon == NULL) {
            goto next;
        }
        span_t label = { l.s, (size_t)(colon - l.s) }, value = { colon + 1, l.n - label.n - 1 };
        py_strip(&label.s, &label.n);
        py_strip(&value.s, &value.n);

        if (span_ieq(label, "policy")) {
            if (!policy_numbers(value, &m, &n)) {
                return false;       /* "Policy line not supported" */
            }
        } else if (span_ieq(label, "derivation")) {
            derivation = value;
        } else if (span_ieq(label, "format")) {
            if (span_ieq(value, "p2wsh")) {
                format = 0;
            } else if (span_ieq(value, "p2sh-p2wsh") || span_ieq(value, "p2wsh-p2sh")) {
                format = 1;
            }
        } else if (code_points(label) == 8) {
            if (!have_xpubs) {
                have_xpubs = true;
                slots = n;
                if (slots > SETUP_KEYS_MAX) {
                    return false;   /* fails the policy check below anyway */
                }
            }
            if ((long)x >= slots) {
                return false;       /* IndexError */
            }
            xfp[x] = label;
            key[x] = value;
            x++;
        }
    next:
        if (last) {
            break;
        }
    }

    /* `None in xpubs or len(xpubs) != n`, then the policy, derivation and
       format checks. */
    if ((long)x != slots || slots != n || m <= 0 || m > 9 || n <= 0 || n > 9 || derivation.n == 0 || format < 0) {
        return false;
    }

    size_t k = 0;
#define PUT(c) do { if (k + 1 >= cap) return false; out[k++] = (c); } while (0)
#define PUTS(str) do { for (const char *q_ = (str); *q_; q_++) PUT(*q_); } while (0)
#define PUTN(p, len_) do { for (size_t i_ = 0; i_ < (len_); i_++) PUT((p)[i_]); } while (0)
    PUTS(format == 0 ? "wsh(sortedmulti(" : "sh(wsh(sortedmulti(");
    char num[4];
    snprintf(num, sizeof num, "%ld", m);
    PUTS(num);
    for (size_t i = 0; i < x; i++) {
        /* derivation[1:] while it starts with 'm', once per key; "'" as "h".
           The xfp is the label, lowercased. */
        if (derivation.n == 0) {
            return false;           /* derivation[0]: IndexError */
        }
        if (derivation.s[0] == 'm') {
            derivation.s++;
            derivation.n--;
        }
        PUTS(",[");
        for (size_t j = 0; j < xfp[i].n; j++) {
            char c = xfp[i].s[j];
            PUT(c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c);
        }
        for (size_t j = 0; j < derivation.n; j++) {
            PUT(derivation.s[j] == '\'' ? 'h' : derivation.s[j]);
        }
        PUT(']');
        PUTN(key[i].s, key[i].n);
        PUTS("/{0,1}/*");
    }
    PUTS(format == 0 ? "))" : ")))");
    out[k] = '\0';
#undef PUT
#undef PUTS
#undef PUTN
    return true;
}

/* ---- Scripts and addresses ------------------------------------------------------ */

/* libwally builds the inner (witness/redeem) script in `spk` before hashing
 * it, so the buffer must hold a whole multisig script, not just the 34-byte
 * scriptPubKey that comes out. */
#define SCRIPT_MAX 520

static bool derive_script(const ms_descriptor_t *d, uint32_t index, uint32_t branch, uint8_t *spk, size_t cap,
                          size_t *len)
{
    return d->wd && wally_descriptor_to_script(d->wd, 0, 0, 0, branch, index, 0, spk, cap, len) == WALLY_OK &&
           *len <= cap;
}

bool desc_address(const ms_descriptor_t *d, uint32_t index, bool change, btc_net_t net, char *out, size_t cap)
{
    static const char *const HRP[] = { "bc", "tb", "bcrt" };
    static const uint32_t NETWORK[] = { WALLY_NETWORK_BITCOIN_MAINNET, WALLY_NETWORK_BITCOIN_TESTNET,
                                        WALLY_NETWORK_BITCOIN_REGTEST };
    uint8_t spk[SCRIPT_MAX];
    size_t len = 0;
    if (!derive_script(d, index, change ? 1 : 0, spk, sizeof spk, &len)) {
        return false;
    }
    char *s = NULL;
    int ret;
    if (len == 23 && spk[0] == 0xa9) {
        ret = wally_scriptpubkey_to_address(spk, len, NETWORK[net], &s);    /* P2SH */
    } else {
        ret = wally_addr_segwit_from_bytes(spk, len, HRP[net], 0, &s);      /* P2WSH */
    }
    const bool ok = ret == WALLY_OK && s && strlen(s) < cap;
    if (ok) {
        snprintf(out, cap, "%s", s);
    }
    wally_free_string(s);
    return ok;
}

/* ---- Descriptor.owns() ------------------------------------------------------------ */

#define PATH_MAX_DEPTH 16

/* A key's allowed derivation (its child path): fixed indexes, one list of
   branches, and the wildcard. */
typedef struct {
    int n;
    struct { int kind; uint32_t v[4]; int nv; } el[PATH_MAX_DEPTH];     /* kind: 0 int, 1 branches, 2 wildcard */
} allowed_t;

static bool parse_allowed(const char *s, allowed_t *a)
{
    memset(a, 0, sizeof *a);
    while (*s) {
        if (*s == '/') { s++; continue; }
        if (a->n == PATH_MAX_DEPTH) return false;
        if (*s == '*') {
            a->el[a->n++].kind = 2;
            s++;
            if (*s == 'h' || *s == '\'') return false;   /* hardened wildcards: not this */
            continue;
        }
        if (*s == '<') {
            a->el[a->n].kind = 1;
            s++;
            while (*s && *s != '>') {
                if (a->el[a->n].nv == 4) return false;
                a->el[a->n].v[a->el[a->n].nv++] = (uint32_t)strtoul(s, (char **)&s, 10);
                if (*s == ';') s++;
            }
            if (*s != '>') return false;
            s++;
            a->n++;
            continue;
        }
        uint32_t v = (uint32_t)strtoul(s, (char **)&s, 10);
        if (*s == 'h' || *s == '\'') { v |= 0x80000000u; s++; }
        a->el[a->n].kind = 0;
        a->el[a->n].v[0] = v;
        a->n++;
    }
    return true;
}

/* AllowedDerivation.check_derivation: (idx, branch_idx), or false. */
static bool check_allowed(const allowed_t *a, const uint32_t *rest, size_t n, uint32_t *idx, uint32_t *branch)
{
    if ((int)n != a->n) return false;
    bool have_idx = false;
    *branch = 0;
    for (int i = 0; i < a->n; i++) {
        if (a->el[i].kind == 0) {
            if (a->el[i].v[0] != rest[i]) return false;
        } else if (a->el[i].kind == 1) {
            int found = -1;
            for (int k = 0; k < a->el[i].nv; k++) {
                if (a->el[i].v[k] == rest[i] && found < 0) found = k;
            }
            if (found < 0) return false;
            *branch = (uint32_t)found;
        } else {
            *idx = rest[i];
            have_idx = true;
        }
    }
    return have_idx;
}

static bool key_paths(const ms_descriptor_t *d, size_t k, uint32_t *origin, size_t *n_origin, allowed_t *allowed,
                      uint8_t own_fp[4])
{
    char *origin_s = NULL, *child_s = NULL, *key_s = NULL;
    bool ok = wally_descriptor_get_key_origin_path_str(d->wd, k, &origin_s) == WALLY_OK &&
              wally_descriptor_get_key_child_path_str(d->wd, k, &child_s) == WALLY_OK &&
              wally_descriptor_get_key(d->wd, k, &key_s) == WALLY_OK;
    if (ok) {
        char path[96];
        snprintf(path, sizeof path, "m/%s", origin_s);
        const int depth = origin_s[0] ? btc_parse_path(path, origin, PATH_MAX_DEPTH) : 0;
        ok = depth >= 0 && parse_allowed(child_s, allowed);
        *n_origin = depth > 0 ? (size_t)depth : 0;
    }
    if (ok) {
        /* my_fingerprint: the key's own, for the "short" derivation path. */
        struct ext_key xk;
        ok = bip32_key_from_base58(key_s, &xk) == WALLY_OK &&
             bip32_key_get_fingerprint(&xk, own_fp, 4) == WALLY_OK;
    }
    wally_free_string(origin_s);
    wally_free_string(child_s);
    wally_free_string(key_s);
    return ok;
}

bool desc_owns(const ms_descriptor_t *d, const struct wally_psbt_output *out, const uint8_t *spk, size_t spk_len)
{
    if (d->wd == NULL || spk == NULL) {
        return false;
    }
    for (size_t i = 0; i < out->keypaths.num_items; i++) {
        const struct wally_map_item *it = &out->keypaths.items[i];
        if (it->value_len < 4 || (it->value_len - 4) % 4 != 0) continue;
        const size_t depth = (it->value_len - 4) / 4;
        uint32_t path[32];
        if (depth > 32) continue;
        for (size_t j = 0; j < depth; j++) {
            const unsigned char *p = it->value + 4 + 4 * j;
            path[j] = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
        }
        for (int k = 0; k < d->n; k++) {
            uint32_t origin[PATH_MAX_DEPTH];
            size_t n_origin = 0;
            allowed_t allowed;
            uint8_t own[4];
            if (!key_paths(d, (size_t)k, origin, &n_origin, &allowed, own)) continue;
            const uint32_t *rest = NULL;
            size_t n_rest = 0;
            if (memcmp(it->value, d->fingerprints[k], 4) == 0 && n_origin <= depth &&
                memcmp(origin, path, n_origin * sizeof(uint32_t)) == 0) {
                rest = path + n_origin;
                n_rest = depth - n_origin;
            }
            if (memcmp(it->value, own, 4) == 0) {
                rest = path;
                n_rest = depth;
            }
            uint32_t idx = 0, branch = 0;
            if (rest && check_allowed(&allowed, rest, n_rest, &idx, &branch)) {
                /* Found: the script must be the one derived there. */
                uint8_t sc[SCRIPT_MAX];
                size_t len = 0;
                return derive_script(d, idx, branch, sc, sizeof sc, &len) && len == spk_len &&
                       memcmp(sc, spk, len) == 0;
            }
        }
    }
    return false;
}
