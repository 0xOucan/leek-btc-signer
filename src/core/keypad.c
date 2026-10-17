/*
 * The keypad. See keypad.h.
 */
#include "keypad.h"

#include <stdio.h>
#include <string.h>

#include "theme.h"
#include "ui.h"
#include "wally_core.h"

#define PAD_MAX 100

static struct {
    keypad_spec_t spec;
    char text[PAD_MAX + 1];
    size_t len;
    int sel;
} pad;

/* The keys, then DEL, then OK when the entry has no fixed length. */
static int n_keys(void)
{
    return (int)strlen(pad.spec.keys) + 1 + (pad.spec.total == 0);
}

static const char *label(int k, char buf[4])
{
    const int keys = (int)strlen(pad.spec.keys);
    if (k < keys) {
        buf[0] = pad.spec.keys[k];
        buf[1] = '\0';
        return buf;
    }
    return k == keys ? "DEL" : "OK";
}

static void pad_render(const screen_t *s)
{
    (void)s;
    char title[32];
    if (pad.spec.total) {
        const size_t at = pad.len < pad.spec.total ? pad.len + 1 : pad.spec.total;
        snprintf(title, sizeof title, "%s %u/%u", pad.spec.title, (unsigned)at, (unsigned)pad.spec.total);
    } else {
        snprintf(title, sizeof title, "%s", pad.spec.title);
    }
    ui_draw_title(title);
    int y = THEME.top_h + THEME.pad;
    /* The entry so far: the tail that fits. */
    const int fit = gfx_text_fit(pad.text, THEME.body, GFX_W - 2 * THEME.pad - 6);
    const char *tail = pad.text + (pad.len > (size_t)fit ? pad.len - (size_t)fit : 0);
    const int x = gfx_text(THEME.pad, y, tail, THEME.body, C_FG);
    gfx_text(x, y, "_", THEME.body, C_ACCENT);
    y += THEME.line_h * 2;
    /* The keys, in as many rows as they need. */
    const int n = n_keys();
    const int kw = gfx_text_width("DEL", THEME.body) + 6;
    int cols = (GFX_W - 2 * THEME.pad) / kw;
    if (cols > n) cols = n;
    if (cols < 1) cols = 1;
    const int w = (GFX_W - 2 * THEME.pad) / cols;
    const int row_h = gfx_font_height(THEME.body) + 6;
    for (int k = 0; k < n; k++) {
        char buf[4];
        const int kx = THEME.pad + (k % cols) * w, ky = y + (k / cols) * row_h;
        if (k == pad.sel) {
            gfx_fill_rect(kx, ky - 2, w - 1, gfx_font_height(THEME.body) + 4, C_SEL_BG);
        }
        gfx_text_centered(kx, w - 1, ky, label(k, buf), THEME.body, k == pad.sel ? C_SEL_FG : C_FG);
    }
    const int below = y + ((n + cols - 1) / cols) * row_h + THEME.pad;
    if (pad.spec.hint && below + gfx_font_height(THEME.body) <= GFX_H) {
        ui_draw_wrapped(below, pad.spec.hint, C_MUTED);
    }
}

static bool pad_input(const screen_t *s, input_ev_t ev)
{
    (void)s;
    const int n = n_keys(), keys = (int)strlen(pad.spec.keys);
    switch (ev) {
    case IN_UP:   pad.sel = (pad.sel + n - 1) % n; return true;
    case IN_DOWN: pad.sel = (pad.sel + 1) % n; return true;
    case IN_OK:
        if (pad.sel == keys) {              /* DEL */
            if (pad.len > 0) {
                pad.text[--pad.len] = '\0';
            }
            return true;
        }
        if (pad.sel > keys) {               /* OK */
            pad.spec.done(pad.text);
            return true;
        }
        if (pad.len < PAD_MAX && (pad.spec.total == 0 || pad.len < pad.spec.total)) {
            pad.text[pad.len++] = pad.spec.keys[pad.sel];
            pad.text[pad.len] = '\0';
        }
        if (pad.spec.total && pad.len == pad.spec.total) {
            pad.spec.done(pad.text);
        }
        return true;
    default:
        return false;       /* BACK leaves, as SeedSigner's back arrow */
    }
}

static void pad_clear(void)
{
    wally_bzero(pad.text, sizeof pad.text);
    pad.len = 0;
}

/* Back on the keypad from the view it finished into: a new entry, as
   SeedSigner runs the View again. */
static void pad_resume(const screen_t *s)
{
    (void)s;
    pad_clear();
    if (pad.spec.initial) {
        snprintf(pad.text, sizeof pad.text, "%s", pad.spec.initial);
        pad.len = strlen(pad.text);
    }
}

static void pad_leave(const screen_t *s)
{
    (void)s;
    pad_clear();
    gfx_wipe();
}

static screen_t PAD = {
    .render = pad_render, .input = pad_input, .resume = pad_resume, .leave = pad_leave,
};

void keypad_open(const keypad_spec_t *spec)
{
    pad_clear();
    pad.spec = *spec;
    if (pad.spec.total > PAD_MAX) {
        pad.spec.total = PAD_MAX;
    }
    if (spec->initial) {
        snprintf(pad.text, sizeof pad.text, "%s", spec->initial);
        pad.len = strlen(pad.text);
    }
    pad.sel = 0;
    PAD.name = spec->name;
    ui_push(&PAD);
}

const char *views_keypad_text(void)
{
    return pad.text;
}

int views_keypad_sel(void)
{
    return pad.sel;
}
