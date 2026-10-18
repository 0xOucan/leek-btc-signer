/* SeedSigner's DecodeQR and its decoders. See qr-decode.h. */

#include "qr-decode.h"

#include <stdlib.h>
#include <string.h>

#include "descriptor.h"
#include "pytext.h"
#include "tinf.h"
#include "wally_core.h"

bool qr_is_psbt(qr_type_t t)
{
    return t == QR_TYPE_PSBT_UR2 || t == QR_TYPE_PSBT_SPECTER ||
           t == QR_TYPE_PSBT_BASE64 || t == QR_TYPE_PSBT_BASE43 ||
           t == QR_TYPE_PSBT_BBQR;
}

bool qr_is_seed(qr_type_t t)
{
    return t == QR_TYPE_SEED_SEEDQR || t == QR_TYPE_SEED_COMPACTSEEDQR ||
           t == QR_TYPE_SEED_MNEMONIC || t == QR_TYPE_SEED_FOUR_LETTER_MNEMONIC;
}

static bool is_ur(qr_type_t t)
{
    return t == QR_TYPE_PSBT_UR2 || t == QR_TYPE_OUTPUT_UR ||
           t == QR_TYPE_ACCOUNT_UR || t == QR_TYPE_BYTES_UR;
}

static bool is_animated(qr_type_t t)
{
    return t == QR_TYPE_PSBT_SPECTER || t == QR_TYPE_PSBT_BBQR || t == QR_TYPE_WALLET_SPECTER;
}

void qr_decoder_init(qr_decoder_t *d, uint8_t *arena, size_t arena_cap)
{
    memset(d, 0, sizeof *d);
    d->arena = arena;
    d->arena_cap = arena_cap;
}

void qr_decoder_reset(qr_decoder_t *d)
{
    uint8_t *a = d->arena;
    const size_t cap = d->arena_cap;
    if (a != NULL) {
        wally_bzero(a, cap);
    }
    wally_bzero(d, sizeof *d);
    qr_decoder_init(d, a, cap);
}

/* ------------------------------------------------------------- single frames */

static bool keep_frame(qr_decoder_t *d, const uint8_t *data, size_t len)
{
    if (len > d->arena_cap) {
        return false;
    }
    memcpy(d->arena, data, len);
    d->frame_len = len;
    return true;
}

/* The code points of `s`, four at a time, as SeedQrDecoder slices them. */
static size_t cp_len(const char *s, size_t len)
{
    size_t n = 0;
    for (size_t i = 0; i < len; i++) {
        if (((uint8_t)s[i] & 0xC0) != 0x80) {
            n++;
        }
    }
    return n;
}

static const char *cp_skip(const char *s, const char *end, size_t count)
{
    while (s < end && count > 0) {
        s++;
        while (s < end && ((uint8_t)*s & 0xC0) == 0x80) {
            s++;
        }
        count--;
    }
    return s;
}

/* SeedQrDecoder.add, SEED__SEEDQR: int() of every four characters, as an
   index into the word list - Python's negative indexes included. */
static qr_status_t add_seedqr(qr_decoder_t *d, const char *s, size_t len)
{
    const size_t words = cp_len(s, len) / 4;
    const char *end = s + len, *p = s;
    d->n_words = 0;
    for (size_t i = 0; i < words; i++) {
        const char *q = cp_skip(p, end, 4);
        long v;
        if (!py_int(p, (size_t)(q - p), &v) || v < -BIP39_WORDS || v >= BIP39_WORDS) {
            d->n_words = 0;
            return QR_STATUS_INVALID;
        }
        if (i < SEED_MAX_WORDS) {
            d->words[i] = (uint16_t)(v < 0 ? v + BIP39_WORDS : v);
        }
        p = q;
    }
    if (words != 12 && words != 24) {
        d->n_words = 0;
        return QR_STATUS_INVALID;
    }
    d->n_words = words;
    d->complete = true;
    return QR_STATUS_COMPLETE;
}

static qr_status_t add_compact(qr_decoder_t *d, const uint8_t *data, size_t len)
{
    size_t n = 0;
    if (!mnemonic_from_entropy(data, len, d->words, &n)) {
        return QR_STATUS_INVALID;
    }
    d->n_words = n;
    d->complete = true;
    return QR_STATUS_COMPLETE;
}

/* SEED__MNEMONIC and SEED__FOUR_LETTER_MNEMONIC: split on single spaces,
   map each word, and keep the phrase only if it is a valid BIP-39 mnemonic of
   12 or 24 words. */
static qr_status_t add_mnemonic(qr_decoder_t *d, const char *s, size_t len, bool four)
{
    uint16_t idx[SEED_MAX_WORDS];
    size_t n = 0, start = 0;
    py_strip(&s, &len);
    for (size_t i = 0; i <= len; i++) {
        if (i < len && s[i] != ' ') {
            continue;
        }
        const size_t tl = i - start;
        char w[9];
        int found = -1;
        if (tl > 0 && tl <= 8) {
            memcpy(w, s + start, tl);
            w[tl] = '\0';
            if (!four) {
                found = bip39_word_index(w);
            } else {
                for (int k = 0; k < BIP39_WORDS && found < 0; k++) {
                    const char *word = BIP39_WORDLIST[k];
                    const size_t wl = strlen(word) < 4 ? strlen(word) : 4;
                    if (wl == tl && memcmp(word, w, tl) == 0) {
                        found = k;
                    }
                }
            }
        }
        if (found < 0 || n >= SEED_MAX_WORDS) {
            return QR_STATUS_INVALID;
        }
        idx[n++] = (uint16_t)found;
        start = i + 1;
    }
    if (!mnemonic_checksum_ok(idx, n)) {
        return QR_STATUS_INVALID;
    }
    memcpy(d->words, idx, n * sizeof idx[0]);
    d->n_words = n;
    d->complete = true;
    wally_bzero(idx, sizeof idx);
    return QR_STATUS_COMPLETE;
}

/* BitcoinAddressQrDecoder.add: the last ":" field, then
   `^((bc1q|tb1q|bcrt1q|bc1p|tb1p|bcrt1p|[123]|[mn])[a-zA-HJ-NP-Z0-9]{25,64})`,
   IGNORECASE, matched as a prefix. */
static qr_status_t add_address(qr_decoder_t *d, const char *s, size_t len)
{
    const char *f = s;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == ':') {
            f = s + i + 1;
        }
    }
    const size_t fl = (size_t)(s + len - f);

    static const struct { const char *p; btc_script_t script; btc_net_t net; } PREFIXES[] = {
        { "bc1q", BTC_SCRIPT_P2WPKH, BTC_NET_MAIN },
        { "tb1q", BTC_SCRIPT_P2WPKH, BTC_NET_TEST },
        { "bcrt1q", BTC_SCRIPT_P2WPKH, BTC_NET_REGTEST },
        { "bc1p", BTC_SCRIPT_P2TR, BTC_NET_MAIN },
        { "tb1p", BTC_SCRIPT_P2TR, BTC_NET_TEST },
        { "bcrt1p", BTC_SCRIPT_P2TR, BTC_NET_REGTEST },
        { "1", BTC_SCRIPT_P2PKH, BTC_NET_MAIN },
        { "2", BTC_SCRIPT_P2SH_P2WPKH, BTC_NET_TEST },
        { "3", BTC_SCRIPT_P2SH_P2WPKH, BTC_NET_MAIN },
        { "m", BTC_SCRIPT_P2PKH, BTC_NET_TEST },
        { "n", BTC_SCRIPT_P2PKH, BTC_NET_TEST },
    };
    for (size_t k = 0; k < sizeof PREFIXES / sizeof PREFIXES[0]; k++) {
        const size_t pl = strlen(PREFIXES[k].p);
        if (!py_istartswith(f, fl, PREFIXES[k].p)) {
            continue;
        }
        size_t body = 0;
        while (pl + body < fl && body < 64) {
            const char c = f[pl + body];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
                break;
            }
            body++;
        }
        if (body < 25) {
            continue;   /* the regex backtracks to the next alternative */
        }
        const size_t n = pl + body;
        if (n >= sizeof d->address) {
            return QR_STATUS_INVALID;
        }
        const bool fold = PREFIXES[k].script == BTC_SCRIPT_P2WPKH ||
                          PREFIXES[k].script == BTC_SCRIPT_P2TR;
        for (size_t i = 0; i < n; i++) {
            const char c = f[i];
            d->address[i] = (fold && c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
        }
        d->address[n] = '\0';
        d->address_script = PREFIXES[k].script;
        d->address_net = PREFIXES[k].net;
        d->complete = true;
        return QR_STATUS_COMPLETE;
    }
    return QR_STATUS_INVALID;
}

/* SignMessageQrDecoder.add: "signmessage {path} ascii:{message}". The
   message is everything between the first "ascii:" and the next one, as
   segment.split("ascii:")[1] has it. */
static qr_status_t add_sign_message(qr_decoder_t *d, const char *s, size_t len)
{
    const char *f[3] = { 0 };
    size_t fl[3] = { 0 }, pos = 0;
    for (int i = 0; i < 3; i++) {
        if (!py_split_next(s, len, &pos, &f[i], &fl[i])) {
            return QR_STATUS_ERROR;   /* parts[1] or parts[2]: IndexError */
        }
    }
    /* fmt = parts[2].split(":")[0] */
    size_t fmt_len = 0;
    while (fmt_len < fl[2] && f[2][fmt_len] != ':') {
        fmt_len++;
    }
    /* segment.split(fmt + ":")[1] */
    const char *msg = NULL, *msg_end = s + len;
    for (size_t i = 0; i + fmt_len + 1 <= len; i++) {
        if (memcmp(s + i, f[2], fmt_len) == 0 && s[i + fmt_len] == ':') {
            if (msg == NULL) {
                msg = s + i + fmt_len + 1;
                i += fmt_len;
            } else {
                msg_end = s + i;
                break;
            }
        }
    }
    if (msg == NULL) {
        return QR_STATUS_ERROR;       /* [1] of a one-element split */
    }
    if (fmt_len != 5 || memcmp(f[2], "ascii", 5) != 0) {
        return QR_STATUS_INVALID;
    }

    /* The path, with every "h" made "'", after the frame in the arena. */
    if (d->frame_len + fl[1] > d->arena_cap) {
        return QR_STATUS_ERROR;
    }
    char *path = (char *)d->arena + d->frame_len;
    for (size_t i = 0; i < fl[1]; i++) {
        path[i] = f[1][i] == 'h' ? '\'' : f[1][i];
    }
    d->msg_path_off = d->frame_len;
    d->msg_path_len = fl[1];
    d->msg_off = (size_t)(msg - s);
    d->msg_len = (size_t)(msg_end - msg);
    d->complete = true;
    return QR_STATUS_COMPLETE;
}

/* ------------------------------------------------------------- wallets */

/* SpecterWalletQrDecoder's JSON: json.loads() of the joined segments, read
   in place, segment by segment. Strict, as CPython's scanner is; NaN and
   Infinity are taken, as it takes them. Nesting deeper than JSON_DEPTH is
   refused (CPython would go on to its recursion limit). */
#define JSON_DEPTH 64

typedef struct {
    const qr_decoder_t *d;
    uint32_t seg;
    size_t off;
} jreader_t;

/* Where a string's decoded UTF-8 goes: kept while it fits, and watched for
   "descriptor" in it. */
typedef struct {
    char *buf;
    size_t cap, n;
    bool overflow, lone_surrogate;
    int match;                      /* how much of "descriptor" just went by */
    bool found;
} jsink_t;

static const char DESCRIPTOR_KEY[] = "descriptor";

static int jr_peek(jreader_t *r)
{
    const qr_decoder_t *d = r->d;
    while (r->seg < d->total && r->off >= d->seg_len[r->seg]) {
        r->seg++;
        r->off = 0;
    }
    if (r->seg >= d->total) {
        return -1;
    }
    return (uint8_t)d->text[d->seg_off[r->seg] - 1 + r->off];
}

static void jr_next(jreader_t *r)
{
    if (jr_peek(r) >= 0) {
        r->off++;
    }
}

static void jr_ws(jreader_t *r)
{
    for (int c = jr_peek(r); c == ' ' || c == '\t' || c == '\n' || c == '\r'; c = jr_peek(r)) {
        jr_next(r);
    }
}

static bool jr_lit(jreader_t *r, const char *lit)
{
    for (; *lit; lit++) {
        if (jr_peek(r) != (uint8_t)*lit) {
            return false;
        }
        jr_next(r);
    }
    return true;
}

static void sink_put(jsink_t *k, uint8_t b)
{
    if (k == NULL) {
        return;
    }
    if (k->buf != NULL) {
        if (k->n + 1 < k->cap) {
            k->buf[k->n++] = (char)b;
            k->buf[k->n] = '\0';
        } else {
            k->overflow = true;
        }
    }
    /* "descriptor" has no repeated prefix: a mismatch restarts at its 'd'. */
    if (b == (uint8_t)DESCRIPTOR_KEY[k->match]) {
        if (++k->match == (int)sizeof DESCRIPTOR_KEY - 1) {
            k->found = true;
            k->match = 0;
        }
    } else {
        k->match = b == 'd' ? 1 : 0;
    }
}

static void sink_cp(jsink_t *k, uint32_t cp)
{
    if (cp < 0x80) {
        sink_put(k, (uint8_t)cp);
    } else if (cp < 0x800) {
        sink_put(k, (uint8_t)(0xC0 | (cp >> 6)));
        sink_put(k, (uint8_t)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        sink_put(k, (uint8_t)(0xE0 | (cp >> 12)));
        sink_put(k, (uint8_t)(0x80 | ((cp >> 6) & 0x3F)));
        sink_put(k, (uint8_t)(0x80 | (cp & 0x3F)));
    } else {
        sink_put(k, (uint8_t)(0xF0 | (cp >> 18)));
        sink_put(k, (uint8_t)(0x80 | ((cp >> 12) & 0x3F)));
        sink_put(k, (uint8_t)(0x80 | ((cp >> 6) & 0x3F)));
        sink_put(k, (uint8_t)(0x80 | (cp & 0x3F)));
    }
}

static bool jr_hex4(jreader_t *r, uint32_t *v)
{
    *v = 0;
    for (int i = 0; i < 4; i++) {
        const int c = jr_peek(r);
        uint32_t h;
        if (c >= '0' && c <= '9') h = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') h = (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') h = (uint32_t)(c - 'A' + 10);
        else return false;
        *v = *v << 4 | h;
        jr_next(r);
    }
    return true;
}

/* A string, at its opening quote, decoded into `k` (which may be NULL). */
static bool json_string(jreader_t *r, jsink_t *k)
{
    jr_next(r);
    for (;;) {
        const int c = jr_peek(r);
        if (c < 0x20) {
            return false;           /* the end, or a control character */
        }
        jr_next(r);
        if (c == '"') {
            return true;
        }
        if (c != '\\') {
            sink_put(k, (uint8_t)c);
            continue;
        }
        const int e = jr_peek(r);
        jr_next(r);
        static const char ESC[] = "\"\\/bfnrt", VAL[] = "\"\\/\b\f\n\r\t";
        const char *p = e > 0 ? strchr(ESC, e) : NULL;
        if (p != NULL && *p) {
            sink_put(k, (uint8_t)VAL[p - ESC]);
            continue;
        }
        uint32_t cp;
        if (e != 'u' || !jr_hex4(r, &cp)) {
            return false;
        }
        if (cp >= 0xD800 && cp <= 0xDBFF) {
            /* A pair makes one code point; CPython keeps a lone half. */
            jreader_t save = *r;
            uint32_t lo;
            if (jr_lit(r, "\\u") && jr_hex4(r, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            } else {
                *r = save;
                if (k) k->lone_surrogate = true;
            }
        } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            if (k) k->lone_surrogate = true;
        }
        sink_cp(k, cp);
    }
}

/* -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][-+]?[0-9]+)? or -Infinity. A '.' or
   'e' not followed by digits is left over, and nothing valid follows a
   value with one of those, so it fails here instead. */
static bool json_number(jreader_t *r)
{
    if (jr_peek(r) == '-') {
        jr_next(r);
        if (jr_peek(r) == 'I') {
            return jr_lit(r, "Infinity");
        }
    }
    int c = jr_peek(r);
    if (c == '0') {
        jr_next(r);
    } else if (c >= '1' && c <= '9') {
        while ((c = jr_peek(r)) >= '0' && c <= '9') jr_next(r);
    } else {
        return false;
    }
    if (jr_peek(r) == '.') {
        jr_next(r);
        if ((c = jr_peek(r)) < '0' || c > '9') return false;
        while ((c = jr_peek(r)) >= '0' && c <= '9') jr_next(r);
    }
    if ((c = jr_peek(r)) == 'e' || c == 'E') {
        jr_next(r);
        if ((c = jr_peek(r)) == '+' || c == '-') jr_next(r);
        if ((c = jr_peek(r)) < '0' || c > '9') return false;
        while ((c = jr_peek(r)) >= '0' && c <= '9') jr_next(r);
    }
    return true;
}

typedef enum { SJ_INVALID, SJ_RAISES, SJ_NO_DESCRIPTOR, SJ_OK } specter_json_t;

/* What the top level found: the "descriptor" key or element, and the
   descriptor string itself (the last one, as a dict keeps it). */
typedef struct {
    bool found, desc_ok;
    char *out;
    size_t cap;
} jtop_t;

static bool json_value(jreader_t *r, int depth, jtop_t *top);

static bool json_container(jreader_t *r, int depth, jtop_t *top)
{
    const bool object = jr_peek(r) == '{';
    const int close = object ? '}' : ']';
    if (depth >= JSON_DEPTH) {
        return false;
    }
    jr_next(r);
    jr_ws(r);
    if (jr_peek(r) == close) {
        jr_next(r);
        return true;
    }
    for (;;) {
        bool is_key = false;
        if (object) {
            if (jr_peek(r) != '"') {
                return false;
            }
            char key[sizeof DESCRIPTOR_KEY];
            jsink_t k = { .buf = key, .cap = sizeof key };
            key[0] = '\0';
            if (!json_string(r, &k)) {
                return false;
            }
            is_key = top && !k.overflow && strcmp(key, DESCRIPTOR_KEY) == 0;
            jr_ws(r);
            if (jr_peek(r) != ':') {
                return false;
            }
            jr_next(r);
            jr_ws(r);
        }
        if (top && object && is_key) {
            /* data['descriptor'] */
            top->found = true;
            top->desc_ok = false;
            if (jr_peek(r) == '"') {
                jsink_t v = { .buf = top->out, .cap = top->cap };
                if (!json_string(r, &v)) {
                    return false;
                }
                top->desc_ok = top->out != NULL && !v.overflow && !v.lone_surrogate;
            } else if (!json_value(r, depth + 1, NULL)) {
                return false;
            }
        } else if (top && !object && jr_peek(r) == '"') {
            /* "descriptor" in a list: an element equal to it */
            char el[sizeof DESCRIPTOR_KEY];
            jsink_t v = { .buf = el, .cap = sizeof el };
            el[0] = '\0';
            if (!json_string(r, &v)) {
                return false;
            }
            if (!v.overflow && strcmp(el, DESCRIPTOR_KEY) == 0) {
                top->found = true;
            }
        } else if (!json_value(r, depth + 1, NULL)) {
            return false;
        }
        jr_ws(r);
        const int c = jr_peek(r);
        jr_next(r);
        if (c == close) {
            return true;
        }
        if (c != ',') {
            return false;
        }
        jr_ws(r);
    }
}

static bool json_value(jreader_t *r, int depth, jtop_t *top)
{
    const int c = jr_peek(r);
    switch (c) {
    case '{':
    case '[':
        return json_container(r, depth, top);
    case '"': {
        jsink_t k = { 0 };
        if (!json_string(r, &k)) {
            return false;
        }
        if (top) {
            top->found = k.found;   /* "descriptor" in a str: a substring */
        }
        return true;
    }
    case 'n': return jr_lit(r, "null");
    case 't': return jr_lit(r, "true");
    case 'f': return jr_lit(r, "false");
    case 'N': return jr_lit(r, "NaN");
    case 'I': return jr_lit(r, "Infinity");
    default:  return json_number(r);
    }
}

/* is_valid, and get_wallet_descriptor() into `out` (if not NULL). A list
   or a string that holds "descriptor" is valid, but data['descriptor']
   then raises; so does a descriptor that is not a string, and one that
   CPython would not encode (a lone surrogate) or that does not fit. */
static specter_json_t specter_json(const qr_decoder_t *d, char *out, size_t cap)
{
    jreader_t r = { .d = d };
    jtop_t top = { .out = out, .cap = cap };
    jr_ws(&r);
    const int first = jr_peek(&r);
    if (!json_value(&r, 0, &top)) {
        return SJ_INVALID;
    }
    jr_ws(&r);
    if (jr_peek(&r) >= 0) {
        return SJ_INVALID;          /* "Extra data" */
    }
    if (first != '{' && first != '[' && first != '"') {
        return SJ_RAISES;           /* `in` a number, a bool or None */
    }
    if (!top.found) {
        return SJ_INVALID;
    }
    return first == '{' && top.desc_ok ? SJ_OK : SJ_NO_DESCRIPTOR;
}

/* GenericWalletQrDecoder and MultiSigConfigFileQRDecoder: the descriptor,
   as scanned or made from the setup file, must parse. A setup file that
   multisig_setup_file_to_descriptor() refuses raises. */
static qr_status_t add_wallet(qr_decoder_t *d, const char *s, size_t len)
{
    bool ok;
    if (d->type == QR_TYPE_WALLET_CONFIGFILE) {
        char *desc = malloc(DESC_TEXT_MAX);
        if (desc == NULL) {
            return QR_STATUS_ERROR;
        }
        if (!desc_from_setup_file(s, len, desc, DESC_TEXT_MAX)) {
            free(desc);
            return QR_STATUS_ERROR;
        }
        ok = desc_check(desc, strlen(desc));
        free(desc);
    } else {
        ok = desc_check(s, len);
    }
    if (!ok) {
        return QR_STATUS_INVALID;
    }
    d->complete = true;
    return QR_STATUS_COMPLETE;
}

/* ------------------------------------------------------------- animated */

/* Specter "pXofY data" or BBQr "B$<enc>P<total><index>data": the total, the
   1-based part number, and the text kept. False where SeedSigner raises. */
static bool parse_animated(const qr_decoder_t *d, const char *s, size_t len,
                           long *total, long *num, const char **body, size_t *body_len)
{
    if (d->type == QR_TYPE_PSBT_BBQR) {
        /* int(segment[4:6], 36) and int(segment[6:8], 36) + 1; the
           detector has checked those four characters. */
        long v[2];
        for (int k = 0; k < 2; k++) {
            v[k] = 0;
            for (int j = 0; j < 2; j++) {
                const char c = s[4 + 2 * k + j];
                v[k] = v[k] * 36 + ((c >= '0' && c <= '9') ? c - '0' : c - 'A' + 10);
            }
        }
        *total = v[0];
        *num = v[1] + 1;
        *body = s + 8;
        *body_len = len - 8;
        py_strip(body, body_len);
        return true;
    }

    if (d->type == QR_TYPE_WALLET_SPECTER) {
        /* SpecterWalletQrDecoder: the numbers from `^p(\d+)of(\d+) `, or
           part 1 of 1 without them; the data is `(.+$)` after them, or the
           whole frame where that does not match (a line break inside). */
        const char *x, *y;
        size_t xn, yn;
        const size_t start = qr_match_pxofy(s, len, &x, &xn, &y, &yn);
        *num = *total = 1;
        *body = s;
        *body_len = len;
        if (start == 0) {
            return true;
        }
        if (!py_int(x, xn, num) || !py_int(y, yn, total)) {
            return false;
        }
        const size_t end = qr_dollar_end(s, len);
        if (end > start && memchr(s + start, '\n', end - start) == NULL) {
            *body = s + start;
            *body_len = end - start;
        }
        return true;
    }

    /* Specter: the numbers from `^p(\d+)of(\d+) `, the data from
       segment.split(" ")[-1].strip(). */
    const char *x = s + 1, *y;
    size_t i = 1;
    while (i < len && s[i] >= '0' && s[i] <= '9') i++;
    const size_t xn = i - 1;
    i += 2;
    y = s + i;
    while (i < len && s[i] >= '0' && s[i] <= '9') i++;
    const size_t yn = (size_t)(s + i - y);
    if (!py_int(x, xn, num) || !py_int(y, yn, total)) {
        return false;
    }
    size_t last = 0;
    for (size_t k = 0; k < len; k++) {
        if (s[k] == ' ') {
            last = k + 1;
        }
    }
    *body = s + last;
    *body_len = len - last;
    py_strip(body, body_len);
    return true;
}

static qr_status_t add_animated(qr_decoder_t *d, const char *s, size_t len)
{
    long total, num;
    const char *body;
    size_t body_len;
    if (!parse_animated(d, s, len, &total, &num, &body, &body_len)) {
        return QR_STATUS_ERROR;
    }

    if (d->seg_off == NULL) {
        /* segments = [None] * total */
        if (total > QR_SEGMENTS_MAX) {
            return QR_STATUS_ERROR;
        }
        const size_t table = (size_t)total * 2 * sizeof(uint32_t);
        if (table > d->arena_cap) {
            return QR_STATUS_ERROR;
        }
        memset(d->arena, 0, table);
        d->seg_off = (uint32_t *)(void *)d->arena;
        d->seg_len = d->seg_off + total;
        d->text = d->arena + table;
        d->text_cap = d->arena_cap - table;
        d->text_used = 0;
        d->total = (uint32_t)total;
    } else if ((uint32_t)total != d->total) {
        return QR_STATUS_ERROR;       /* "Segment total changed unexpectedly" */
    }

    /* segments[num - 1], where -1 is the last one, as a Python list has it */
    long index = num - 1;
    if (index < 0) {
        index += (long)d->total;
    }
    if (index < 0 || index >= (long)d->total) {
        return QR_STATUS_ERROR;       /* IndexError */
    }

    /* A held segment has a length of at least 1: offsets are stored + 1. */
    if (d->seg_off[index] != 0) {
        return QR_STATUS_PART_EXISTING;
    }
    if (body_len > d->text_cap - d->text_used) {
        return QR_STATUS_ERROR;
    }
    memcpy(d->text + d->text_used, body, body_len);
    d->seg_off[index] = (uint32_t)d->text_used + 1;
    d->seg_len[index] = (uint32_t)body_len;
    d->text_used += body_len;
    if (d->type == QR_TYPE_PSBT_BBQR) {
        d->bbqr_encoding = s[2];
    }
    d->collected++;
    if (d->collected == d->total) {
        if (d->type == QR_TYPE_WALLET_SPECTER) {
            /* is_valid: the joined segments are JSON with a "descriptor". */
            const specter_json_t r = specter_json(d, NULL, 0);
            if (r == SJ_INVALID) {
                return QR_STATUS_INVALID;
            }
            if (r == SJ_RAISES) {
                return QR_STATUS_ERROR;
            }
        }
        d->complete = true;
        return QR_STATUS_COMPLETE;
    }
    return QR_STATUS_PART_COMPLETE;
}

/* ------------------------------------------------------------- add */

qr_status_t qr_decoder_add(qr_decoder_t *d, const uint8_t *data, size_t len)
{
    if (data == NULL) {
        return QR_STATUS_FALSE;
    }
    const qr_type_t t = qr_detect(data, len);
    if (d->type == QR_TYPE_NONE) {
        d->type = t;
        if (is_ur(t)) {
            ur_decoder_init(&d->ur, d->arena, d->arena_cap);
        }
    } else if (d->type != t) {
        return QR_STATUS_TYPE_CHANGED;
    }

    const char *s = (const char *)data;
    switch (t) {
    case QR_TYPE_NONE:
    case QR_TYPE_COUNT:
    case QR_TYPE_INVALID:
        return QR_STATUS_INVALID;


    case QR_TYPE_PSBT_UR2:
    case QR_TYPE_OUTPUT_UR:
    case QR_TYPE_ACCOUNT_UR:
    case QR_TYPE_BYTES_UR: {
        const UrPartResult r = ur_decoder_receive(&d->ur, s, len);
        if (ur_decoder_complete(&d->ur)) {
            if (ur_decoder_failed(&d->ur)) {
                return QR_STATUS_INVALID;
            }
            d->complete = true;
            return QR_STATUS_COMPLETE;
        }
        return r == UR_PART_ACCEPTED ? QR_STATUS_PART_COMPLETE : QR_STATUS_PART_EXISTING;
    }

    case QR_TYPE_PSBT_SPECTER:
    case QR_TYPE_PSBT_BBQR:
    case QR_TYPE_WALLET_SPECTER:
        return add_animated(d, s, len);

    default:
        break;
    }

    /* A seed is decoded straight from the caller's frame into the decoder's
       own words, and never copied into the arena, so the arena may sit in
       PSRAM (secmem.h). */
    switch (t) {
    case QR_TYPE_SEED_COMPACTSEEDQR:
        return add_compact(d, data, len);
    case QR_TYPE_SEED_SEEDQR:
        return add_seedqr(d, s, len);
    case QR_TYPE_SEED_MNEMONIC:
        return add_mnemonic(d, s, len, false);
    case QR_TYPE_SEED_FOUR_LETTER_MNEMONIC:
        return add_mnemonic(d, s, len, true);
    default:
        break;
    }

    /* Every other type is one frame, kept whole in the arena. Another frame
       of the same type replaces it, as the reference's add() does. */
    if (!keep_frame(d, data, len)) {
        return QR_STATUS_ERROR;
    }
    s = (const char *)d->arena;
    switch (t) {
    case QR_TYPE_BITCOIN_ADDRESS:
        return add_address(d, s, len);
    case QR_TYPE_SIGN_MESSAGE:
        return add_sign_message(d, s, len);
    case QR_TYPE_WALLET_GENERIC:
    case QR_TYPE_WALLET_CONFIGFILE:
        return add_wallet(d, s, len);
    case QR_TYPE_PSBT_BASE64:
    case QR_TYPE_PSBT_BASE43:
    case QR_TYPE_SETTINGS:
        /* The detector already parsed it as SeedSigner's add() would. */
        d->complete = true;
        return QR_STATUS_COMPLETE;
    default:
        return QR_STATUS_INVALID;
    }
}

int qr_decoder_percent(const qr_decoder_t *d, bool weight_mixed)
{
    if (is_ur(d->type)) {
        return (int)(ur_decoder_progress(&d->ur, weight_mixed) * 100.0);
    }
    if (d->type == QR_TYPE_WALLET_SPECTER) {
        /* Not among the animated types get_percent_complete() counts: all
           or nothing for one part, 0 for more. */
        return d->total == 1 && d->complete ? 100 : 0;
    }
    if (is_animated(d->type)) {
        if (d->total == 0) {
            return 0;
        }
        return (int)(((double)d->collected / (double)d->total) * 100.0);
    }
    return d->complete ? 100 : 0;
}

/* ------------------------------------------------------------- getters */

/* The bstr a UR carries, inside the crypto-psbt tag (310) if it has one. */
static bool ur_bstr(const qr_decoder_t *d, bool allow_psbt_tag, const uint8_t **out, size_t *len)
{
    size_t n;
    const uint8_t *c = ur_decoder_message(&d->ur, &n);
    if (c == NULL) {
        return false;
    }
    if (allow_psbt_tag && n >= 3 && c[0] == 0xD9 && c[1] == 0x01 && c[2] == 0x36) {
        c += 3;
        n -= 3;
    }
    if (n == 0 || (c[0] >> 5) != 2) {
        return false;
    }
    /* The head of a byte string; anything after the item is ignored, as the
       reference's decoder ignores it. */
    const uint8_t info = c[0] & 0x1F;
    size_t hdr = 1, blen;
    if (info < 24) {
        blen = info;
    } else if (info == 24 && n >= 2) {
        blen = c[1]; hdr = 2;
    } else if (info == 25 && n >= 3) {
        blen = ((size_t)c[1] << 8) | c[2]; hdr = 3;
    } else if (info == 26 && n >= 5) {
        blen = ((size_t)c[1] << 24) | ((size_t)c[2] << 16) | ((size_t)c[3] << 8) | c[4];
        hdr = 5;
    } else {
        return false;
    }
    if (blen > n - hdr) {
        return false;
    }
    *out = c + hdr;
    *len = blen;
    return true;
}

const uint8_t *qr_decoder_ur_cbor(const qr_decoder_t *d, size_t *len)
{
    if (!d->complete || !is_ur(d->type)) {
        return NULL;
    }
    return ur_decoder_message(&d->ur, len);
}

static bool put(uint8_t *out, size_t cap, size_t *len, const uint8_t *p, size_t n)
{
    if (n > cap - *len) {
        return false;
    }
    memcpy(out + *len, p, n);
    *len += n;
    return true;
}

bool qr_decoder_psbt(const qr_decoder_t *d, uint8_t *out, size_t cap, size_t *len)
{
    if (!d->complete || !qr_is_psbt(d->type)) {
        return false;
    }
    *len = 0;
    const char *frame = (const char *)d->arena;

    switch (d->type) {
    case QR_TYPE_PSBT_UR2: {
        const uint8_t *b;
        size_t n;
        return ur_bstr(d, true, &b, &n) && put(out, cap, len, b, n);
    }
    case QR_TYPE_PSBT_BASE64: {
        const long n = py_b64decode(frame, d->frame_len, out, cap);
        if (n < 0) return false;
        *len = (size_t)n;
        return true;
    }
    case QR_TYPE_PSBT_BASE43: {
        const long n = py_base43_decode(frame, d->frame_len, out, cap);
        if (n < 0) return false;
        *len = (size_t)n;
        return true;
    }
    case QR_TYPE_PSBT_SPECTER: {
        /* "".join(segments), then only if it is canonical base64. The
           joined text goes in the output first and is decoded in place:
           base64 never grows as it decodes. */
        for (uint32_t i = 0; i < d->total; i++) {
            if (!put(out, cap, len, d->text + d->seg_off[i] - 1, d->seg_len[i])) {
                return false;
            }
        }
        if (!py_is_base64((const char *)out, *len)) {
            return false;
        }
        const long n = py_b64decode((const char *)out, *len, out, cap);
        if (n < 0) return false;
        *len = (size_t)n;
        return true;
    }
    case QR_TYPE_PSBT_BBQR: {
        for (uint32_t i = 0; i < d->total; i++) {
            const char *seg = (const char *)d->text + d->seg_off[i] - 1;
            long n;
            if (d->bbqr_encoding == 'H') {
                n = py_fromhex(seg, d->seg_len[i], out + *len, cap - *len);
            } else {
                n = py_b32decode_padded(seg, d->seg_len[i], out + *len, cap - *len);
            }
            if (n < 0) {
                return false;
            }
            *len += (size_t)n;
        }
        if (d->bbqr_encoding != 'Z') {
            return true;
        }
        /* Raw deflate. The compressed bytes move to the end of `out` so
           the output can grow from the start without overtaking them. */
        const size_t zlen = *len;
        if (zlen == 0 || zlen > cap) {
            return false;
        }
        memmove(out + cap - zlen, out, zlen);
        unsigned int dlen = (unsigned int)(cap - zlen);
        if (tinf_uncompress(out, &dlen, out + cap - zlen, (unsigned int)zlen) != TINF_OK) {
            return false;
        }
        *len = dlen;
        return true;
    }
    default:
        return false;
    }
}

size_t qr_decoder_psbt_max(const qr_decoder_t *d)
{
    if (!d->complete || !qr_is_psbt(d->type)) {
        return 0;
    }
    switch (d->type) {
    case QR_TYPE_PSBT_UR2: {
        size_t n = 0;
        return ur_decoder_message(&d->ur, &n) ? n : 0;
    }
    case QR_TYPE_PSBT_BASE64:
        return d->frame_len / 4 * 3 + 3;
    case QR_TYPE_PSBT_BASE43:
        return d->frame_len + 1;    /* base43 always shrinks; the leading-zero quirk adds one */
    case QR_TYPE_PSBT_SPECTER:
        /* The joined text, which is decoded in place in the caller's buffer. */
        return d->text_used + 4;
    case QR_TYPE_PSBT_BBQR:
        if (d->bbqr_encoding == 'H') {
            return d->text_used / 2 + 1;
        }
        if (d->bbqr_encoding == '2') {
            return d->text_used * 5 / 8 + 5;
        }
        /* Inflated: PSBTs compress two to three times; the compressed bytes
           also sit at the end of the buffer while inflating. */
        return (d->text_used * 5 / 8 + 5) * 17;
    default:
        return 0;
    }
}

size_t qr_decoder_seed(const qr_decoder_t *d, uint16_t words[SEED_MAX_WORDS])
{
    if (!d->complete || !qr_is_seed(d->type)) {
        return 0;
    }
    memcpy(words, d->words, d->n_words * sizeof words[0]);
    return d->n_words;
}

const char *qr_decoder_address(const qr_decoder_t *d, btc_script_t *script, btc_net_t *net)
{
    if (!d->complete || d->type != QR_TYPE_BITCOIN_ADDRESS) {
        return NULL;
    }
    if (script) *script = d->address_script;
    if (net) *net = d->address_net;
    return d->address;
}

bool qr_decoder_settings(const qr_decoder_t *d, const char **text, size_t *len)
{
    if (!d->complete || d->type != QR_TYPE_SETTINGS) {
        return false;
    }
    *text = (const char *)d->arena;
    *len = d->frame_len;
    return true;
}

bool qr_decoder_sign_message(const qr_decoder_t *d, const char **path, size_t *path_len,
                             const char **msg, size_t *msg_len)
{
    if (!d->complete || d->type != QR_TYPE_SIGN_MESSAGE) {
        return false;
    }
    *path = (const char *)d->arena + d->msg_path_off;
    *path_len = d->msg_path_len;
    *msg = (const char *)d->arena + d->msg_off;
    *msg_len = d->msg_len;
    return true;
}

bool qr_is_wallet_descriptor(const qr_decoder_t *d)
{
    switch (d->type) {
    case QR_TYPE_WALLET_SPECTER:
    case QR_TYPE_WALLET_GENERIC:
    case QR_TYPE_WALLET_CONFIGFILE:
    case QR_TYPE_OUTPUT_UR:
        return true;
    case QR_TYPE_BYTES_UR: {
        /* A setup file: "policy:", "format:" and "derivation:" in it. */
        const uint8_t *b;
        size_t n;
        if (!d->complete || !ur_bstr(d, false, &b, &n) || !py_utf8_valid(b, n)) {
            return false;
        }
        const char *t = (const char *)b;
        return py_contains(t, n, "policy:", true) && py_contains(t, n, "format:", true) &&
               py_contains(t, n, "derivation:", true);
    }
    default:
        return false;
    }
}

bool qr_decoder_descriptor(const qr_decoder_t *d, char *out, size_t cap)
{
    if (!d->complete || cap == 0) {
        return false;
    }
    out[0] = '\0';
    const char *frame = (const char *)d->arena;
    switch (d->type) {
    case QR_TYPE_WALLET_SPECTER:
        return specter_json(d, out, cap) == SJ_OK;
    case QR_TYPE_WALLET_GENERIC:
        if (d->frame_len >= cap || memchr(frame, '\0', d->frame_len) != NULL) {
            return false;
        }
        memcpy(out, frame, d->frame_len);
        out[d->frame_len] = '\0';
        return true;
    case QR_TYPE_WALLET_CONFIGFILE:
        return desc_from_setup_file(frame, d->frame_len, out, cap);
    case QR_TYPE_BYTES_UR: {
        const uint8_t *b;
        size_t n;
        return qr_is_wallet_descriptor(d) && ur_bstr(d, false, &b, &n) &&
               desc_from_setup_file((const char *)b, n, out, cap);
    }
    default:
        return false;   /* crypto-output: not decoded (qr-decode.h) */
    }
}
