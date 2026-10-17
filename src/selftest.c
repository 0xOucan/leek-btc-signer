/*
 * Self-test image (T21): run the host vector suites on the device.
 *
 * Each suite is the very file sim/ runs on the host, compiled here with its
 * main() renamed. Results go to the console as
 *   SELFTEST <suite> PASS|FAIL
 *   SELFTEST-DONE <failed suites>
 * for scripts/qemu.sh --selftest. Only in the s3cam-qemu-selftest image.
 */
#if defined(LBS_SELFTEST)

#include <stdio.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define SUITES(X) X(test_wally) X(test_btc) X(test_psbt) X(test_cbor) X(test_ur)  \
    X(test_ur_fountain) X(test_ur_decoder) X(test_ur_encoder) X(test_qrcode)   \
    X(test_qr_decode) X(test_qr_encode) X(test_scan) X(test_widgets)      \
    X(test_text_entry) X(test_qr_view)

#define DECLARE(t) int t##_main(void);
SUITES(DECLARE)

#define ENTRY(t) { #t, t##_main },
static const struct {
    const char *name;
    int (*run)(void);
} suites[] = { SUITES(ENTRY) };

static int failed;
static SemaphoreHandle_t done;

/* Suites derive keys and run PBKDF2, so they get a stack of their own
 * rather than the UI task's. */
static void runner(void *arg)
{
    (void)arg;
    for (size_t i = 0; i < sizeof suites / sizeof suites[0]; i++) {
        printf("SELFTEST-HEAP %s free %u largest %u\n", suites[i].name,
               (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
               (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        int rc = suites[i].run();
        printf("SELFTEST %s %s\n", suites[i].name, rc == 0 ? "PASS" : "FAIL");
        failed += rc != 0;
    }
    xSemaphoreGive(done);
    vTaskDelete(NULL);
}

int selftest_run(void)
{
    done = xSemaphoreCreateBinary();
    xTaskCreate(runner, "selftest", 32 * 1024, NULL, 5, NULL);
    xSemaphoreTake(done, portMAX_DELAY);
    printf("SELFTEST-DONE %d\n", failed);
    fflush(stdout);
    return failed;
}

#endif
