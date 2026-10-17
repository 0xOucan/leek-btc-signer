/*
 * Where scanned QR frames come from: one mailbox between the task that
 * reads codes (the camera on the board, the harness under QEMU, a test on
 * the host) and the UI task that decodes them.
 *
 * Single producer, single consumer, lock-free. A frame that arrives while
 * the mailbox is full is dropped: the camera sees the same code again a
 * moment later, and an animated code is a fountain anyway. Frames are raw
 * bytes as the QR reader returned them, NUL bytes included (CompactSeedQR).
 *
 * The slots can hold a scanned seed: the Scan screen drains them on the way
 * in and out (qr_source_drain wipes).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The largest payload a QR code holds: version 40, numeric. */
#define QR_SOURCE_FRAME_MAX 7089
#define QR_SOURCE_SLOTS 3

/* Producer. False if the frame was dropped (too long, or the mailbox full). */
bool qr_source_push(const uint8_t *data, size_t len);

/* Consumer. Copies the oldest frame out; false if there is none. */
bool qr_source_next(uint8_t *out, size_t cap, size_t *len);

/* Consumer. Throw away and wipe every queued frame. */
void qr_source_drain(void);

/* Frames dropped since boot, for the bench. */
unsigned qr_source_dropped(void);
