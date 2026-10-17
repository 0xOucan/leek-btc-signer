/**
 * The Python string rules SeedSigner's QR code relies on, in C.
 *
 * SeedSigner decides what a QR code is with str.strip(), str.split(),
 * bytes.decode('utf-8'), base64 round trips, base32 and hex. Each of those
 * has edges (Unicode whitespace, strict UTF-8, canonical base64, '=' already
 * at the end of a base32 segment) that decide whether a frame is accepted.
 * Porting them once, here, keeps qr-detect.c and qr-decode.c reading like the
 * Python they port.
 *
 * Nothing allocates. Text is a pointer and a length and need not be
 * NUL-terminated.
 */

#ifndef LBS_PYTEXT_H
#define LBS_PYTEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** bytes.decode('utf-8') succeeds: no overlongs, surrogates or > U+10FFFF. */
bool py_utf8_valid(const uint8_t *s, size_t len);

/**
 * If str.isspace() holds for the code point at s[0], its length in bytes,
 * else 0. Valid UTF-8 is assumed. Covers ASCII \t\n\v\f\r, space, \x1c-\x1f
 * and the Unicode whitespace Python strips.
 */
size_t py_space_at(const char *s, size_t len);

/** str.strip() with no argument: the slice of `s` it would return. */
void py_strip(const char **s, size_t *len);

/**
 * str.split() with no argument: the next field of a whitespace split.
 * Start with *pos = 0. Returns false when no field is left.
 */
bool py_split_next(const char *s, size_t len, size_t *pos,
                   const char **field, size_t *field_len);

/** s.startswith(prefix) */
bool py_startswith(const char *s, size_t len, const char *prefix);

/** s.lower().startswith(prefix) for an ASCII lowercase prefix. */
bool py_istartswith(const char *s, size_t len, const char *prefix);

/** `needle in s` for an ASCII needle; `fold` compares s.lower(). */
bool py_contains(const char *s, size_t len, const char *needle, bool fold);

/**
 * int(s) for a field of ASCII text: surrounding whitespace, an optional sign,
 * digits with single underscores between them. Other scripts' digits are not
 * accepted (Python takes them). False where int() would raise, or past
 * +-1e9.
 */
bool py_int(const char *s, size_t len, long *out);

/**
 * base64.b64encode(base64.b64decode(s)) == s: standard alphabet, padded,
 * nothing extra, zero trailing bits. SeedSigner's is_base64().
 */
bool py_is_base64(const char *s, size_t len);

/** Decode text that py_is_base64() accepted. Returns the byte count, or
 *  -1 if it does not fit. */
long py_b64decode(const char *s, size_t len, uint8_t *out, size_t cap);

/**
 * b32decode(seg + '=' * ((8 - len % 8) % 8)), as SeedSigner's BBQr decoder
 * calls it: uppercase RFC 4648 alphabet, and Python's padding rule, '='
 * already at the end of `seg` included. Returns the byte count or -1.
 */
long py_b32decode_padded(const char *s, size_t len, uint8_t *out, size_t cap);

/** bytes.fromhex(s): pairs of hex digits, ASCII whitespace allowed between
 *  pairs. Returns the byte count or -1. */
long py_fromhex(const char *s, size_t len, uint8_t *out, size_t cap);

/**
 * SeedSigner's DecodeQR.base43_decode (Electrum's alphabet). Leading '0'
 * characters become leading zero bytes, after a value that is always at
 * least one byte long, so "0" decodes to two zero bytes, as there. Returns
 * the byte count, or -1 for a character outside the alphabet or no room.
 */
long py_base43_decode(const char *s, size_t len, uint8_t *out, size_t cap);

#endif /* LBS_PYTEXT_H */
