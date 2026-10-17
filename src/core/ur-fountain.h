/**
 * The BC-UR fountain code: which fragments a given part carries.
 *
 * ---------------------------------------------------------------------------
 * Why a fountain at all
 *
 * A payload too large for one QR frame is split into `seq_len` fragments. The
 * first `seq_len` parts are the plain fragments, in order. Every part after
 * that is the XOR of a pseudo-randomly chosen subset of them. A receiver can
 * therefore start watching an animation at any moment, miss frames to glare or
 * a shaky hand, and still converge - which is the whole reason animated QR is
 * usable by a person holding a device in one hand.
 *
 * The subset is not transmitted. Both sides derive it from the part number, the
 * fragment count and the checksum of the whole message, so it must be derived
 * *identically* or the two sides silently disagree about what they are XORing.
 * That is why this file exists separately and why it is tested against
 * Blockchain Commons' implementation rather than against itself.
 *
 * ---------------------------------------------------------------------------
 * The derivation, in order
 *
 *   seed    = SHA-256(seq_num_be32 || checksum_be32)
 *   rng     = Xoshiro256** seeded with that digest, big-endian per word
 *   degree  = a sample from the distribution 1/1, 1/2 ... 1/seq_len,
 *             drawn with Walker's alias method
 *   subset  = the first `degree` entries of a shuffle of 0..seq_len-1
 *
 * The algorithm is from bc-ur (BSD-2-Clause Plus Patent). The implementation
 * is LeekWallet's: no allocation, no globals, caller owns the scratch space.
 *
 * ---------------------------------------------------------------------------
 * Floating point
 *
 * The degree choice uses doubles, so interoperating means producing bit-
 * identical IEEE-754 results. That is fine on any conforming implementation but
 * it is a real constraint, and it is the reason the test compares against the
 * reference across thousands of cases rather than spot-checking a few.
 *
 * One wrinkle. The sum of the probabilities is a plain left-to-right sum, as
 * bc-ur, Hummingbird (Sparrow) and Python 3.10 compute it. SeedSigner OS ships
 * Python 3.12, whose sum() is compensated, so on a SeedSigner the last bits of
 * some probabilities differ. The alias table never does (checked for every
 * seq_len up to UR_MAX_PARTS), and a pick could only differ if a random draw
 * fell between two adjacent doubles. The plain sum keeps us interoperable
 * with the senders that matter, and the SeedSigner vectors still match.
 */

#ifndef LEEK_UR_FOUNTAIN_H
#define LEEK_UR_FOUNTAIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Largest fragment count we will handle.
 *
 * A PSBT can run to tens of kilobytes, and senders choose fragments of a few
 * hundred bytes at most, so this allows a large consolidation. It is a bound
 * and not a buffer: a message claiming more parts than this is refused,
 * because the alternative is a scratch struct whose size an attacker chooses.
 */
#define UR_MAX_PARTS 1024

/** Bytes of bitmap needed for UR_MAX_PARTS fragments. */
#define UR_PART_MASK_BYTES ((UR_MAX_PARTS + 7) / 8)

/**
 * Working space for one fragment choice, about 26 KB at UR_MAX_PARTS. It
 * belongs in the caller's context (PSRAM on the device), never on a stack.
 */
typedef struct {
    double  scaled[UR_MAX_PARTS];
    double  probs[UR_MAX_PARTS];
    int16_t aliases[UR_MAX_PARTS];
    int16_t small[UR_MAX_PARTS];
    int16_t large[UR_MAX_PARTS];
    int16_t remaining[UR_MAX_PARTS];
} UrFountainScratch;

/** Xoshiro256** state. Exposed so the tests can drive it directly. */
typedef struct {
    uint64_t s[4];
} UrRng;

/** Seed from a 32-byte digest, big-endian per 64-bit word. */
void ur_rng_seed(UrRng *rng, const uint8_t digest[32]);

/** Next raw 64 bits. */
uint64_t ur_rng_next(UrRng *rng);

/** Next double in [0, 1). */
double ur_rng_next_double(UrRng *rng);

/** Next integer in [low, high], inclusive, as bc-ur draws it. */
uint64_t ur_rng_next_int(UrRng *rng, uint64_t low, uint64_t high);

/**
 * Set the bits of `mask` for the fragments part `seq_num` carries.
 *
 * `seq_num` is 1-based, as it appears on the wire. Parts 1..seq_len select
 * exactly one fragment each; later parts select a mixed subset.
 *
 * Returns false and leaves `mask` untouched if `seq_len` is zero or above
 * UR_MAX_PARTS, or if `seq_num` is zero.
 */
bool ur_fountain_fragments(uint32_t seq_num, uint32_t seq_len, uint32_t checksum,
                           uint8_t mask[UR_PART_MASK_BYTES],
                           UrFountainScratch *scratch);

/** Count of set bits in a fragment mask. */
uint32_t ur_mask_count(const uint8_t mask[UR_PART_MASK_BYTES], uint32_t seq_len);

#endif /* LEEK_UR_FOUNTAIN_H */
