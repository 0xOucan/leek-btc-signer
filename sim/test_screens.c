/*
 * The shared screens on every display profile: status screens that page a
 * long body, the QR display screen (read back off the framebuffer with
 * quirc, animated by the screen's own tick), and the working frame.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "btc-init.h"
#include "gfx.h"
#include "quirc.h"
#include "screens.h"
#include "screens-common.h"
#include "ui.h"

static uint32_t clock_ms;
static uint32_t fake_clock(void) { return clock_ms; }
static void rand_fill(void *p, unsigned n) { memset(p, 0x5A, n); }

static int oks, leaves, flushes, brightness = -1;
static char note_buf[160];
static void on_ok(void) { oks++; }
static void on_leave(void) { leaves++; }
static void on_flush(void) { flushes++; }
static void on_brightness(int level) { brightness = level; }
static void note(const char *n) { snprintf(note_buf, sizeof note_buf, "%s", n); }

static const char *top(void) { return ui_top()->name; }

static int lit_pixels(void)
{
    int n = 0;
    for (int y = 0; y < GFX_H; y++)
        for (int x = 0; x < GFX_W; x++)
            n += gfx_get(x, y) != C_BG;
    return n;
}

static void test_notices(void)
{
    printf("== status screens [%s]\n", GFX_PROFILE_NAME);
    static const notice_kind_t kinds[] = { NOTICE_INFO, NOTICE_SUCCESS, NOTICE_WARNING,
                                           NOTICE_DIRE, NOTICE_ERROR };
    char body[600];
    size_t k = 0;
    for (int i = 0; i < 40 && k + 20 < sizeof body; i++) {
        k += (size_t)snprintf(body + k, sizeof body - k, "line %d of a long warning. ", i);
    }
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
        ui_init(&SCREEN_HOME);
        oks = 0;
        const notice_t n = { kinds[i], NULL, "Privacy Leak!", body, NULL, on_ok };
        ui_notice(&n);
        CHECK(strcmp(top(), "NOTICE") == 0, "notice did not open");
        ui_render();
        const int before = lit_pixels();
        CHECK(before > 0, "notice %zu drew nothing", i);
        CHECK(ui_notice_lines() > 3, "the body did not wrap");
        /* DOWN pages through the body, then stops. */
        for (int d = 0; d < 200; d++) ui_input(IN_DOWN);
        ui_render();
        CHECK(lit_pixels() > 0, "paged notice drew nothing");
        ui_input(IN_OK);
        CHECK(strcmp(top(), "MainMenuView") == 0 && oks == 1, "OK did not close and continue");
    }
    /* A short body and BACK: closes without the continuation. */
    oks = 0;
    const notice_t s = { NOTICE_ERROR, NULL, NULL, "Short.", "Back", on_ok };
    ui_notice(&s);
    ui_input(IN_BACK);
    CHECK(strcmp(top(), "MainMenuView") == 0 && oks == 0, "BACK should close without continuing");
}

static size_t read_screen(struct quirc *q, uint8_t *out, size_t cap)
{
    const int up = 3;
    quirc_resize(q, GFX_W * up, GFX_H * up);
    int w, h;
    uint8_t *img = quirc_begin(q, &w, &h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const gfx_color_t c = gfx_get(x / up, y / up);
            const uint32_t rgb = gfx_role_rgb(c);
            const bool light = GFX_MONO ? gfx_role_lit(c)
                                        : ((rgb >> 16) & 255) + ((rgb >> 8) & 255) + (rgb & 255) > 384;
            img[(size_t)y * (size_t)w + (size_t)x] = light ? 255 : 0;
        }
    quirc_end(q);
    struct quirc_data *d = malloc(sizeof *d);
    size_t n = 0;
    for (int i = 0; d && !n && i < quirc_count(q); i++) {
        struct quirc_code c;
        quirc_extract(q, i, &c);
        if (quirc_decode(&c, d) == QUIRC_SUCCESS && (size_t)d->payload_len <= cap) {
            memcpy(out, d->payload, (size_t)d->payload_len);
            n = (size_t)d->payload_len;
        }
    }
    free(d);
    return n;
}

static void test_qr_screen(void)
{
    printf("== QR display screen [%s]\n", GFX_PROFILE_NAME);
    struct quirc *q = quirc_new();
    qr_encoder_t *enc = test_alloc(sizeof *enc);
    uint8_t *buf = test_alloc(4096), *got = test_alloc(4096);
    seed_t *s = test_alloc(sizeof *s);
    if (!q || !enc || !buf || !got || !s) {
        CHECK(0, "out of memory");
        if (q) quirc_destroy(q);
        return;
    }
    CHECK(seed_from_mnemonic(s, "abandon abandon abandon abandon abandon abandon abandon "
                                "abandon abandon abandon abandon about", "") == SEED_OK, "seed");
    ui_init(&SCREEN_HOME);
    leaves = 0;
    qr_encode_seedqr(enc, s, buf, 4096);
    ui_show_qr("TestQRDisplayView", enc, on_leave);
    CHECK(strcmp(top(), "TestQRDisplayView") == 0, "QR screen did not open");
    ui_render();
    const char *want = "000000000000000000000000000000000000000000000003";
    size_t n = read_screen(q, got, 4096);
    CHECK(n == strlen(want) && memcmp(got, want, n) == 0, "the SeedQR on screen reads \"%.*s\"",
          (int)n, got);

    /* Brightness: clamped both ways, and back to full on the way out. */
    for (int i = 0; i < 9; i++) ui_input(IN_DOWN);
    CHECK(brightness == 0, "brightness %d after DOWN x9", brightness);
    for (int i = 0; i < 9; i++) ui_input(IN_UP);
    CHECK(brightness == QR_BRIGHTNESS_MAX, "brightness %d after UP x9", brightness);
    ui_input(IN_DOWN);
    CHECK(strcmp(note_buf, "brightness 3") == 0, "note: %s", note_buf);
    ui_input(IN_BACK);
    CHECK(strcmp(top(), "MainMenuView") == 0 && leaves == 1, "BACK did not leave the QR screen");
    CHECK(brightness == QR_BRIGHTNESS_MAX, "brightness not restored");

    /* Animated: the screen's tick moves to the next part on time. */
    qr_encode_xpub_ur(enc, s, "m/84h/0h/0h", BTC_NET_MAIN, QR_DENSITY_LOW, buf, 4096);
    CHECK(qr_encoder_seq_len(enc) > 2, "expected an animated code");
    clock_ms = 5000;
    ui_show_qr("TestQRDisplayView", enc, on_leave);
    ui_render();
    size_t a = read_screen(q, got, 4096);
    char first[512];
    snprintf(first, sizeof first, "%.*s", (int)a, got);
    CHECK(strncmp(first, "UR:CRYPTO-ACCOUNT/1-", 20) == 0, "first frame: %s", first);
    clock_ms += 100;
    CHECK(!ui_tick(), "moved on before the frame time");
    clock_ms += 100;
    CHECK(ui_tick(), "did not move on after the frame time");
    ui_render();
    a = read_screen(q, got, 4096);
    CHECK(a > 20 && strncmp((char *)got, "UR:CRYPTO-ACCOUNT/2-", 20) == 0, "second frame: %.*s",
          (int)a, got);
    ui_input(IN_OK);
    CHECK(strcmp(top(), "MainMenuView") == 0 && leaves == 2, "OK did not leave");

    seed_clear(s);
    qr_encoder_wipe(enc);
    quirc_destroy(q);
}

static void test_working(void)
{
    printf("== working frame [%s]\n", GFX_PROFILE_NAME);
    flushes = 0;
    ui_working(NULL);
    CHECK(flushes == 1 && lit_pixels() > 0, "working frame not drawn and flushed");
}

int main(void)
{
    btc_init(rand_fill);
    ui_set_clock(fake_clock);
    ui_set_flush_hook(on_flush);
    ui_set_brightness_hook(on_brightness);
    ui_set_note_hook(note);
    test_notices();
    test_qr_screen();
    test_working();
    test_free_all();
    char name[64];
    snprintf(name, sizeof name, "test_screens [%s]", GFX_PROFILE_NAME);
    return CHECK_DONE(name);
}
