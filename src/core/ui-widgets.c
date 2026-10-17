/*
 * Title bar, lists, wrapped text, and the menu and message screen types.
 * Everything positions itself from THEME, never from fixed pixels.
 */
#include "ui.h"

#include <string.h>

#include "theme.h"

void ui_draw_title(const char *title)
{
    const gfx_font_t *f = THEME.title;
    const int avail = GFX_W - 2 * THEME.pad;
    char buf[48];
    int n = gfx_text_fit(title, f, avail);
    if (n > (int)sizeof buf - 1) {
        n = (int)sizeof buf - 1;
    }
    memcpy(buf, title, (size_t)n);
    buf[n] = '\0';
    gfx_text_centered(0, GFX_W, THEME.title_y, buf, f, C_FG);
#if GFX_MONO
    gfx_hline(0, THEME.top_h - 1, GFX_W, C_FG);
#endif
}

/* A small solid triangle, pointing up or down, for "more this way". */
static void scroll_mark(int x, int y, bool up)
{
    for (int i = 0; i < 3; i++) {
        int row = up ? y + i : y + 2 - i;
        gfx_hline(x - i, row, 2 * i + 1, C_MUTED);
    }
}

/* Rounded corners for colour buttons: clear the corner pixels. */
static void round_corners(int x, int y, int w, int h)
{
    static const int8_t cut[] = { 3, 1, 1 };     /* pixels cut per corner row */
    for (int r = 0; r < 3; r++) {
        int c = cut[r];
        gfx_hline(x, y + r, c, C_BG);
        gfx_hline(x + w - c, y + r, c, C_BG);
        gfx_hline(x, y + h - 1 - r, c, C_BG);
        gfx_hline(x + w - c, y + h - 1 - r, c, C_BG);
    }
}

static void draw_list(int y0, int rows, const char *const *labels, int n, int sel, int *top);

void ui_draw_list(const char *const *labels, int n, int sel, int *top)
{
    draw_list(THEME.row_y0, theme_rows(), labels, n, sel, top);
}

void ui_draw_list_from(int y0, const char *const *labels, int n, int sel, int *top)
{
    int rows = (GFX_H - y0) / THEME.row_h;
    draw_list(y0, rows < 1 ? 1 : rows, labels, n, sel, top);
}

static void draw_list(int y0, int rows, const char *const *labels, int n, int sel, int *top)
{
    if (sel < *top) {
        *top = sel;
    }
    if (sel >= *top + rows) {
        *top = sel - rows + 1;
    }
    if (*top < 0) {
        *top = 0;
    }

    const gfx_font_t *f = THEME.body;
    const int fh = gfx_font_height(f);
    const bool more_above = *top > 0;
    const bool more_below = *top + rows < n;
    /* Room on the right for the scroll marks on monochrome panels. */
    const int mark_w = (GFX_MONO && (more_above || more_below)) ? 6 : 0;

    for (int i = 0; i < rows && *top + i < n; i++) {
        const int idx = *top + i;
        const bool on = idx == sel;
        const int y = y0 + i * THEME.row_h;
        int x, w;

        if (THEME.row_style == ROW_BUTTON) {
            x = THEME.pad;
            w = GFX_W - 2 * THEME.pad;
            gfx_fill_rect(x, y, w, THEME.row_box_h, on ? C_SEL_BG : C_BUTTON_BG);
            round_corners(x, y, w, THEME.row_box_h);
        } else {
            x = 0;
            w = GFX_W - mark_w;
            if (on) {
                gfx_fill_rect(x, y, w, THEME.row_box_h, C_SEL_BG);
            }
        }

        const int tx = x + THEME.pad + 1;
        const int avail = w - 2 * (THEME.pad + 1);
        char buf[48];
        int len = gfx_text_fit(labels[idx], f, avail);
        if (len > (int)sizeof buf - 1) {
            len = (int)sizeof buf - 1;
        }
        memcpy(buf, labels[idx], (size_t)len);
        buf[len] = '\0';
        gfx_text(tx, y + (THEME.row_box_h - fh + 1) / 2, buf, f,
                 on ? C_SEL_FG : C_FG);
    }

    if (more_above) {
        scroll_mark(GFX_W - 3, y0 + 1, true);
    }
    if (more_below) {
        scroll_mark(GFX_W - 3, GFX_H - 4, false);
    }
}

int ui_draw_wrapped(int y, const char *text, gfx_color_t c)
{
    const gfx_font_t *f = THEME.body;
    const int x = THEME.pad;
    const int avail = GFX_W - 2 * THEME.pad;
    const char *p = text;

    while (*p != '\0' && y + gfx_font_height(f) <= GFX_H) {
        /* Longest run that fits, breaking at a space or newline. */
        int fit = 0, brk = -1;
        while (p[fit] != '\0' && p[fit] != '\n') {
            char probe[64];
            int k = fit + 1 < (int)sizeof probe ? fit + 1 : (int)sizeof probe - 1;
            memcpy(probe, p, (size_t)k);
            probe[k] = '\0';
            if (gfx_text_width(probe, f) > avail) {
                break;
            }
            if (p[fit] == ' ') {
                brk = fit;
            }
            fit++;
        }
        int take = fit;
        if (p[fit] != '\0' && p[fit] != '\n' && brk > 0) {
            take = brk;     /* wrap at the last space that fitted */
        }
        if (take == 0 && p[0] != '\n') {
            take = 1;       /* a single glyph wider than the line */
        }
        char line[64];
        int k = take < (int)sizeof line ? take : (int)sizeof line - 1;
        memcpy(line, p, (size_t)k);
        line[k] = '\0';
        gfx_text(x, y, line, f, c);
        y += THEME.line_h;
        p += take;
        while (*p == ' ') {
            p++;
        }
        if (*p == '\n') {
            p++;
        }
    }
    return y;
}

/* ---- Menu ------------------------------------------------------------- */

void menu_enter(const screen_t *s)
{
    menu_t *m = s->ctx;
    m->sel = 0;
    m->top = 0;
}

void menu_render(const screen_t *s)
{
    menu_t *m = s->ctx;
    const char *labels[MENU_MAX_ITEMS];
    int n = m->n < MENU_MAX_ITEMS ? m->n : MENU_MAX_ITEMS;
    for (int i = 0; i < n; i++) {
        labels[i] = m->items[i].label;
    }
    ui_draw_title(m->title);
    ui_draw_list(labels, n, m->sel, &m->top);
}

bool menu_input(const screen_t *s, input_ev_t ev)
{
    menu_t *m = s->ctx;
    switch (ev) {
    case IN_UP:
        if (m->sel > 0) {
            m->sel--;
        }
        return true;
    case IN_DOWN:
        if (m->sel < m->n - 1) {
            m->sel++;
        }
        return true;
    case IN_OK:
        if (m->items[m->sel].action) {
            m->items[m->sel].action();
        } else if (m->items[m->sel].next) {
            ui_push(m->items[m->sel].next);
        }
        return true;
    default:
        return false;
    }
}

/* ---- Message ---------------------------------------------------------- */

void message_render(const screen_t *s)
{
    const message_t *m = s->ctx;
    ui_draw_title(m->title);
    ui_draw_wrapped(THEME.top_h + THEME.pad + 2, m->body, C_FG);
}

bool message_input(const screen_t *s, input_ev_t ev)
{
    (void)s;
    if (ev == IN_OK) {
        ui_pop();
        return true;
    }
    return false;
}
