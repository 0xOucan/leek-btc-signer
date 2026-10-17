/* The QR display component. See qr-view.h. */
#include "qr-view.h"

#include <string.h>

#include "gfx.h"
#include "wally_core.h"

static void load(qr_view_t *v)
{
    v->part_len = qr_encoder_next_part(v->enc, v->part, sizeof v->part);
    v->ok = v->part_len > 0 &&
            qrcode_fit(&v->code, v->modules, QRCODE_MAX_VERSION, ECC_LOW, v->part,
                       (uint16_t)v->part_len) > 0;
}

void qr_view_start(qr_view_t *v, qr_encoder_t *enc, uint32_t now)
{
    memset(v, 0, sizeof *v);
    v->enc = enc;
    load(v);
    v->shown_at = now;
}

bool qr_view_tick(qr_view_t *v, uint32_t now)
{
    if (v->enc == NULL || qr_encoder_seq_len(v->enc) <= 1 ||
        now - v->shown_at < QR_VIEW_FRAME_MS) {
        return false;
    }
    load(v);
    v->shown_at = now;
    return true;
}

int qr_view_scale(const qr_view_t *v, int *border)
{
    const int side = GFX_W < GFX_H ? GFX_W : GFX_H;
    for (int b = 2; b >= 0; b--) {
        const int scale = side / (v->code.size + 2 * b);
        if (scale >= 1) {
            *border = b;
            return scale;
        }
    }
    *border = 0;
    return 0;
}

void qr_view_draw(const qr_view_t *v)
{
    gfx_clear(C_BG);
    int border;
    const int scale = v->ok ? qr_view_scale(v, &border) : 0;
    if (scale == 0) {
        gfx_text_centered(0, GFX_W, GFX_H / 2 - 4, "QR too large", &FONT_5X7, C_ERROR);
        return;
    }
    const int span = (v->code.size + 2 * border) * scale;
    const int x0 = (GFX_W - span) / 2, y0 = (GFX_H - span) / 2;
    gfx_fill_rect(x0, y0, span, span, C_QR_LIGHT);
    for (int y = 0; y < v->code.size; y++) {
        for (int x = 0; x < v->code.size; x++) {
            if (qrcode_getModule((QRCode *)&v->code, (uint8_t)x, (uint8_t)y)) {
                gfx_fill_rect(x0 + (border + x) * scale, y0 + (border + y) * scale,
                              scale, scale, C_BG);
            }
        }
    }
}

void qr_view_wipe(qr_view_t *v)
{
    wally_bzero(v, sizeof *v);
}
