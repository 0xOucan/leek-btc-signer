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

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

int test_wally_main(void);
int test_btc_main(void);

static const struct {
    const char *name;
    int (*run)(void);
} suites[] = {
    { "test_wally", test_wally_main },
    { "test_btc", test_btc_main },
};

static int failed;
static SemaphoreHandle_t done;

/* Suites derive keys and run PBKDF2, so they get a stack of their own
 * rather than the UI task's. */
static void runner(void *arg)
{
    (void)arg;
    for (size_t i = 0; i < sizeof suites / sizeof suites[0]; i++) {
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
