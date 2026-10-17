/*
 * QEMU harness. See qemu-harness.h.
 */
#if defined(LBS_QEMU_HARNESS)

#include "qemu-harness.h"

#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gfx.h"
#include "input-queue.h"
#include "qr-source.h"
#include "ui.h"

static uint8_t last_frame[GFX_FB_BYTES];
static bool sent_once;
static bool resend;
static const char *last_screen;

static void put_base64(const uint8_t *in, size_t len)
{
    static const char T[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char out[4];
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < len) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < len) v |= in[i + 2];
        out[0] = T[(v >> 18) & 63];
        out[1] = T[(v >> 12) & 63];
        out[2] = i + 1 < len ? T[(v >> 6) & 63] : '=';
        out[3] = i + 2 < len ? T[v & 63] : '=';
        fwrite(out, 1, 4, stdout);
    }
}

void qemu_harness_frame(void)
{
    const uint8_t *fb = gfx_framebuffer();
    if (sent_once && !resend && memcmp(fb, last_frame, sizeof last_frame) == 0) {
        return;
    }
    memcpy(last_frame, fb, sizeof last_frame);
    sent_once = true;
    resend = false;
    printf("F %d %d ", GFX_W, GFX_H);
    put_base64(fb, GFX_FB_BYTES);
    printf("\n");
    fflush(stdout);
}

void qemu_harness_screen(const char *name)
{
    if (name != last_screen || resend) {
        last_screen = name;
        printf("S %s\n", name);
        fflush(stdout);
    }
}

void qemu_harness_ack(void)
{
    /* Frames take about a second to cross the emulated UART, so a test that
     * waited a fixed time read stale frames. It waits for this instead. */
    printf("A\n");
    fflush(stdout);
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* "Q <hex>": the bytes a QR reader would have returned. Hex, so binary
 * payloads (CompactSeedQR) cross the text protocol intact. */
static void inject_qr(const char *hex)
{
    static uint8_t frame[QR_SOURCE_FRAME_MAX];
    size_t n = 0;
    for (; hex[0] && hex[1] && n < sizeof frame; hex += 2) {
        const int hi = hexval(hex[0]), lo = hexval(hex[1]);
        if (hi < 0 || lo < 0) {
            printf("E bad hex in Q\n");
            return;
        }
        frame[n++] = (uint8_t)(hi << 4 | lo);
    }
    if (!qr_source_push(frame, n)) {
        printf("E QR frame dropped\n");
    }
}

static void note(const char *text)
{
    printf("N %s\n", text);
    fflush(stdout);
}

static void reader_task(void *arg)
{
    (void)arg;
    /* A Q line carries up to a version-40 payload as hex. */
    static char line[2 * QR_SOURCE_FRAME_MAX + 8];
    for (;;) {
        if (fgets(line, sizeof line, stdin) == NULL) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == 'K' && line[1] == ' ') {
            input_ev_t ev = input_from_name(line + 2);
            if (ev == IN_NONE) {
                printf("E unknown key %s\n", line + 2);
            } else {
                input_post(ev);
            }
        } else if (line[0] == 'R') {
            resend = true;
            qemu_harness_frame();
        } else if (line[0] == 'Q' && line[1] == ' ') {
            inject_qr(line + 2);
        } else if (line[0] != '\0') {
            printf("E unknown command %c\n", line[0]);
        }
        fflush(stdout);
    }
}

void qemu_harness_start(void)
{
    /* stdin needs the UART driver behind it; the default console is
     * write-only and non-blocking. */
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0));
    uart_vfs_dev_use_driver(UART_NUM_0);
    setvbuf(stdin, NULL, _IONBF, 0);
    ui_set_note_hook(note);
    xTaskCreate(reader_task, "harness", 4096, NULL, 5, NULL);
}

#endif
