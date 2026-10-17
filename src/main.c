/*
 * Leek BTC Signer - entry point and UI loop.
 *
 * One task owns the UI: it draws the top screen, pushes the frame to the
 * panel when it changed, and waits for the next input event. Every input
 * source (the poll task on the board, the harness under QEMU) posts to the
 * same queue.
 */
#include <stdio.h>
#include <stdlib.h>

#include "board.h"
#include "btc-init.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "gfx.h"
#include "input-queue.h"
#include "panel.h"
#include "screens.h"
#include "screens-common.h"
#include "secmem.h"
#include "settings.h"
#include "ui.h"
#include "views-power.h"
#include "views-splash.h"
#if defined(LBS_QEMU_HARNESS)
#include "qemu-harness.h"
#endif

static const char *TAG = "lbs";

/* secp256k1 context blinding. Not key material: seeds will come from the
 * health-tested entropy pool (T8). */
static void blinding_rand(void *buf, unsigned len)
{
    esp_fill_random(buf, len);
}

/* Secrets: internal RAM or nothing, never PSRAM (secmem.h). */
static void *internal_alloc(size_t n, bool internal_only)
{
    (void)internal_only;
    return heap_caps_calloc(1, n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static const sec_backend_t SEC_INTERNAL = { internal_alloc, heap_caps_free };

/* ui_working(): a frame on the panel now, before a slow step. */
static void flush_now(void)
{
    if (panel_present()) {
        panel_flush();
    }
#if defined(LBS_QEMU_HARNESS)
    qemu_harness_frame();
#endif
}

static void brightness(int level)
{
    panel_set_brightness(level, QR_BRIGHTNESS_MAX);
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void app_main(void)
{
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "Leek BTC Signer %s", app->version);
    ESP_LOGI(TAG, "board=%s display=%s image=%s",
             BOARD_NAME, GFX_PROFILE_NAME, BOARD_IMAGE);
    screens_set_version(app->version, BOARD_NAME, GFX_PROFILE_NAME, BOARD_IMAGE);

#if defined(LBS_SELFTEST)
    /* Before btc_init: the suites initialise and clean up libwally
     * themselves. */
    extern int selftest_run(void);
    selftest_run();
#endif

    if (!btc_init(blinding_rand)) {
        /* Nothing may sign without a working library: stop here, visibly. */
        ESP_LOGE(TAG, "libwally/secp256k1 failed to initialise");
        abort();
    }

    sec_set_backend(&SEC_INTERNAL);
    settings_reset();
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

    ui_set_clock(now_ms);
    ui_set_flush_hook(flush_now);
    ui_set_restart_hook(esp_restart);   /* RestartView, after the seeds are wiped */
    ui_set_brightness_hook(brightness);
    ui_init(&SCREEN_HOME);
    ui_push(&SCREEN_SPLASH);    /* OpeningSplashView, then the main menu */

#if defined(LBS_QEMU)
    /* The marker the QEMU smoke test waits for. */
    printf("LBS-READY\n");
    fflush(stdout);
#endif

    /* Draw after anything that changed what is shown: a key, or a screen's
     * tick (a scanned frame). A screen with a tick keeps the wait short;
     * every other screen sleeps until a key. */
    bool dirty = true;
    for (;;) {
        if (dirty) {
            ui_render();
            if (panel_present()) {
                panel_flush();
            }
#if defined(LBS_QEMU_HARNESS)
            qemu_harness_screen(ui_top()->name);
            qemu_harness_ack();
#endif
            dirty = false;
        }
        input_ev_t ev;
        if (input_wait(&ev, ui_wants_tick() ? UI_TICK_MS : -1)) {
            ui_input(ev);
            dirty = true;
        }
        if (ui_tick()) {
            dirty = true;
        }
    }
}
