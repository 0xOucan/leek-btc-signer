/*
 * Showing a QR code: SeedSigner's QRDisplayScreen, as a component.
 *
 * A static code is drawn once; an animated one moves to the encoder's next
 * part every QR_VIEW_FRAME_MS, SeedSigner's 5/30 s. The symbol is drawn at
 * the largest whole number of pixels per module that fits the panel with
 * SeedSigner's two-module quiet zone (one, then none, on a panel too small
 * for it), centred, dark modules on the light QR role - on an OLED that is
 * unlit modules on lit pixels, which is what a camera expects.
 *
 * test_qr_view reads the framebuffer itself back with quirc on every
 * display profile: what the panel would show is what a camera decodes.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "qr-encode.h"
#include "qrcode.h"

#define QR_VIEW_FRAME_MS 167        /* SeedSigner: time.sleep(5 / 30.0) */
#define QR_VIEW_PART_MAX 4400       /* a version-25 alphanumeric part, and some */

typedef struct {
    qr_encoder_t *enc;
    uint8_t part[QR_VIEW_PART_MAX];
    size_t part_len;
    QRCode code;
    uint8_t modules[1712];          /* qrcode_getBufferSize(25) */
    bool ok;                        /* the current part fits a symbol */
    uint32_t shown_at;
} qr_view_t;

/* Take the encoder's first part. `now` is ui_now_ms(). */
void qr_view_start(qr_view_t *v, qr_encoder_t *enc, uint32_t now);

/* Next part, if the code is animated and the frame time is up. True if it
 * changed. */
bool qr_view_tick(qr_view_t *v, uint32_t now);

/* Draw the symbol over the whole screen. */
void qr_view_draw(const qr_view_t *v);

/* Pixels per module and quiet zone the symbol is drawn with; 0 if it does
 * not fit at all. */
int qr_view_scale(const qr_view_t *v, int *border);

/* Zero the part and the modules (they may spell a seed). */
void qr_view_wipe(qr_view_t *v);
