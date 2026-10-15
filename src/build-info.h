/*
 * Build identity: which board, display profile and image kind this binary is.
 *
 * Exactly one board and one display profile per build, chosen in
 * platformio.ini. A build that names none, or two, fails here rather than
 * running with a guessed pin map.
 */
#pragma once

#if defined(LBS_BOARD_S3CAM)
#  define LBS_BOARD_NAME "S3CAM"
#else
#  error "No board selected: set -DLBS_BOARD_S3CAM=1 in platformio.ini"
#endif

#if (defined(LBS_DISPLAY_OLED) + defined(LBS_DISPLAY_TFT240) + \
     defined(LBS_DISPLAY_TFT320) + defined(LBS_DISPLAY_TFT160)) != 1
#  error "Select exactly one display profile (LBS_DISPLAY_OLED, _TFT240, _TFT320, _TFT160)"
#endif

#if defined(LBS_DISPLAY_OLED)
#  define LBS_DISPLAY_NAME "oled"
#elif defined(LBS_DISPLAY_TFT240)
#  define LBS_DISPLAY_NAME "tft240"
#elif defined(LBS_DISPLAY_TFT320)
#  define LBS_DISPLAY_NAME "tft320"
#else
#  define LBS_DISPLAY_NAME "tft160"
#endif

#if defined(LBS_QEMU)
#  define LBS_IMAGE_KIND "qemu"
#elif defined(LBS_DEV_BUILD)
#  define LBS_IMAGE_KIND "dev"
#else
#  define LBS_IMAGE_KIND "release"
#endif
