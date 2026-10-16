/*
 * Pins and wiring options, one block per board. docs/HARDWARE.md is the
 * human-readable copy of this file and must agree with it.
 *
 * Bring-up flags, set with -D in platformio.ini when the first boot shows
 * they are needed:
 *   BOARD_SH1107_FLIP=1          the 128x128 picture is upside down
 *   BOARD_ENC_REVERSE=1          clockwise scrolls up instead of down
 *   ENCODER_HALF_STEP            a 30/15 encoder needs two clicks per step
 *   BOARD_BUTTONS_ACTIVE_HIGH=1  a key reads high when pressed
 */
#pragma once

#if defined(LBS_BOARD_S3CAM)

#define BOARD_NAME "S3CAM"

/* ESP32-S3-WROOM-1-N16R8 CAM. The camera owns GPIO 4-18 except 14, the
 * microSD 38-40, the octal PSRAM 35-37, USB 19-20. */

/* Panel, I2C: SH1107 128x128 (oled128) or SSD1306 128x64 (oled64). */
#define PIN_I2C_SDA       47
#define PIN_I2C_SCL       21

/* Four-button module V G 1 2 3 4: pressed pulls to GND. */
#define PIN_BTN_UP        1
#define PIN_BTN_DOWN      41
#define PIN_BTN_BACK      14
#define PIN_BTN_OK        42    /* the encoder's SW is wired in parallel */

/* EC11 module. GPIO43/44 are UART0, free because the console is USB
 * Serial/JTAG; the ROM's boot banner still drives GPIO43 for a few ms,
 * which is what the 1 kOhm in series with DT is for. */
#define PIN_ENC_CLK       44    /* header pin labelled RX */
#define PIN_ENC_DT        43    /* header pin labelled TX, via 1 kOhm */

/* microSD, on the board, 1-bit. */
#define PIN_SD_CMD        38
#define PIN_SD_CLK        39
#define PIN_SD_D0         40

#else
#  error "No board selected: set -DLBS_BOARD_S3CAM=1 in platformio.ini"
#endif

#ifndef BOARD_SH1107_FLIP
#  define BOARD_SH1107_FLIP 0
#endif
#ifndef BOARD_ENC_REVERSE
#  define BOARD_ENC_REVERSE 0
#endif
#ifndef BOARD_BUTTONS_ACTIVE_HIGH
#  define BOARD_BUTTONS_ACTIVE_HIGH 0
#endif

#if defined(LBS_QEMU)
#  define BOARD_IMAGE "qemu"
#elif defined(LBS_DEV_BUILD)
#  define BOARD_IMAGE "dev"
#else
#  define BOARD_IMAGE "release"
#endif
