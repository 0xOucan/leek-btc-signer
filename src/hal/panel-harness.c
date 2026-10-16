/*
 * QEMU's panel: QEMU has no I2C display model, so frames go out through the
 * harness instead.
 */
#if defined(LBS_QEMU)

#include "panel.h"

#include "qemu-harness.h"

esp_err_t panel_init(void) { return ESP_OK; }

esp_err_t panel_flush(void)
{
#if defined(LBS_QEMU_HARNESS)
    qemu_harness_frame();
#endif
    return ESP_OK;
}

bool panel_present(void) { return true; }

#endif
