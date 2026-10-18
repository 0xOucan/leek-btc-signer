/**
 * What the device shows as QR codes: a port of SeedSigner's encoders
 * (seedsigner/models/encode_qr.py).
 *
 * Each encoder is a sequence of parts, as there: a static code is one part,
 * shown for as long as the screen is up; an animated one hands out parts in a
 * cycle (Specter's legacy pXofY) or forever (a UR fountain). The text of
 * every part is SeedSigner's, byte for byte, so whatever reads a SeedSigner's
 * codes reads these. sim/test_qr_encode.c checks that against SeedSigner's
 * own output, and reads every part back through quirc.
 *
 *   SeedQR            four digits per word
 *   CompactSeedQR     the entropy, as bytes
 *   xpub, static      "[fingerprint/path]xpub", the SLIP-132 version embit picks
 *   xpub, Specter     the same text in "pXofY " parts (legacy Specter Desktop)
 *   xpub, UR          crypto-account (BCR-2020-015), as SeedSigner builds it
 *   PSBT, UR          crypto-psbt
 *
 * The density setting chooses the fragment size, as SeedSigner's does: 10,
 * 30 or 120 bytes for a UR, 40, 65 or 90 characters for Specter.
 *
 * Memory: the encoder holds its text and, for a UR, the CBOR message, in a
 * buffer the caller provides (PSRAM on the device for a PSBT). A SeedQR's
 * buffer holds the seed: qr_encoder_wipe() when the screen closes.
 */

#ifndef LBS_QR_ENCODE_H
#define LBS_QR_ENCODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "btc.h"
#include "ur-encoder.h"

typedef enum { QR_DENSITY_LOW, QR_DENSITY_MEDIUM, QR_DENSITY_HIGH } qr_density_t;

typedef enum { QR_OUT_STATIC, QR_OUT_SPECTER, QR_OUT_UR } qr_out_kind_t;

typedef struct {
    qr_out_kind_t kind;
    uint8_t  *buf;          /* caller's: the text, or the UR message */
    size_t    cap, len;
    size_t    fragment;     /* Specter: characters per part */
    uint32_t  parts, sent;  /* Specter: part count, parts handed out */
    UrEncoder ur;
} qr_encoder_t;

/* SeedQrEncoder and CompactSeedQrEncoder. */
bool qr_encode_seedqr(qr_encoder_t *e, const seed_t *s, uint8_t *buf, size_t cap);
bool qr_encode_compact_seedqr(qr_encoder_t *e, const seed_t *s, uint8_t *buf, size_t cap);

/* StaticXpubQrEncoder, SpecterLegacyXPubQrEncoder, UrXpubQrEncoder. */
bool qr_encode_xpub(qr_encoder_t *e, const seed_t *s, const char *path, btc_net_t net, btc_sig_t sig,
                    uint8_t *buf, size_t cap);
bool qr_encode_xpub_specter(qr_encoder_t *e, const seed_t *s, const char *path, btc_net_t net, btc_sig_t sig,
                            qr_density_t density, uint8_t *buf, size_t cap);
bool qr_encode_xpub_ur(qr_encoder_t *e, const seed_t *s, const char *path, btc_net_t net,
                       qr_density_t density, uint8_t *buf, size_t cap);

/* UrPsbtQrEncoder: `psbt` is the serialized PSBT. */
bool qr_encode_psbt_ur(qr_encoder_t *e, const uint8_t *psbt, size_t len,
                       qr_density_t density, uint8_t *buf, size_t cap);

/* GenericStaticQrEncoder: any text, e.g. an address. */
bool qr_encode_text(qr_encoder_t *e, const char *text, uint8_t *buf, size_t cap);

/**
 * crypto-account CBOR for one xpub, as SeedSigner's UrXpubQrEncoder builds
 * it: the master fingerprint and one output descriptor per script type the
 * path implies (all six if it implies none). Returns the length, or 0.
 */
size_t qr_crypto_account(const seed_t *s, const char *path, btc_net_t net,
                         uint8_t *out, size_t cap);

/** next_part(): the next part's bytes, and its length. Static codes return
 *  the same part every time; UR parts are uppercased, as SeedSigner's are. */
size_t qr_encoder_next_part(qr_encoder_t *e, uint8_t *out, size_t cap);

/** seq_len(): parts in one cycle, 1 for a static code. */
uint32_t qr_encoder_seq_len(const qr_encoder_t *e);

/** Start the parts over (SeedSigner's restart()). */
void qr_encoder_restart(qr_encoder_t *e);

/** Zero the buffer and the encoder. */
void qr_encoder_wipe(qr_encoder_t *e);

#endif /* LBS_QR_ENCODE_H */
