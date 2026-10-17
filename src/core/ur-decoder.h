/**
 * Assembling a message from animated-QR parts, as SeedSigner assembles it.
 *
 * ---------------------------------------------------------------------------
 * What arrives
 *
 *   ur:<type>/<bytewords>                        a single-part UR
 *   ur:<type>/<seq_num>-<seq_len>/<bytewords>    one part of many
 *
 * and a part's bytewords carry CBOR `[seq_num, seq_len, message_len,
 * checksum, fragment]`. Parts 1..seq_len are the plain fragments; later parts
 * are the XOR of a subset both ends derive (ur-fountain.h).
 *
 * ---------------------------------------------------------------------------
 * Why this follows SeedSigner step for step
 *
 * The algorithm is a port of SeedSigner's fountain decoder
 * (seedsigner/helpers/ur2/fountain_decoder.py, from Foundation Devices'
 * foundation-ur-py), queue order and quirks included, because what the Scan
 * screen shows per frame ("new part" or "seen it") and the frame on which a
 * scan completes are behaviour a SeedSigner user already knows. The
 * reductions, the FIFO queue and the "did this part add anything" rule are
 * the reference's. So is one quirk worth knowing: its subset test is not
 * strict, so a mixed part that meets another with the same fragments leaves
 * an empty entry behind, and that entry counts as a mixed part.
 * sim/test_qr_decode.c replays SeedSigner's own decodes frame by frame.
 *
 * ---------------------------------------------------------------------------
 * Where it is deliberately stricter
 *
 * - A part whose fragment length cannot cover message_len in seq_len
 *   fragments, or would need fewer than seq_len, is refused. bc-ur never sends
 *   one; the reference would accept it and fail the checksum at the end.
 * - Trailing bytes after the part's CBOR array are refused.
 * - The sequence component is ASCII digits only. Python's int() also takes
 *   "+1", " 1" and other scripts' digits.
 * - A message whose CRC-32 does not match ends the decode as failed. The
 *   reference also ends it, but reports it complete and leaves the caller to
 *   trip over the missing result.
 * - Everything is bounded by the caller's arena. A mixed part that would not
 *   fit is dropped as "nothing new"; the reference has no bound.
 *
 * ---------------------------------------------------------------------------
 * Memory
 *
 * No allocation. The caller hands over one arena; at the first part the
 * decoder lays out the fragments, the mixed parts and the queue in it, sized
 * by that part's seq_len and fragment length. A first part that cannot fit is
 * refused, so a sender cannot walk the decoder into a corner later. The
 * fields a sender controls are pinned at the first accepted part, as the
 * reference pins them.
 */

#ifndef LEEK_UR_DECODER_H
#define LEEK_UR_DECODER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ur-fountain.h"
#include "ur.h"

/** Largest decoded part. A version-40 QR holds 4296 alphanumeric
 *  characters, which is 2148 bytes of bytewords including the CRC. */
#define UR_PART_MAX 2200

/** Mixed parts the arena must hold at the least for a first part to be
 *  accepted. More are used when the arena has room. */
#define UR_DECODER_MIN_MIXED 16

typedef enum {
    UR_PART_REJECTED,   /* malformed, or inconsistent with the first part */
    UR_PART_REDUNDANT,  /* valid, but it added nothing */
    UR_PART_ACCEPTED,   /* it added a fragment or a mixed part */
    UR_PART_COMPLETE,   /* the decode has ended: see ur_decoder_failed() */
} UrPartResult;

typedef struct {
    bool     started;
    bool     done;
    bool     failed;         /* done, and the CRC-32 did not match */
    char     type[UR_TYPE_MAX + 1];

    uint32_t seq_len;
    uint32_t message_len;
    uint32_t checksum;
    uint32_t fragment_len;
    uint32_t mask_bytes;
    uint32_t processed;      /* parts that passed validation */

    uint8_t *arena;
    size_t   arena_cap;

    /* Laid out in the arena at the first part. An entry is a fragment mask
       of mask_bytes followed by fragment_len bytes of data. */
    size_t   entry_size;
    uint8_t *fragments;      /* seq_len * fragment_len, by index */
    uint8_t *received;       /* mask of the fragments known */
    uint32_t received_count;
    uint8_t *mixed;          /* mixed_max entries, in insertion order */
    uint32_t mixed_count, mixed_max;
    uint8_t *queue;          /* mixed_max + 1 entries, a ring */
    uint32_t queue_head, queue_count;
    uint8_t *spare;          /* two entries of working space */

    size_t   single_len;     /* a single-part UR: its message, in `raw` */

    UrFountainScratch scratch;
    uint8_t  raw[UR_PART_MAX];
} UrDecoder;

/** Point a decoder at its arena and clear it. */
void ur_decoder_init(UrDecoder *d, uint8_t *arena, size_t arena_cap);

/** Forget everything and wipe the arena, keeping it. */
void ur_decoder_reset(UrDecoder *d);

/**
 * Feed one scanned UR string, single-part or multi-part.
 *
 * Once the decode has ended every further part returns UR_PART_COMPLETE, as
 * SeedSigner's DecodeQR reports it.
 */
UrPartResult ur_decoder_receive(UrDecoder *d, const char *ur, size_t ur_len);

/** True once the decode has ended, successfully or not. */
bool ur_decoder_complete(const UrDecoder *d);

/** True if it ended with a CRC-32 mismatch. */
bool ur_decoder_failed(const UrDecoder *d);

/** The assembled message, or NULL unless it ended successfully. */
const uint8_t *ur_decoder_message(const UrDecoder *d, size_t *len);

/** The UR type, lowercase, once a part has been accepted; "" before. */
const char *ur_decoder_type(const UrDecoder *d);

/**
 * Progress as SeedSigner's scan screen computes it, 0.0 to 1.0, from the
 * fountain's state alone (so a single-part UR reads 0, as there).
 *
 * With `weight_mixed` false: parts processed against 1.75 times seq_len,
 * capped at 0.99. With it true: known fragments, plus for each fragment that
 * appears in a mixed part a share of 1/degree per part, capped at 0.75.
 */
double ur_decoder_progress(const UrDecoder *d, bool weight_mixed);

#endif /* LEEK_UR_DECODER_H */
