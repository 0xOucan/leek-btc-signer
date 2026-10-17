/*
 * detect_segment_type(), SeedSigner's answer to "what is this QR code".
 * See qr-decode.h. The checks run in SeedSigner's order, because the order
 * is part of the answer: a base64 PSBT that happens to contain 48 digits in
 * a row is a PSBT, not a SeedQR, only because the PSBT check comes first.
 */

#include "qr-decode.h"

#include <string.h>

#include "pytext.h"
#include "wally_psbt.h"

/* The largest payload any QR code holds (version 40, numeric). A longer
   frame cannot have come from a camera, and the scratch below is sized by
   it. */
#define QR_FRAME_MAX 7089

/* Decoded base64 or base43 of one frame, and the frame with spaces and
   newlines removed. Static rather than on the UI task's stack; the decoder
   runs on one task. */
static uint8_t s_scratch[QR_FRAME_MAX];

static const char *const NAMES[QR_TYPE_COUNT] = {
    [QR_TYPE_NONE] = "none",
    [QR_TYPE_INVALID] = "invalid",
    [QR_TYPE_PSBT_UR2] = "psbt__ur2",
    [QR_TYPE_PSBT_SPECTER] = "psbt__specter",
    [QR_TYPE_PSBT_BASE64] = "psbt__base64",
    [QR_TYPE_PSBT_BASE43] = "psbt__base43",
    [QR_TYPE_PSBT_BBQR] = "psbt__bbqr",
    [QR_TYPE_SEED_SEEDQR] = "seed__seedqr",
    [QR_TYPE_SEED_COMPACTSEEDQR] = "seed__compactseedqr",
    [QR_TYPE_SEED_MNEMONIC] = "seed__mnemonic",
    [QR_TYPE_SEED_FOUR_LETTER_MNEMONIC] = "seed__four_letter_mnemonic",
    [QR_TYPE_SETTINGS] = "settings",
    [QR_TYPE_BITCOIN_ADDRESS] = "bitcoin_address",
    [QR_TYPE_SIGN_MESSAGE] = "sign_message",
    [QR_TYPE_WALLET_SPECTER] = "wallet__specter",
    [QR_TYPE_WALLET_GENERIC] = "wallet__generic",
    [QR_TYPE_WALLET_CONFIGFILE] = "wallet__configfile",
    [QR_TYPE_OUTPUT_UR] = "output__ur",
    [QR_TYPE_ACCOUNT_UR] = "account__ur",
    [QR_TYPE_BYTES_UR] = "bytes__ur",
};

const char *qr_type_name(qr_type_t t)
{
    return (t >= 0 && t < QR_TYPE_COUNT) ? NAMES[t] : "?";
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool is_alnum(char c)
{
    return is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* Python's `$`: the end, or just before a newline that ends the string. */
static size_t dollar_end(const char *s, size_t len)
{
    return (len > 0 && s[len - 1] == '\n') ? len - 1 : len;
}

/* `^p(\d+)of(\d+) `, IGNORECASE. Where the match ends, or 0. The numbers
   are given back as text. */
static size_t match_pxofy(const char *s, size_t len,
                          const char **x, size_t *xn, const char **y, size_t *yn)
{
    size_t i = 0;
    if (len < 1 || (s[0] != 'p' && s[0] != 'P')) {
        return 0;
    }
    i = 1;
    *x = s + i;
    while (i < len && is_digit(s[i])) i++;
    *xn = (size_t)(s + i - *x);
    if (*xn == 0 || i + 2 > len || (s[i] != 'o' && s[i] != 'O') || (s[i + 1] != 'f' && s[i + 1] != 'F')) {
        return 0;
    }
    i += 2;
    *y = s + i;
    while (i < len && is_digit(s[i])) i++;
    *yn = (size_t)(s + i - *y);
    if (*yn == 0 || i >= len || s[i] != ' ') {
        return 0;
    }
    return i + 1;
}

/* `^p(\d+)of(\d+) ([A-Za-z0-9+\/=]+$)` */
static bool is_specter_psbt(const char *s, size_t len)
{
    const char *x, *y;
    size_t xn, yn;
    const size_t start = match_pxofy(s, len, &x, &xn, &y, &yn);
    if (start == 0) {
        return false;
    }
    const size_t end = dollar_end(s, len);
    if (end <= start) {
        return false;
    }
    for (size_t i = start; i < end; i++) {
        const char c = s[i];
        if (!is_alnum(c) && c != '+' && c != '/' && c != '=') {
            return false;
        }
    }
    return true;
}

static bool psbt_parses(const uint8_t *bytes, size_t len)
{
    struct wally_psbt *psbt = NULL;
    if (wally_psbt_from_bytes(bytes, len, 0, &psbt) != WALLY_OK) {
        return false;
    }
    wally_psbt_free(psbt);
    return true;
}

static bool is_base64_psbt(const char *s, size_t len)
{
    if (!py_is_base64(s, len)) {
        return false;
    }
    const long n = py_b64decode(s, len, s_scratch, sizeof s_scratch);
    return n >= 0 && psbt_parses(s_scratch, (size_t)n);
}

static bool is_base43_psbt(const char *s, size_t len)
{
    const long n = py_base43_decode(s, len, s_scratch, sizeof s_scratch);
    return n >= 0 && psbt_parses(s_scratch, (size_t)n);
}

/* `^B\$[2HZ]P[0-9A-Z]{4}` */
static bool is_bbqr(const char *s, size_t len)
{
    if (len < 8 || s[0] != 'B' || s[1] != '$' ||
        (s[2] != '2' && s[2] != 'H' && s[2] != 'Z') || s[3] != 'P') {
        return false;
    }
    for (int i = 4; i < 8; i++) {
        if (!is_digit(s[i]) && !(s[i] >= 'A' && s[i] <= 'Z')) {
            return false;
        }
    }
    return true;
}

/* `^\{\"label\".*\"descriptor\"\:.*`, IGNORECASE, on the frame with every
   "\n" and " " removed. `.` does not cross a newline, but they are gone. */
static bool is_specter_wallet_json(const char *s, size_t len)
{
    size_t n = 0;
    for (size_t i = 0; i < len && n < sizeof s_scratch; i++) {
        if (s[i] != '\n' && s[i] != ' ') {
            s_scratch[n++] = (uint8_t)s[i];
        }
    }
    const char *c = (const char *)s_scratch;
    return py_istartswith(c, n, "{\"label\"") &&
           py_contains(c + 8, n - 8, "\"descriptor\":", true);
}

/* `\d{48,96}`, searched: a run of 48 digits anywhere. */
static bool has_48_digits(const char *s, size_t len)
{
    size_t run = 0;
    for (size_t i = 0; i < len; i++) {
        run = is_digit(s[i]) ? run + 1 : 0;
        if (run >= 48) {
            return true;
        }
    }
    return false;
}

/* is_bitcoin_address(): "bitcoin:" anything, or
   `^((bc1|tb1|bcr|[123]|[mn])[a-zA-HJ-NP-Z0-9]{25,62})$`, IGNORECASE, under
   which the class is every ASCII letter and digit. */
static bool is_bitcoin_address(const char *s, size_t len)
{
    if (py_istartswith(s, len, "bitcoin:")) {
        return true;
    }
    size_t p;
    if (py_istartswith(s, len, "bc1") || py_istartswith(s, len, "tb1") ||
        py_istartswith(s, len, "bcr")) {
        p = 3;
    } else if (len > 0 && strchr("123mnMN", s[0]) != NULL && s[0] != '\0') {
        p = 1;
    } else {
        return false;
    }
    const size_t end = dollar_end(s, len);
    if (end < p + 25 || end > p + 62) {
        return false;
    }
    for (size_t i = p; i < end; i++) {
        if (!is_alnum(s[i])) {
            return false;
        }
    }
    return true;
}

/* A BIP-39 word, or a word's first four letters ([:4]), exactly. */
static bool bip39_token(const char *t, size_t n, bool four)
{
    char w[9];
    if (n == 0 || n > 8) {
        return false;
    }
    memcpy(w, t, n);
    w[n] = '\0';
    if (!four) {
        return bip39_word_index(w) >= 0;
    }
    if (n > 4) {
        return false;
    }
    for (int i = 0; i < BIP39_WORDS; i++) {
        const char *word = BIP39_WORDLIST[i];
        const size_t wl = strlen(word);
        if ((wl < 4 ? wl : 4) == n && memcmp(word, w, n) == 0) {
            return true;
        }
    }
    return false;
}

/* all(x in wordlist for x in s.strip().split(" ")) */
static bool all_tokens(const char *s, size_t len, bool four)
{
    py_strip(&s, &len);
    size_t start = 0;
    for (size_t i = 0; i <= len; i++) {
        if (i == len || s[i] == ' ') {
            if (!bip39_token(s + start, i - start, four)) {
                return false;
            }
            start = i + 1;
        }
    }
    return true;
}

qr_type_t qr_detect(const uint8_t *data, size_t len)
{
    if (data == NULL || len > QR_FRAME_MAX) {
        return QR_TYPE_INVALID;
    }

    if (py_utf8_valid(data, len)) {
        const char *s = (const char *)data;

        if (py_istartswith(s, len, "ur:crypto-psbt/")) return QR_TYPE_PSBT_UR2;
        if (py_istartswith(s, len, "ur:crypto-output/")) return QR_TYPE_OUTPUT_UR;
        if (py_istartswith(s, len, "ur:crypto-account/")) return QR_TYPE_ACCOUNT_UR;
        if (is_specter_psbt(s, len)) return QR_TYPE_PSBT_SPECTER;
        if (py_istartswith(s, len, "ur:bytes/")) return QR_TYPE_BYTES_UR;
        if (is_base64_psbt(s, len)) return QR_TYPE_PSBT_BASE64;
        if (is_bbqr(s, len)) return QR_TYPE_PSBT_BBQR;

        const char *x, *y;
        size_t xn, yn;
        if (match_pxofy(s, len, &x, &xn, &y, &yn) > 0) return QR_TYPE_WALLET_SPECTER;
        if (is_specter_wallet_json(s, len)) return QR_TYPE_WALLET_SPECTER;
        if (py_contains(s, len, "multisig setup file", true)) return QR_TYPE_WALLET_CONFIGFILE;
        if (py_contains(s, len, "sortedmulti", false)) return QR_TYPE_WALLET_GENERIC;

        if (has_48_digits(s, len)) return QR_TYPE_SEED_SEEDQR;
        if (is_bitcoin_address(s, len)) return QR_TYPE_BITCOIN_ADDRESS;
        if (py_startswith(s, len, "signmessage")) return QR_TYPE_SIGN_MESSAGE;
        if (py_startswith(s, len, "settings::")) return QR_TYPE_SETTINGS;

        if (all_tokens(s, len, false)) return QR_TYPE_SEED_MNEMONIC;
        if (all_tokens(s, len, true)) return QR_TYPE_SEED_FOUR_LETTER_MNEMONIC;
        if (is_base43_psbt(s, len)) return QR_TYPE_PSBT_BASE43;
    }

    /* Not text, or no text type: 16 or 32 bytes is a CompactSeedQR. */
    if (len == 16 || len == 32) {
        return QR_TYPE_SEED_COMPACTSEEDQR;
    }
    return QR_TYPE_INVALID;
}
