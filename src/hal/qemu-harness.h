/*
 * The QEMU harness (T6): stands in for the panel, the buttons, the encoder
 * and the camera, over UART0. Compiled only when LBS_QEMU_HARNESS is set;
 * scripts/check-release.sh fails a release image that contains it.
 *
 *   host -> device   K <UP|DOWN|OK|BACK|HOLD>   an input event
 *                    Q <hex>                    a decoded QR frame's bytes
 *                    R                          resend the current frame
 *   device -> host   F <w> <h> <base64>         the framebuffer, 4-bit
 *                                               palette indices, on change
 *                    S <screen>                 the screen on top
 *                    N <note>                   what a screen concluded
 *                    E <message>                a harness error
 */
#pragma once

void qemu_harness_start(void);
void qemu_harness_frame(void);
void qemu_harness_screen(const char *name);
void qemu_harness_ack(void);
