/*
 * Leek BTC Signer - entry point.
 *
 * T0 only proves the skeleton: every environment builds and boots. Drivers
 * arrive from LeekWallet in T2 onwards.
 */
#include <stdio.h>

#include "esp_app_desc.h"
#include "esp_log.h"

#include "build-info.h"

static const char *TAG = "lbs";

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();

    ESP_LOGI(TAG, "Leek BTC Signer %s", app->version);
    ESP_LOGI(TAG, "board=%s display=%s image=%s",
             LBS_BOARD_NAME, LBS_DISPLAY_NAME, LBS_IMAGE_KIND);

#if defined(LBS_QEMU)
    /* Marker the QEMU smoke test waits for. printf, not ESP_LOGI, so it
     * survives any log level and carries no timestamp to match around. */
    printf("LBS-READY\n");
    fflush(stdout);
#endif
}
