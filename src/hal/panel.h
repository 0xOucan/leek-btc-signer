/*
 * The panel behind gfx: one driver per build, chosen by display profile.
 * A missing panel is not fatal; the firmware runs headless and says so.
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t panel_init(void);
/* Push the gfx framebuffer to the glass. */
esp_err_t panel_flush(void);
bool panel_present(void);
/* The QR display screen's brightness keys: `level` of `max`. */
void panel_set_brightness(int level, int max);
