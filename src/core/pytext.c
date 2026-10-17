/* Python string rules for the QR decoders. See pytext.h. */

#include "pytext.h"

#include <string.h>

bool py_utf8_valid(const uint8_t *s, size_t len)
{
    size_t i = 0;
    while (i < len) {
        const uint8_t c = s[i];
        size_t n;
        uint32_t cp, min;
        if (c < 0x80) {
            i++;
            continue;
        } else if (c >= 0xC2 && c <= 0xDF) {
            n = 1; cp = c & 0x1F; min = 0x80;
        } else if (c >= 0xE0 && c <= 0xEF) {
            n = 2; cp = c & 0x0F; min = 0x800;
        } else if (c >= 0xF0 && c <= 0xF4) {
            n = 3; cp = c & 0x07; min = 0x10000;
        } else {
            return false;
        }
        if (len - i - 1 < n) {
            return false;
        }
        for (size_t k = 1; k <= n; k++) {
            if ((s[i + k] & 0xC0) != 0x80) {
                return false;
            }
            cp = (cp << 6) | (s[i + k] & 0x3F);
        }
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return false;
        }
        i += n + 1;
    }
    return true;
}

/* Decode the code point at s[0] of valid UTF-8; its length in *n. */
static uint32_t code_point(const char *s, size_t len, size_t *n)
{
    const uint8_t c = (uint8_t)s[0];
    if (c < 0x80 || len < 2) {
        *n = 1;
        return c;
    }
    if (c < 0xE0) {
        *n = 2;
        return ((uint32_t)(c & 0x1F) << 6) | ((uint8_t)s[1] & 0x3F);
    }
    if (c < 0xF0 && len >= 3) {
        *n = 3;
        return ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)((uint8_t)s[1] & 0x3F) << 6) |
               ((uint8_t)s[2] & 0x3F);
    }
    if (len >= 4) {
        *n = 4;
        return ((uint32_t)(c & 0x07) << 18) | ((uint32_t)((uint8_t)s[1] & 0x3F) << 12) |
               ((uint32_t)((uint8_t)s[2] & 0x3F) << 6) | ((uint8_t)s[3] & 0x3F);
    }
    *n = 1;
    return c;
}

static bool is_space_cp(uint32_t cp)
{
    return (cp >= 0x09 && cp <= 0x0D) || (cp >= 0x1C && cp <= 0x20) ||
           cp == 0x85 || cp == 0xA0 || cp == 0x1680 ||
           (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 || cp == 0x2029 ||
           cp == 0x202F || cp == 0x205F || cp == 0x3000;
}

size_t py_space_at(const char *s, size_t len)
{
    if (len == 0) {
        return 0;
    }
    size_t n;
    const uint32_t cp = code_point(s, len, &n);
    return is_space_cp(cp) ? n : 0;
}

/* Length of the whitespace code point ending at s[len - 1], or 0. */
static size_t space_before(const char *s, size_t len)
{
    size_t start = len;
    /* Back up over continuation bytes to the lead byte. */
    while (start > 0 && ((uint8_t)s[start - 1] & 0xC0) == 0x80 && len - start < 3) {
        start--;
    }
    if (start == 0) {
        return 0;
    }
    start--;
    const size_t n = py_space_at(s + start, len - start);
    return (n == len - start) ? n : 0;
}

void py_strip(const char **s, size_t *len)
{
    size_t n;
    while (*len > 0 && (n = py_space_at(*s, *len)) > 0) {
        *s += n;
        *len -= n;
    }
    while (*len > 0 && (n = space_before(*s, *len)) > 0) {
        *len -= n;
    }
}

bool py_split_next(const char *s, size_t len, size_t *pos,
                   const char **field, size_t *field_len)
{
    size_t i = *pos, n;
    while (i < len && (n = py_space_at(s + i, len - i)) > 0) {
        i += n;
    }
    if (i >= len) {
        *pos = len;
        return false;
    }
    const size_t start = i;
    while (i < len && py_space_at(s + i, len - i) == 0) {
        size_t cl;
        code_point(s + i, len - i, &cl);
        i += cl;
    }
    *field = s + start;
    *field_len = i - start;
    *pos = i;
    return true;
}

bool py_startswith(const char *s, size_t len, const char *prefix)
{
    const size_t p = strlen(prefix);
    return len >= p && memcmp(s, prefix, p) == 0;
}

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* For an ASCII needle, s.lower() matches exactly where an ASCII fold does:
   no non-ASCII character lowers to a lone ASCII letter except the Kelvin sign
   and U+0130, and callers here never search for 'k', nor does U+0130 lower
   to a lone 'i'. */
bool py_istartswith(const char *s, size_t len, const char *prefix)
{
    const size_t p = strlen(prefix);
    if (len < p) {
        return false;
    }
    for (size_t i = 0; i < p; i++) {
        if (lower(s[i]) != prefix[i]) {
            return false;
        }
    }
    return true;
}

bool py_contains(const char *s, size_t len, const char *needle, bool fold)
{
    const size_t n = strlen(needle);
    if (n == 0) {
        return true;
    }
    for (size_t i = 0; i + n <= len; i++) {
        size_t k = 0;
        while (k < n && (fold ? lower(s[i + k]) : s[i + k]) == needle[k]) {
            k++;
        }
        if (k == n) {
            return true;
        }
    }
    return false;
}

bool py_int(const char *s, size_t len, long *out)
{
    py_strip(&s, &len);
    if (len == 0) {
        return false;
    }
    bool neg = false;
    size_t i = 0;
    if (s[0] == '+' || s[0] == '-') {
        neg = s[0] == '-';
        i = 1;
    }
    long v = 0;
    bool digit_before = false;
    for (; i < len; i++) {
        if (s[i] == '_') {
            /* Only between two digits. */
            if (!digit_before || i + 1 >= len || s[i + 1] < '0' || s[i + 1] > '9') {
                return false;
            }
            digit_before = false;
            continue;
        }
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
        if (v > 100000000L) {
            return false;
        }
        v = v * 10 + (s[i] - '0');
        digit_before = true;
    }
    if (!digit_before) {
        return false;
    }
    *out = neg ? -v : v;
    return true;
}

/* ---------------------------------------------------------------- base64 */

static int b64_value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool py_is_base64(const char *s, size_t len)
{
    if (len % 4 != 0) {
        return false;
    }
    size_t pad = 0;
    while (pad < 2 && pad < len && s[len - 1 - pad] == '=') {
        pad++;
    }
    const size_t body = len - pad;
    for (size_t i = 0; i < body; i++) {
        if (b64_value(s[i]) < 0) {
            return false;
        }
    }
    /* A third '=' would leave a lone character in the last quantum. */
    if (pad > 0 && body > 0 && body % 4 == 1) {
        return false;
    }
    if (pad == 0 && body % 4 != 0) {
        return false;
    }
    /* The bits past the last whole byte must be zero, or the re-encode
       differs. */
    if (pad == 1 && (b64_value(s[body - 1]) & 0x03) != 0) {
        return false;
    }
    if (pad == 2 && (b64_value(s[body - 1]) & 0x0F) != 0) {
        return false;
    }
    if (pad > 0 && body == 0) {
        return false;
    }
    return true;
}

long py_b64decode(const char *s, size_t len, uint8_t *out, size_t cap)
{
    size_t n = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (size_t i = 0; i < len && s[i] != '='; i++) {
        acc = (acc << 6) | (uint32_t)b64_value(s[i]);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (n >= cap) {
                return -1;
            }
            out[n++] = (uint8_t)(acc >> bits);
        }
    }
    return (long)n;
}

/* ---------------------------------------------------------------- base32 */

static int b32_value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= '2' && c <= '7') return c - '2' + 26;
    return -1;
}

long py_b32decode_padded(const char *s, size_t len, uint8_t *out, size_t cap)
{
    /* The padded string is s + pad '='s, so its length is a multiple of 8
       and Python's first check always passes. Its pad count is the run of
       '=' at the end of the padded string. */
    const size_t pad_added = (8 - len % 8) % 8;
    size_t body = len;
    while (body > 0 && s[body - 1] == '=') {
        body--;
    }
    const size_t padchars = (len - body) + pad_added;

    /* Every full quantum of the stripped string, '=' inside one included,
       must be in the alphabet. */
    size_t n = 0;
    uint64_t acc = 0;
    for (size_t q = 0; q < body; q += 8) {
        acc = 0;
        for (size_t k = q; k < q + 8 && k < body; k++) {
            const int v = b32_value(s[k]);
            if (v < 0) {
                return -1;
            }
            acc = (acc << 5) | (uint64_t)v;
        }
        /* Python shifts in only the characters present, so a short last
           quantum is a smaller number; it then fixes it up below. */
        const size_t got = (q + 8 <= body) ? 8 : body - q;
        if (got == 8) {
            if (n + 5 > cap) return -1;
            for (int b = 4; b >= 0; b--) {
                out[n++] = (uint8_t)(acc >> (8 * b));
            }
        } else {
            /* Python writes acc.to_bytes(5) here; a value over 40 bits
               cannot happen with fewer than 8 characters. */
            if (n + 5 > cap) return -1;
            for (int b = 4; b >= 0; b--) {
                out[n++] = (uint8_t)(acc >> (8 * b));
            }
        }
    }
    if (padchars != 0 && padchars != 1 && padchars != 3 && padchars != 4 && padchars != 6) {
        return -1;
    }
    if (padchars > 0 && n > 0) {
        /* acc <<= 5 * padchars; keep the leading (43 - 5 * padchars) // 8
           bytes of its 5-byte form in place of the last 5. */
        const uint64_t last = acc << (5 * padchars);
        const size_t leftover = (43 - 5 * padchars) / 8;
        n -= 5;
        for (size_t b = 0; b < leftover; b++) {
            out[n++] = (uint8_t)(last >> (8 * (4 - b)));
        }
    }
    return (long)n;
}

/* ---------------------------------------------------------------- hex */

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool ascii_space(char c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}

long py_fromhex(const char *s, size_t len, uint8_t *out, size_t cap)
{
    size_t n = 0, i = 0;
    while (i < len) {
        if (ascii_space(s[i])) {
            i++;
            continue;
        }
        if (i + 1 >= len) {
            return -1;
        }
        const int hi = hex_value(s[i]), lo = hex_value(s[i + 1]);
        if (hi < 0 || lo < 0 || n >= cap) {
            return -1;
        }
        out[n++] = (uint8_t)((hi << 4) | lo);
        i += 2;
    }
    return (long)n;
}

/* ---------------------------------------------------------------- base43 */

static const char B43[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ$*+-./:";

long py_base43_decode(const char *s, size_t len, uint8_t *out, size_t cap)
{
    if (cap == 0) {
        return -1;
    }
    /* The value, big-endian, in the tail of `out`; `used` bytes of it. */
    size_t used = 0;
    for (size_t i = 0; i < len; i++) {
        const char *p = s[i] ? strchr(B43, s[i]) : NULL;
        if (p == NULL) {
            return -1;
        }
        uint32_t carry = (uint32_t)(p - B43);
        for (size_t k = 0; k < used; k++) {
            uint8_t *b = &out[cap - 1 - k];
            const uint32_t v = (uint32_t)*b * 43 + carry;
            *b = (uint8_t)v;
            carry = v >> 8;
        }
        while (carry > 0) {
            if (used >= cap) {
                return -1;
            }
            out[cap - 1 - used] = (uint8_t)carry;
            used++;
            carry >>= 8;
        }
    }
    /* The reference always emits at least one byte for the value, 0 too. */
    if (used == 0) {
        out[cap - 1] = 0;
        used = 1;
    }
    size_t zeros = 0;
    while (zeros < len && s[zeros] == '0') {
        zeros++;
    }
    if (zeros + used > cap) {
        return -1;
    }
    memmove(out + zeros, out + cap - used, used);
    memset(out, 0, zeros);
    return (long)(zeros + used);
}
