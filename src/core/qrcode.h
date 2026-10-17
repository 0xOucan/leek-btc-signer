/**
 * QR Code Generator, carried over from LeekWallet.
 * Based on https://github.com/ricmoo/QRCode (MIT License)
 *
 * The mode is chosen from the data as Python's qrcode chooses it for a single
 * segment: numeric if every byte is a digit, alphanumeric if every byte is in
 * that set, bytes otherwise. A scanner returns the same bytes whichever mode
 * carried them, which is what lets a CompactSeedQR go through the same call
 * as a UR frame.
 *
 * The working buffers are on the stack and grow with the version, so the
 * version is capped: QRCODE_MAX_VERSION is 117 modules, the most a 128-pixel
 * panel can show at one pixel per module.
 */

#ifndef QRCODE_H
#define QRCODE_H

#include <stdint.h>
#include <stdbool.h>

/* QR Code structure */
typedef struct {
    uint8_t version;
    uint8_t size;
    uint8_t ecc;
    uint8_t mode;
    uint8_t mask;
    uint8_t *modules;
} QRCode;

/* Error correction levels */
#define ECC_LOW      0
#define ECC_MEDIUM   1
#define ECC_QUARTILE 2
#define ECC_HIGH     3

/**
 * Get the buffer size required for a QR code version
 * @param version QR code version (1-40)
 * @return Buffer size in bytes
 */
uint16_t qrcode_getBufferSize(uint8_t version);

/**
 * Initialize QR code with text data
 * @param qrcode QR code structure to initialize
 * @param modules Buffer for module data (use qrcode_getBufferSize)
 * @param version QR code version (1-40, use 2 for ETH addresses)
 * @param ecc Error correction level (ECC_LOW, ECC_MEDIUM, etc.)
 * @param data Text data to encode
 * @return 0 on success, negative on error
 */
int8_t qrcode_initText(QRCode *qrcode, uint8_t *modules, uint8_t version,
                       uint8_t ecc, const char *data);

/** Largest version encoded; see above. */
#define QRCODE_MAX_VERSION 25

/** Like qrcode_initText(), for `length` bytes that may include NUL. */
int8_t qrcode_initBytes(QRCode *qrcode, uint8_t *modules, uint8_t version,
                        uint8_t ecc, const uint8_t *data, uint16_t length);

/** The same, with the mask pattern chosen by the caller (0-7) rather than
 *  by penalty score; -1 chooses. For comparing against other encoders. */
int8_t qrcode_initBytesMask(QRCode *qrcode, uint8_t *modules, uint8_t version,
                            uint8_t ecc, const uint8_t *data, uint16_t length,
                            int8_t force_mask);

/**
 * Encode at the smallest version up to `max_version` that holds the data, as
 * SeedSigner's qrcode.make(fit=True) does. `modules` must hold
 * qrcode_getBufferSize(max_version). Returns the version, or -1 if none fits.
 */
int8_t qrcode_fit(QRCode *qrcode, uint8_t *modules, uint8_t max_version,
                  uint8_t ecc, const uint8_t *data, uint16_t length);

/**
 * Get a module (pixel) value at position
 * @param qrcode Initialized QR code
 * @param x X coordinate
 * @param y Y coordinate
 * @return true if module is dark, false if light
 */
bool qrcode_getModule(QRCode *qrcode, uint8_t x, uint8_t y);

#endif /* QRCODE_H */
