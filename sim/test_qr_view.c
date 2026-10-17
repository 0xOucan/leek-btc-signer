/*
 * The QR display component, read back off the framebuffer.
 *
 * For each display profile: draw SeedQR, CompactSeedQR, a static xpub and
 * every frame of an animated UR PSBT exactly as the device draws them, take
 * the framebuffer's pixels (lit or not on an OLED, luminance on a TFT),
 * upscale them as a camera sees a panel, and decode them with quirc. The
 * animated frames go through the port's own decoder too, on a fake clock,
 * and must assemble SeedSigner's PSBT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "btc-init.h"
#include "gfx.h"
#include "qr-decode.h"
#include "qr-view.h"
#include "quirc.h"
#include "ui.h"
#include "vectors/seedsigner_qr.h"

#define N(a) (sizeof(a) / sizeof((a)[0]))
/* Camera pixels per panel pixel. Two on the device build, where quirc's
   working copy of a 3x frame would not fit beside everything else in a
   board without PSRAM. */
#ifdef LBS_HOST
#define UP 3
#else
#define UP 2
#endif

static struct quirc *qr;
static qr_view_t *view;
static qr_encoder_t *enc;
#define BUF_SIZE (16 * 1024)
static uint8_t *buf, *got_screen, *raw, *got;
static uint32_t clock_ms;

static uint32_t fake_clock(void)
{
    return clock_ms;
}

static void rand_fill(void *p, unsigned n)
{
    memset(p, 0x5A, n);
}

static bool light(int x, int y)
{
    const gfx_color_t c = gfx_get(x, y);
    if (GFX_MONO) {
        return gfx_role_lit(c);
    }
    const uint32_t rgb = gfx_role_rgb(c);
    return ((rgb >> 16) & 255) + ((rgb >> 8) & 255) + (rgb & 255) > 3 * 128;
}

/* quirc's reading of the framebuffer, or 0 bytes. */
static size_t read_screen(uint8_t *out, size_t cap)
{
    if (quirc_resize(qr, GFX_W * UP, GFX_H * UP) < 0) {
        return 0;
    }
    int w, h;
    uint8_t *img = quirc_begin(qr, &w, &h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            img[(size_t)y * (size_t)w + (size_t)x] = light(x / UP, y / UP) ? 255 : 0;
        }
    }
    quirc_end(qr);
    struct quirc_data *d = malloc(sizeof *d);
    size_t n = 0;
    for (int i = 0; d && n == 0 && i < quirc_count(qr); i++) {
        struct quirc_code c;
        quirc_extract(qr, i, &c);
        if (quirc_decode(&c, d) == QUIRC_SUCCESS && (size_t)d->payload_len <= cap) {
            memcpy(out, d->payload, (size_t)d->payload_len);
            n = (size_t)d->payload_len;
        }
    }
    free(d);
    return n;
}

static bool shows(const uint8_t *want, size_t len)
{
    qr_view_draw(view);
    const size_t n = read_screen(got_screen, 4096);
    return n == len && memcmp(got_screen, want, len) == 0;
}

static void test_static(void)
{
    printf("== static codes: SeedQR, CompactSeedQR, xpub [%s]\n", GFX_PROFILE_NAME);
    seed_t s;
    for (size_t i = 0; i < 3 && i < N(QR_SEEDQR); i++) {
        CHECK(seed_from_mnemonic(&s, QR_SEEDQR[i].mnemonic, "") == SEED_OK, "seed");

        qr_encode_seedqr(enc, &s, buf, BUF_SIZE);
        qr_view_start(view, enc, 0);
        CHECK(shows((const uint8_t *)QR_SEEDQR[i].standard, strlen(QR_SEEDQR[i].standard)),
              "SeedQR %zu is not on the screen", i);

        qr_encode_compact_seedqr(enc, &s, buf, BUF_SIZE);
        qr_view_start(view, enc, 0);
        uint8_t ent[32];
        const size_t n = seed_entropy(&s, ent);
        CHECK(shows(ent, n), "CompactSeedQR %zu is not on the screen", i);

        CHECK(!qr_view_tick(view, 10000), "a static code moved on");
        qr_view_wipe(view);
        qr_encoder_wipe(enc);
    }
    const qrv_xpub_t *x = &QR_XPUBS[0];
    CHECK(seed_from_mnemonic(&s, x->mnemonic, x->passphrase) == SEED_OK, "seed");
    qr_encode_xpub(enc, &s, x->path, x->net, buf, BUF_SIZE);
    qr_view_start(view, enc, 0);
    CHECK(shows((const uint8_t *)x->xpub_static, strlen(x->xpub_static)), "xpub is not on the screen");
    seed_clear(&s);
}

static void test_animated(void)
{
    printf("== animated UR PSBT, frame by frame off the screen [%s]\n", GFX_PROFILE_NAME);
    uint8_t *arena = malloc(48 * 1024);
    qr_decoder_t *dec = malloc(sizeof *dec);
    unsigned runs = 0;
    for (size_t i = 0; dec && arena && i < N(QR_PSBT_OUT); i++) {
        const qrv_psbt_out_t *v = &QR_PSBT_OUT[i];
        if (v->density == QR_DENSITY_LOW && strlen(v->psbt_hex) > 600) {
            continue;   /* hundreds of frames: the other densities cover it */
        }
        const size_t len = strlen(v->psbt_hex) / 2;
        for (size_t k = 0; k < len; k++) {
            unsigned b;
            sscanf(v->psbt_hex + 2 * k, "%2x", &b);
            raw[k] = (uint8_t)b;
        }
        CHECK(qr_encode_psbt_ur(enc, raw, len, v->density, buf, BUF_SIZE), "encode");
        qr_decoder_init(dec, arena, 48 * 1024);
        clock_ms = 1000;
        qr_view_start(view, enc, ui_now_ms());
        qr_status_t st = QR_STATUS_FALSE;
        int frames = 0;
        uint8_t *frame = got_screen;
        while (st != QR_STATUS_COMPLETE && frames < 400) {
            qr_view_draw(view);
            const size_t n = read_screen(frame, 4096);
            CHECK(n > 0, "PSBT %zu frame %d did not scan off the screen", i, frames);
            if (n == 0) {
                break;
            }
            st = qr_decoder_add(dec, frame, n);
            /* Hold the frame for less than the frame time: nothing moves. */
            clock_ms += QR_VIEW_FRAME_MS - 1;
            CHECK(!qr_view_tick(view, ui_now_ms()), "moved on early");
            clock_ms += 1;
            CHECK(qr_view_tick(view, ui_now_ms()), "did not move on");
            frames++;
        }
        size_t n = 0;
        CHECK(st == QR_STATUS_COMPLETE && qr_decoder_psbt(dec, got, 10 * 1024, &n) && n == len &&
              memcmp(got, raw, len) == 0, "PSBT %zu did not come off the screen whole", i);
        qr_decoder_reset(dec);
        runs++;
    }
    free(dec);
    free(arena);
    printf("   %u animated PSBTs read back off the framebuffer\n", runs);
}

static void test_too_large(void)
{
    printf("== a code too large for the panel says so [%s]\n", GFX_PROFILE_NAME);
    static char big[1200];
    memset(big, 'x', sizeof big - 1);
    qr_encode_text(enc, big, buf, BUF_SIZE);
    qr_view_start(view, enc, 0);
    int border;
    const int scale = qr_view_scale(view, &border);
    const int side = GFX_W < GFX_H ? GFX_W : GFX_H;
    CHECK((scale > 0) == (view->code.size <= side), "scale %d for a %d-module code", scale, view->code.size);
    qr_view_draw(view);
}

int main(void)
{
    btc_init(rand_fill);
    ui_set_clock(fake_clock);
    qr = quirc_new();
    view = test_alloc(sizeof *view);
    enc = test_alloc(sizeof *enc);
    buf = test_alloc(BUF_SIZE);
    got_screen = test_alloc(4096);
    raw = test_alloc(10 * 1024);
    got = test_alloc(10 * 1024);
    if (!qr || !view || !enc || !buf || !got_screen || !raw || !got) {
        CHECK(0, "out of memory");
        if (qr) quirc_destroy(qr);
        test_free_all();
        return CHECK_DONE("test_qr_view");
    }
    test_static();
    test_animated();
    test_too_large();
    quirc_destroy(qr);
    test_free_all();
    char name[64];
    snprintf(name, sizeof name, "test_qr_view [%s]", GFX_PROFILE_NAME);
    return CHECK_DONE(name);
}
