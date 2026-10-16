/*
 * Display profile: the panel's geometry, chosen at build time.
 *
 * Pure C, no ESP-IDF: the host simulator and tests compile this too. Exactly
 * one LBS_DISPLAY_* is defined per build (platformio.ini, or sim/Makefile).
 */
#pragma once

#if (defined(LBS_DISPLAY_OLED128) + defined(LBS_DISPLAY_OLED64) + \
     defined(LBS_DISPLAY_TFT240) + defined(LBS_DISPLAY_TFT320) + \
     defined(LBS_DISPLAY_TFT160)) != 1
#  error "Select exactly one display profile: LBS_DISPLAY_OLED128, _OLED64, _TFT240, _TFT320 or _TFT160"
#endif

#if defined(LBS_DISPLAY_OLED128)
#  define GFX_W 128          /* SH1107 1.5" */
#  define GFX_H 128
#  define GFX_MONO 1
#  define GFX_PROFILE_NAME "oled128"
#elif defined(LBS_DISPLAY_OLED64)
#  define GFX_W 128          /* SSD1306 0.96" */
#  define GFX_H 64
#  define GFX_MONO 1
#  define GFX_PROFILE_NAME "oled64"
#elif defined(LBS_DISPLAY_TFT240)
#  define GFX_W 240          /* ST7789, SeedSigner's own resolution */
#  define GFX_H 240
#  define GFX_MONO 0
#  define GFX_PROFILE_NAME "tft240"
#elif defined(LBS_DISPLAY_TFT320)
#  define GFX_W 320          /* ST7789 landscape */
#  define GFX_H 240
#  define GFX_MONO 0
#  define GFX_PROFILE_NAME "tft320"
#else
#  define GFX_W 128          /* ST7735S portrait */
#  define GFX_H 160
#  define GFX_MONO 0
#  define GFX_PROFILE_NAME "tft160"
#endif

/* Two 4-bit palette indices per byte. */
#define GFX_FB_BYTES ((GFX_W * GFX_H) / 2)
