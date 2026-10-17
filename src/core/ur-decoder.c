/* Fountain assembly of animated-QR parts. See ur-decoder.h. */

#include "ur-decoder.h"

#include <string.h>

#include "cbor.h"

/* Held mixed parts are compared with each other on every reduction, so the
   count is capped whatever the arena could take: past a few hundred the
   decoder would be slower than the camera. */
#define UR_DECODER_MAX_MIXED 256

/* ------------------------------------------------------------- masks */

static bool bit_get(const uint8_t *m, uint32_t i)
{
    return (m[i / 8] >> (i % 8)) & 1u;
}

static void bit_set(uint8_t *m, uint32_t i)
{
    m[i / 8] |= (uint8_t)(1u << (i % 8));
}

static uint32_t mask_count(const UrDecoder *d, const uint8_t *m)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < d->mask_bytes; i++) {
        for (uint8_t b = m[i]; b; b &= (uint8_t)(b - 1)) {
            n++;
        }
    }
    return n;
}

/* a is a subset of b, equal included. The reference's is_strict_subset()
   is Python's issubset(), so equal sets count, and that is kept. */
static bool mask_subset(const UrDecoder *d, const uint8_t *a, const uint8_t *b)
{
    for (uint32_t i = 0; i < d->mask_bytes; i++) {
        if (a[i] & (uint8_t)~b[i]) {
            return false;
        }
    }
    return true;
}

static uint32_t mask_first(const UrDecoder *d, const uint8_t *m)
{
    for (uint32_t i = 0; i < d->seq_len; i++) {
        if (bit_get(m, i)) {
            return i;
        }
    }
    return 0;
}

/* ------------------------------------------------------------- entries */

static uint8_t *entry_data(const UrDecoder *d, uint8_t *e)
{
    return e + d->mask_bytes;
}

static uint8_t *mixed_at(const UrDecoder *d, uint32_t i)
{
    return d->mixed + (size_t)i * d->entry_size;
}

static void xor_into(uint8_t *dst, const uint8_t *src, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        dst[i] ^= src[i];
    }
}

/* reduce_part_by_part(a, b): if b's fragments are within a's, take them
   out of a. */
static void reduce_by(const UrDecoder *d, uint8_t *a, const uint8_t *b)
{
    if (!mask_subset(d, b, a)) {
        return;
    }
    for (uint32_t i = 0; i < d->mask_bytes; i++) {
        a[i] &= (uint8_t)~b[i];
    }
    xor_into(a + d->mask_bytes, b + d->mask_bytes, d->fragment_len);
}

static void enqueue(UrDecoder *d, const uint8_t *e)
{
    /* Cannot overflow: the queue and the mixed parts together never hold
       more than mixed_max + 1 entries (see the layout in receive). */
    const uint32_t slot = (d->queue_head + d->queue_count) % (d->mixed_max + 1);
    memcpy(d->queue + (size_t)slot * d->entry_size, e, d->entry_size);
    d->queue_count++;
}

/* mixed_parts[key] = e: replace in place when the key is held, which keeps
   its position as a Python dict does, otherwise append. False when full. */
static bool mixed_put(UrDecoder *d, const uint8_t *e)
{
    for (uint32_t i = 0; i < d->mixed_count; i++) {
        uint8_t *m = mixed_at(d, i);
        if (memcmp(m, e, d->mask_bytes) == 0) {
            memcpy(m, e, d->entry_size);
            return true;
        }
    }
    if (d->mixed_count >= d->mixed_max) {
        return false;
    }
    memcpy(mixed_at(d, d->mixed_count++), e, d->entry_size);
    return true;
}

/* reduce_mixed_by(p): reduce every held mixed part by p, queue those that
   became simple, and rebuild the dict from the rest in order. */
static void reduce_mixed_by(UrDecoder *d, const uint8_t *p)
{
    const uint32_t n = d->mixed_count;
    for (uint32_t i = 0; i < n; i++) {
        reduce_by(d, mixed_at(d, i), p);
    }
    d->mixed_count = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t *m = mixed_at(d, i);
        if (mask_count(d, m) == 1) {
            enqueue(d, m);
        } else {
            /* Shifts down within the same array: the write position never
               passes the read position. A key already written takes this
               value in its own place. */
            bool replaced = false;
            for (uint32_t j = 0; j < d->mixed_count; j++) {
                uint8_t *w = mixed_at(d, j);
                if (memcmp(w, m, d->mask_bytes) == 0) {
                    memcpy(w, m, d->entry_size);
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                uint8_t *w = mixed_at(d, d->mixed_count++);
                if (w != m) {
                    memcpy(w, m, d->entry_size);
                }
            }
        }
    }
}

static void finish(UrDecoder *d)
{
    d->done = true;
    d->failed = ur_crc32(d->fragments, d->message_len) != d->checksum;
}

static void process_simple(UrDecoder *d, const uint8_t *p)
{
    const uint32_t idx = mask_first(d, p);
    if (bit_get(d->received, idx)) {
        return;
    }
    memcpy(d->fragments + (size_t)idx * d->fragment_len, p + d->mask_bytes,
           d->fragment_len);
    bit_set(d->received, idx);
    d->received_count++;
    if (d->received_count == d->seq_len) {
        finish(d);
    } else {
        reduce_mixed_by(d, p);
    }
}

static void process_mixed(UrDecoder *d, const uint8_t *p)
{
    uint8_t *p2 = d->spare + d->entry_size;
    memcpy(p2, p, d->entry_size);

    /* By every known fragment. Their order does not matter: taking one
       fragment out never changes whether another is in. */
    for (uint32_t i = 0; i < d->seq_len; i++) {
        if (bit_get(p2, i) && bit_get(d->received, i)) {
            p2[i / 8] &= (uint8_t)~(1u << (i % 8));
            xor_into(entry_data(d, p2), d->fragments + (size_t)i * d->fragment_len,
                     d->fragment_len);
        }
    }
    /* Then by every held mixed part, in order. Here order matters. */
    for (uint32_t i = 0; i < d->mixed_count; i++) {
        reduce_by(d, p2, mixed_at(d, i));
    }

    if (mask_count(d, p2) == 1) {
        enqueue(d, p2);
    } else {
        reduce_mixed_by(d, p2);
        mixed_put(d, p2);
    }
}

static void process_queue(UrDecoder *d)
{
    while (!d->done && d->queue_count > 0) {
        uint8_t *e = d->spare;
        memcpy(e, d->queue + (size_t)d->queue_head * d->entry_size, d->entry_size);
        d->queue_head = (d->queue_head + 1) % (d->mixed_max + 1);
        d->queue_count--;
        if (mask_count(d, e) == 1) {
            process_simple(d, e);
        } else {
            process_mixed(d, e);
        }
    }
}

/* ------------------------------------------------------------- parsing */

/* One decimal field of the sequence component: ASCII digits, at least one. */
static bool parse_num(const char *s, size_t len, uint32_t *out)
{
    uint32_t v = 0;
    if (len == 0) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (s[i] < '0' || s[i] > '9' || v > 100000000u) {
            return false;
        }
        v = v * 10 + (uint32_t)(s[i] - '0');
    }
    *out = v;
    return true;
}

/* [seq_num, seq_len, message_len, checksum, fragment] */
static bool parse_part(const uint8_t *cbor, size_t len, uint32_t f[4],
                       const uint8_t **data, uint32_t *data_len)
{
    CborReader r;
    CborItem it;

    cbor_reader_init(&r, cbor, len);
    if (!cbor_read(&r, &it) || it.type != CBOR_ARRAY || it.value != 5) {
        return false;
    }
    for (int i = 0; i < 4; i++) {
        if (!cbor_read(&r, &it) || it.type != CBOR_UINT) {
            return false;
        }
        f[i] = it.value;
    }
    if (!cbor_read(&r, &it) || it.type != CBOR_BYTES) {
        return false;
    }
    *data = it.data;
    *data_len = it.value;
    return cbor_reader_done(&r);
}

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* The first part fixes the layout of the arena. */
static bool start(UrDecoder *d, const char *type, uint32_t seq_len,
                  uint32_t message_len, uint32_t checksum, uint32_t frag_len)
{
    d->mask_bytes = (seq_len + 7) / 8;
    d->entry_size = d->mask_bytes + frag_len;

    const size_t frags = (size_t)seq_len * frag_len;
    const size_t fixed = frags + d->mask_bytes + 2 * d->entry_size;
    if (fixed > d->arena_cap) {
        return false;
    }
    /* mixed_max entries for the mixed parts and mixed_max + 1 for the queue */
    size_t m = (d->arena_cap - fixed) / d->entry_size;
    m = m > 0 ? (m - 1) / 2 : 0;
    if (m > UR_DECODER_MAX_MIXED) {
        m = UR_DECODER_MAX_MIXED;
    }
    if (m < UR_DECODER_MIN_MIXED) {
        return false;
    }

    uint8_t *p = d->arena;
    d->fragments = p;  p += frags;
    d->received = p;   p += d->mask_bytes;
    d->spare = p;      p += 2 * d->entry_size;
    d->mixed = p;      p += m * d->entry_size;
    d->queue = p;
    d->mixed_max = (uint32_t)m;
    memset(d->arena, 0, fixed);

    d->started = true;
    d->seq_len = seq_len;
    d->message_len = message_len;
    d->checksum = checksum;
    d->fragment_len = frag_len;
    strcpy(d->type, type);
    return true;
}

UrPartResult ur_decoder_receive(UrDecoder *d, const char *ur, size_t ur_len)
{
    if (d == NULL || ur == NULL) {
        return UR_PART_REJECTED;
    }
    if (d->done) {
        return UR_PART_COMPLETE;
    }

    /* "ur:" in any case, then <type>/<body> or <type>/<seq>/<body>. */
    if (ur_len < 3 || lower(ur[0]) != 'u' || lower(ur[1]) != 'r' || ur[2] != ':') {
        return UR_PART_REJECTED;
    }
    size_t slash[3], n_slash = 0;
    for (size_t i = 3; i < ur_len; i++) {
        if (ur[i] == '/') {
            if (n_slash == 3) {
                return UR_PART_REJECTED;
            }
            slash[n_slash++] = i;
        }
    }
    if (n_slash < 1 || n_slash > 2) {
        return UR_PART_REJECTED;
    }

    char type[UR_TYPE_MAX + 1];
    const size_t type_len = slash[0] - 3;
    if (type_len == 0 || type_len > UR_TYPE_MAX) {
        return UR_PART_REJECTED;
    }
    for (size_t i = 0; i < type_len; i++) {
        const char c = lower(ur[3 + i]);
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
            return UR_PART_REJECTED;
        }
        type[i] = c;
    }
    type[type_len] = '\0';

    /* The type is pinned by the first part of either kind. */
    if (d->type[0] != '\0' && strcmp(type, d->type) != 0) {
        return UR_PART_REJECTED;
    }

    if (n_slash == 1) {
        /* Single part: the whole message, no CBOR envelope. It ends the
           decode even part-way through a multi-part one, as in the
           reference. */
        size_t raw_len = 0;
        if (!ur_bytewords_decode(ur + slash[0] + 1, ur_len - slash[0] - 1,
                                 d->raw, sizeof d->raw, &raw_len) || raw_len == 0) {
            return UR_PART_REJECTED;
        }
        strcpy(d->type, type);
        d->single_len = raw_len;
        d->done = true;
        return UR_PART_COMPLETE;
    }

    uint32_t hdr_num, hdr_len;
    const char *seq = ur + slash[0] + 1;
    const size_t seq_total = slash[1] - slash[0] - 1;
    const char *dash = memchr(seq, '-', seq_total);
    if (dash == NULL ||
        !parse_num(seq, (size_t)(dash - seq), &hdr_num) ||
        !parse_num(dash + 1, seq_total - (size_t)(dash - seq) - 1, &hdr_len) ||
        hdr_num < 1 || hdr_len < 1) {
        return UR_PART_REJECTED;
    }

    size_t raw_len = 0;
    if (!ur_bytewords_decode(ur + slash[1] + 1, ur_len - slash[1] - 1,
                             d->raw, sizeof d->raw, &raw_len)) {
        return UR_PART_REJECTED;
    }
    uint32_t f[4];
    const uint8_t *data;
    uint32_t data_len;
    if (!parse_part(d->raw, raw_len, f, &data, &data_len)) {
        return UR_PART_REJECTED;
    }
    const uint32_t seq_num = f[0], seq_len = f[1], message_len = f[2], checksum = f[3];
    if (hdr_num != seq_num || hdr_len != seq_len) {
        return UR_PART_REJECTED;
    }

    if (!d->started) {
        if (seq_len > UR_MAX_PARTS || data_len == 0 || message_len == 0 ||
            (uint64_t)seq_len * data_len < message_len ||
            (uint64_t)(seq_len - 1) * data_len >= message_len ||
            !start(d, type, seq_len, message_len, checksum, data_len)) {
            return UR_PART_REJECTED;
        }
    } else if (seq_len != d->seq_len || message_len != d->message_len ||
               checksum != d->checksum || data_len != d->fragment_len) {
        return UR_PART_REJECTED;
    }

    uint8_t *e = d->spare;
    uint8_t mask[UR_PART_MASK_BYTES];
    if (!ur_fountain_fragments(seq_num, d->seq_len, d->checksum, mask, &d->scratch)) {
        return UR_PART_REJECTED;
    }
    memcpy(e, mask, d->mask_bytes);
    memcpy(entry_data(d, e), data, d->fragment_len);

    const uint32_t had_received = d->received_count;
    const uint32_t had_mixed = d->mixed_count;
    enqueue(d, e);
    process_queue(d);
    d->processed++;

    if (d->done) {
        return UR_PART_COMPLETE;
    }
    if (had_received == d->received_count && had_mixed == d->mixed_count) {
        return UR_PART_REDUNDANT;
    }
    return UR_PART_ACCEPTED;
}

/* ------------------------------------------------------------- the rest */

void ur_decoder_init(UrDecoder *d, uint8_t *arena, size_t arena_cap)
{
    memset(d, 0, sizeof *d);
    d->arena = arena;
    d->arena_cap = arena_cap;
}

void ur_decoder_reset(UrDecoder *d)
{
    uint8_t *a = d->arena;
    const size_t cap = d->arena_cap;
    if (a != NULL) {
        memset(a, 0, cap);
    }
    memset(d, 0, sizeof *d);
    ur_decoder_init(d, a, cap);
}

bool ur_decoder_complete(const UrDecoder *d)
{
    return d->done;
}

bool ur_decoder_failed(const UrDecoder *d)
{
    return d->done && d->failed;
}

const uint8_t *ur_decoder_message(const UrDecoder *d, size_t *len)
{
    if (!d->done || d->failed) {
        return NULL;
    }
    if (d->single_len > 0) {
        if (len) *len = d->single_len;
        return d->raw;
    }
    if (len) *len = d->message_len;
    return d->fragments;
}

const char *ur_decoder_type(const UrDecoder *d)
{
    return d->type;
}

double ur_decoder_progress(const UrDecoder *d, bool weight_mixed)
{
    /* The reference asks its fountain decoder, which a single-part UR never
       reaches: a static UR reads 0%, and one that ends a multi-part scan
       leaves the fountain's own estimate. */
    if (d->done && d->single_len == 0) {
        return 1.0;
    }
    if (!d->started) {
        return 0.0;
    }
    if (!weight_mixed) {
        const double p = (double)d->processed / ((double)d->seq_len * 1.75);
        return p < 0.99 ? p : 0.99;
    }

    /* Per fragment, the sum of 1/degree over the mixed parts naming it,
       each capped at 0.75 so a guess never weighs as much as a fragment. */
    double mixed_score = 0.0;
    for (uint32_t i = 0; i < d->seq_len; i++) {
        double score = 0.0;
        bool any = false;
        for (uint32_t m = 0; m < d->mixed_count; m++) {
            const uint8_t *e = mixed_at(d, m);
            if (bit_get(e, i)) {
                score += 1.0 / (double)mask_count(d, e);
                any = true;
            }
        }
        if (any) {
            mixed_score += score < 0.75 ? score : 0.75;
        }
    }
    return ((double)d->received_count + mixed_score) / (double)d->seq_len;
}
