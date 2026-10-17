/**
 * What a scanned QR code is, and putting animated ones back together: a port
 * of SeedSigner's DecodeQR (seedsigner/models/decode_qr.py).
 *
 * ---------------------------------------------------------------------------
 * The shape, as in SeedSigner
 *
 * Every frame the camera decodes goes to qr_decoder_add(). The first frame
 * fixes the type, detected exactly as detect_segment_type() detects it, in
 * the same order; a later frame of another type is refused. Each frame
 * returns SeedSigner's status: a new part, a part already seen, complete, or
 * invalid. Once complete, the getters hand back what SeedSigner's getters do:
 * the PSBT bytes, the seed's words, the address and its type, the settings
 * text or the message to sign.
 *
 * Supported here: PSBTs as UR (crypto-psbt), BBQr (hex, base32, zlib),
 * Specter's pXofY, base64 and base43; SeedQR, CompactSeedQR, mnemonics and
 * four-letter mnemonics; addresses and BIP-21 URIs; SettingsQR; sign-message
 * requests; and UR bytes, crypto-output and crypto-account, assembled.
 *
 * Detected but not yet decoded: wallet descriptors (Specter JSON, a generic
 * descriptor, a multisig setup file, and the descriptor inside a UR output,
 * account or bytes). They belong to multisig, task T47. Their frames return
 * QR_STATUS_UNSUPPORTED rather than a guess.
 *
 * ---------------------------------------------------------------------------
 * Where it is deliberately different
 *
 * - SeedSigner raises an exception, which ends the scan on an error screen,
 *   when a frame of another type arrives mid-scan, when an animated frame
 *   names a part outside its total, or when the total changes. These return
 *   QR_STATUS_TYPE_CHANGED and QR_STATUS_ERROR, so the scan screen can ignore
 *   a stray frame and keep going.
 * - Digits are ASCII digits. Python's \d and int() also take other scripts'.
 * - A UR whose CRC-32 fails at the end is INVALID. SeedSigner reports it
 *   complete and fails when it reads the result.
 * - A BBQr zlib stream that ends early gives no PSBT. SeedSigner inflates
 *   what is there and hands the prefix to its PSBT parser, which refuses it.
 * - An animated code is limited to QR_SEGMENTS_MAX parts and to the arena.
 *
 * sim/test_qr_decode.c replays every scan in SeedSigner's own test suite, and
 * generated ones, against what SeedSigner did with them.
 *
 * ---------------------------------------------------------------------------
 * Memory
 *
 * No allocation. The caller provides the decoder (a few tens of KB, for the
 * UR fountain scratch) and an arena for the frames. A scanned seed's words
 * are kept in the decoder and its frame is never copied into the arena, so
 * the decoder belongs in internal RAM (sec_alloc) and the arena may be PSRAM.
 * qr_decoder_reset() wipes both, and must be called when a scan ends.
 */

#ifndef LBS_QR_DECODE_H
#define LBS_QR_DECODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "btc.h"
#include "ur-decoder.h"

/* SeedSigner's QRType values that detect_segment_type() can return. */
typedef enum {
    QR_TYPE_NONE = 0,              /* nothing scanned yet */
    QR_TYPE_INVALID,
    QR_TYPE_PSBT_UR2,
    QR_TYPE_PSBT_SPECTER,
    QR_TYPE_PSBT_BASE64,
    QR_TYPE_PSBT_BASE43,
    QR_TYPE_PSBT_BBQR,
    QR_TYPE_SEED_SEEDQR,
    QR_TYPE_SEED_COMPACTSEEDQR,
    QR_TYPE_SEED_MNEMONIC,
    QR_TYPE_SEED_FOUR_LETTER_MNEMONIC,
    QR_TYPE_SETTINGS,
    QR_TYPE_BITCOIN_ADDRESS,
    QR_TYPE_SIGN_MESSAGE,
    QR_TYPE_WALLET_SPECTER,
    QR_TYPE_WALLET_GENERIC,
    QR_TYPE_WALLET_CONFIGFILE,
    QR_TYPE_OUTPUT_UR,
    QR_TYPE_ACCOUNT_UR,
    QR_TYPE_BYTES_UR,
    QR_TYPE_COUNT
} qr_type_t;

/* SeedSigner's DecodeQRStatus, plus the cases where it raises. */
typedef enum {
    QR_STATUS_PART_COMPLETE = 1,   /* a new part */
    QR_STATUS_PART_EXISTING,       /* a part already held, or one that added nothing */
    QR_STATUS_COMPLETE,
    QR_STATUS_FALSE,               /* no data */
    QR_STATUS_INVALID,
    QR_STATUS_TYPE_CHANGED,        /* SeedSigner raises */
    QR_STATUS_ERROR,               /* SeedSigner raises */
    QR_STATUS_UNSUPPORTED,         /* detected; decoding comes with T47 */
} qr_status_t;

/* SeedSigner's lowercase QRType string, e.g. "psbt__ur2". */
const char *qr_type_name(qr_type_t t);

/** detect_segment_type(). Pure: the PSBT checks parse with libwally. */
qr_type_t qr_detect(const uint8_t *data, size_t len);

/* An animated code of more parts than this is refused. BBQr's own limit is
   1295. */
#define QR_SEGMENTS_MAX 2048

typedef struct {
    qr_type_t type;
    bool      complete;

    /* Single frames and animated segments are copied into the arena. */
    uint8_t  *arena;
    size_t    arena_cap;

    /* Animated (Specter pXofY, BBQr): a table of (offset, length) at the
       start of the arena, then the text. */
    uint32_t  total, collected;
    uint32_t *seg_off, *seg_len;
    size_t    text_used, text_cap;
    uint8_t  *text;
    char      bbqr_encoding;

    /* Single frame: the frame, at the start of the arena. */
    size_t    frame_len;

    /* Results */
    uint16_t  words[SEED_MAX_WORDS];
    size_t    n_words;
    char      address[100];
    btc_script_t address_script;   /* BTC_SCRIPT_P2SH_P2WPKH stands for any P2SH */
    btc_net_t address_net;
    size_t    msg_path_off, msg_path_len, msg_off, msg_len;

    UrDecoder ur;
} qr_decoder_t;

void qr_decoder_init(qr_decoder_t *d, uint8_t *arena, size_t arena_cap);

/** Forget the scan and wipe the decoder and the arena. */
void qr_decoder_reset(qr_decoder_t *d);

/** add_data(): one decoded frame, as raw bytes. */
qr_status_t qr_decoder_add(qr_decoder_t *d, const uint8_t *data, size_t len);

/** get_percent_complete(), 0 to 100. */
int qr_decoder_percent(const qr_decoder_t *d, bool weight_mixed);

bool qr_is_psbt(qr_type_t t);
bool qr_is_seed(qr_type_t t);

/**
 * get_data_psbt(): the PSBT's bytes, into `out`. False if not complete, not
 * a PSBT, or SeedSigner's getter would return None (bad base64 or base32,
 * a zlib stream that does not inflate). Parsing them is psbt-review's job.
 */
bool qr_decoder_psbt(const qr_decoder_t *d, uint8_t *out, size_t cap, size_t *len);

/** An upper bound on the bytes qr_decoder_psbt() can produce, so the
 *  caller can size its buffer to the scan rather than to the worst case.
 *  A zlib BBQr is bounded at 16 times its compressed size. 0 if not a
 *  complete PSBT scan. */
size_t qr_decoder_psbt_max(const qr_decoder_t *d);

/** get_seed_phrase() as word indexes. The checksum is not checked here
 *  (SeedSigner checks it when it loads the seed). */
size_t qr_decoder_seed(const qr_decoder_t *d, uint16_t words[SEED_MAX_WORDS]);

/** get_address() and get_address_type(). */
const char *qr_decoder_address(const qr_decoder_t *d, btc_script_t *script, btc_net_t *net);

/** get_settings_data(): the SettingsQR text, borrowed from the arena. */
bool qr_decoder_settings(const qr_decoder_t *d, const char **text, size_t *len);

/** get_qr_data() for a sign-message request: the path (with "h" written as
 *  "'") and the message, borrowed from the arena. */
bool qr_decoder_sign_message(const qr_decoder_t *d, const char **path, size_t *path_len,
                             const char **msg, size_t *msg_len);

/** A UR's assembled CBOR (bytes, crypto-output, crypto-account). */
const uint8_t *qr_decoder_ur_cbor(const qr_decoder_t *d, size_t *len);

#endif /* LBS_QR_DECODE_H */
