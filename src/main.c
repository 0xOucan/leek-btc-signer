/*
 * Leek BTC Signer - entry point and UI loop.
 *
 * One task owns the UI: it draws the top screen, pushes the frame to the
 * panel when it changed, and waits for the next input event. Every input
 * source (the poll task on the board, the harness under QEMU) posts to the
 * same queue.
 */
#include <stdio.h>

#include "board.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "gfx.h"
#include "input-queue.h"
#include "panel.h"
#include "screens.h"
#include "ui.h"
#if defined(LBS_QEMU_HARNESS)
#include "qemu-harness.h"
#endif

static const char *TAG = "lbs";

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "Leek BTC Signer %s", app->version);
    ESP_LOGI(TAG, "board=%s display=%s image=%s",
             BOARD_NAME, GFX_PROFILE_NAME, BOARD_IMAGE);
    screens_set_version(app->version, BOARD_NAME, GFX_PROFILE_NAME, BOARD_IMAGE);

    input_queue_init();
    /* Headless is a working state, not a failure: a loose wire should give
     * a dark screen and a log line, not a board that looks dead. */
    if (panel_init() != ESP_OK) {
        ESP_LOGW(TAG, "display absent, continuing headless");
    }
#if defined(LBS_QEMU_HARNESS)
    qemu_harness_start();
#elif !defined(LBS_QEMU)
    input_hw_start();
#endif

    ui_init(&SCREEN_HOME);

#if defined(LBS_QEMU)
    /* The marker the QEMU smoke test waits for. */
    printf("LBS-READY\n");
    fflush(stdout);
#endif

    for (;;) {
        ui_render();
        if (panel_present()) {
            panel_flush();
        }
#if defined(LBS_QEMU_HARNESS)
        qemu_harness_screen(ui_top()->name);
        qemu_harness_ack();
#endif
        input_ev_t ev;
        if (input_wait(&ev, -1)) {
            ui_input(ev);
        }
    }
}
