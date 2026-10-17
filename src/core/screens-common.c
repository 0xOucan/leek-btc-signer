/*
 * Screens every flow uses: SeedSigner's status screens (Success, Warning,
 * DireWarning, Error) with a body that pages when it is long, the QR display
 * screen, and the "working" frame shown during a slow step.
 */
#include "screens-common.h"

#include <stdio.h>
#include <string.h>

#include "qr-view.h"
#include "theme.h"

/* ---- Hooks the platform sets ------------------------------------------ */

static void (*flush_hook)(void);
static void (*brightness_hook)(int level);

void ui_set_flush_hook(void (*fn)(void))
{
    flush_hook = fn;
}

void ui_set_brightness_hook(void (*fn)(int level))
{
    brightness_hook = fn;
}

void ui_working(const char *text)
{
    gfx_clear(C_BG);
    const int y = (GFX_H - gfx_font_height(THEME.body)) / 2;
    gfx_text_centered(0, GFX_W, y, text ? text : "Working...", THEME.body, C_FG);
    if (flush_hook) {
        flush_hook();
    }
}

/* ---- Wrapping with paging --------------------------------------------- */

#define NOTICE_LINES_MAX 40
#define NOTICE_TEXT_MAX 600

/* Cut `text` into lines that fit the screen, at spaces and newlines. */
static int wrap(const char *text, const char *lines[], int len[], int max)
{
    const int avail = GFX_W - 2 * THEME.pad - 4;
    int n = 0;
    const char *p = text;
    while (*p && n < max) {
        int fit = 0, brk = -1;
        while (p[fit] && p[fit] != '\n') {
            char tmp[96];
            const int k = fit + 1 < (int)sizeof tmp ? fit + 1 : (int)sizeof tmp - 1;
            memcpy(tmp, p, (size_t)k);
            tmp[k] = '\0';
            if (gfx_text_width(tmp, THEME.body) > avail) {
                break;
            }
            if (p[fit] == ' ') {
                brk = fit;
            }
            fit++;
        }
        int take = fit;
        if (p[fit] && p[fit] != '\n' && brk > 0) {
            take = brk;
        }
        if (take == 0 && p[0] != '\n') {
            take = 1;       /* a word wider than the screen: cut it */
        }
        lines[n] = p;
        len[n] = take;
        n++;
        p += take;
        if (*p == ' ' || *p == '\n') {
            p++;
        }
    }
    return n;
}

/* ---- Status screens --------------------------------------------------- */

static struct {
    notice_t n;
    char title[24], headline[48], body[NOTICE_TEXT_MAX], button[24];
    const char *lines[NOTICE_LINES_MAX];
    int len[NOTICE_LINES_MAX];
    int n_lines, top;
} notice;

static gfx_color_t notice_color(notice_kind_t k)
{
    switch (k) {
    case NOTICE_SUCCESS: return C_OK;
    case NOTICE_WARNING: return C_WARN;
    case NOTICE_DIRE:    return C_DIRE;
    case NOTICE_ERROR:   return C_ERROR;
    default:             return C_INFO;
    }
}

static const char *default_title(notice_kind_t k)
{
    switch (k) {
    case NOTICE_SUCCESS: return "Success!";
    case NOTICE_WARNING:
    case NOTICE_DIRE:    return "Caution";
    case NOTICE_ERROR:   return "Error";
    default:             return "Info";
    }
}

static int body_rows(int body_y)
{
    const int button_h = THEME.row_box_h + THEME.pad;
    return (GFX_H - body_y - button_h) / THEME.line_h;
}

static int body_top(void)
{
    int y = THEME.top_h + THEME.pad;
    if (notice.headline[0]) {
        y += THEME.line_h + 2;
    }
    return y;
}

static void notice_render(const screen_t *s)
{
    (void)s;
    const gfx_color_t c = notice_color(notice.n.kind);
    ui_draw_title(notice.title);

    /* SeedSigner's warning edges: colour on a TFT, border weight on an OLED. */
    const bool edged = notice.n.kind == NOTICE_WARNING || notice.n.kind == NOTICE_DIRE ||
                       notice.n.kind == NOTICE_ERROR;
    if (edged) {
        const int w = notice.n.kind == NOTICE_WARNING ? 1 : 2;
        for (int i = 0; i < w; i++) {
            gfx_rect(i, THEME.top_h + i, GFX_W - 2 * i, GFX_H - THEME.top_h - 2 * i, c);
        }
    }

    int y = THEME.top_h + THEME.pad;
    if (notice.headline[0]) {
        if (GFX_MONO && edged) {
            /* An inverted bar stands in for the coloured headline. */
            gfx_fill_rect(2, y - 1, GFX_W - 4, THEME.line_h, C_SEL_BG);
            gfx_text_centered(0, GFX_W, y, notice.headline, THEME.body, C_SEL_FG);
        } else {
            gfx_text_centered(0, GFX_W, y, notice.headline, THEME.body, c);
        }
        y += THEME.line_h + 2;
    }

    const int rows = body_rows(y);
    for (int i = 0; i < rows && notice.top + i < notice.n_lines; i++) {
        char line[96];
        snprintf(line, sizeof line, "%.*s", notice.len[notice.top + i], notice.lines[notice.top + i]);
        gfx_text(THEME.pad + 2, y + i * THEME.line_h, line, THEME.body, C_FG);
    }
    /* More above or below: a mark at the right edge. */
    if (notice.top > 0) {
        gfx_text(GFX_W - THEME.pad - 6, y, "^", THEME.body, C_MUTED);
    }
    if (notice.top + rows < notice.n_lines) {
        gfx_text(GFX_W - THEME.pad - 6, y + (rows - 1) * THEME.line_h, "v", THEME.body, C_MUTED);
    }

    /* The one button, selected, as SeedSigner's ButtonListScreen draws it. */
    const char *label = notice.button;
    const int by = GFX_H - THEME.row_box_h - (edged ? 3 : 1);
    gfx_fill_rect(THEME.pad + 2, by, GFX_W - 2 * THEME.pad - 4, THEME.row_box_h, C_SEL_BG);
    gfx_text_centered(0, GFX_W, by + (THEME.row_box_h - gfx_font_height(THEME.body)) / 2,
                      label, THEME.body, C_SEL_FG);
}

static bool notice_input(const screen_t *s, input_ev_t ev)
{
    (void)s;
    const int rows = body_rows(body_top());
    if (ev == IN_UP) {
        if (notice.top > 0) notice.top--;
        return true;
    }
    if (ev == IN_DOWN) {
        if (notice.top + rows < notice.n_lines) notice.top++;
        return true;
    }
    if (ev == IN_OK) {
        void (*next)(void) = notice.n.on_ok;
        ui_pop();
        if (next) {
            next();
        }
        return true;
    }
    return false;
}

static const screen_t NOTICE = {
    .name = "NOTICE", .render = notice_render, .input = notice_input,
};

void ui_notice(const notice_t *n)
{
    notice.n = *n;
    snprintf(notice.title, sizeof notice.title, "%s", n->title ? n->title : default_title(n->kind));
    snprintf(notice.headline, sizeof notice.headline, "%s", n->headline ? n->headline : "");
    snprintf(notice.body, sizeof notice.body, "%s", n->body ? n->body : "");
    snprintf(notice.button, sizeof notice.button, "%s",
             n->button ? n->button : (n->kind == NOTICE_WARNING || n->kind == NOTICE_DIRE)
                                         ? "I understand" : "OK");
    notice.n_lines = wrap(notice.body, notice.lines, notice.len, NOTICE_LINES_MAX);
    notice.top = 0;
    ui_push(&NOTICE);
}

int ui_notice_lines(void)
{
    return notice.n_lines;
}

/* ---- QR display ------------------------------------------------------- */

static struct {
    qr_view_t view;
    qr_encoder_t *enc;
    void (*on_leave)(void);
    int brightness;
} qrd;

static void qr_enter(const screen_t *s)
{
    (void)s;
    qr_view_start(&qrd.view, qrd.enc, ui_now_ms());
    if (brightness_hook) {
        brightness_hook(qrd.brightness);
    }
}

static void qr_render(const screen_t *s)
{
    (void)s;
    qr_view_draw(&qrd.view);
}

static bool qr_tick(const screen_t *s)
{
    (void)s;
    return qr_view_tick(&qrd.view, ui_now_ms());
}

static bool qr_input(const screen_t *s, input_ev_t ev)
{
    (void)s;
    /* SeedSigner: up and down make the code brighter and darker. */
    if (ev == IN_UP || ev == IN_DOWN) {
        qrd.brightness += ev == IN_UP ? 1 : -1;
        if (qrd.brightness < 0) qrd.brightness = 0;
        if (qrd.brightness > QR_BRIGHTNESS_MAX) qrd.brightness = QR_BRIGHTNESS_MAX;
        if (brightness_hook) {
            brightness_hook(qrd.brightness);
        }
        ui_note("brightness %d", qrd.brightness);
        return true;
    }
    if (ev == IN_OK) {
        ui_pop();       /* SeedSigner leaves on its right or select key */
        return true;
    }
    return false;
}

static void qr_leave(const screen_t *s)
{
    (void)s;
    qr_view_wipe(&qrd.view);
    if (brightness_hook) {
        brightness_hook(QR_BRIGHTNESS_MAX);
    }
    void (*done)(void) = qrd.on_leave;
    qrd.on_leave = NULL;
    if (done) {
        done();
    }
}

static const screen_t QR_DISPLAY = {
    .name = "QR_DISPLAY", .enter = qr_enter, .render = qr_render, .input = qr_input,
    .leave = qr_leave, .tick = qr_tick,
};

void ui_show_qr(qr_encoder_t *enc, void (*on_leave)(void))
{
    qrd.enc = enc;
    qrd.on_leave = on_leave;
    if (qrd.brightness == 0) {
        qrd.brightness = QR_BRIGHTNESS_MAX;
    }
    ui_push(&QR_DISPLAY);
}
