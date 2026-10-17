/* Stackable list and status views. See views.h. */
#include "views.h"

#include <stdio.h>
#include <string.h>

#include "screens.h"
#include "theme.h"

#define VIEW_SLOTS 12
#define VIEW_TEXT_MAX 400
#define VIEW_LINES_MAX 32

typedef struct {
    bool used;
    view_spec_t spec;
    char name[40], title[32], headline[48], text[VIEW_TEXT_MAX];
    char labels[VIEW_BUTTONS_MAX][VIEW_LABEL_MAX];
    const char *lines[VIEW_LINES_MAX];
    int len[VIEW_LINES_MAX];
    int n_lines;
    int sel, top, body_top;
    bool paged;         /* a list whose text does not fit: it pages, as a status body */
    screen_t screen;
} slot_t;

static slot_t slots[VIEW_SLOTS];

static slot_t *of(const screen_t *s)
{
    return (slot_t *)s->ctx;
}

/* ---- Text ------------------------------------------------------------- */

static int wrap(const char *text, int avail, const char *lines[], int len[], int max)
{
    int n = 0;
    const char *p = text;
    while (*p && n < max) {
        int fit = 0, brk = -1;
        char tmp[96];
        while (p[fit] && p[fit] != '\n' && fit < (int)sizeof tmp - 1) {
            memcpy(tmp, p, (size_t)fit + 1);
            tmp[fit + 1] = '\0';
            if (gfx_text_width(tmp, THEME.body) > avail) {
                break;
            }
            if (p[fit] == ' ') {
                brk = fit;
            }
            fit++;
        }
        int take = (p[fit] && p[fit] != '\n' && brk > 0) ? brk : fit;
        if (take == 0 && *p != '\n') {
            take = 1;
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

static void draw_line(int x, int y, const char *s, int n, gfx_color_t c)
{
    char buf[96];
    snprintf(buf, sizeof buf, "%.*s", n, s);
    gfx_text(x, y, buf, THEME.body, c);
}

/* ---- Buttons ---------------------------------------------------------- */

/* Rows of buttons from y, `sel` highlighted, scrolled by *top so it shows. */
static void draw_buttons(slot_t *v, int y, int rows)
{
    if (rows < 1) {
        rows = 1;
    }
    if (v->sel < v->top) v->top = v->sel;
    if (v->sel >= v->top + rows) v->top = v->sel - rows + 1;
    for (int i = 0; i < rows && v->top + i < v->spec.n_buttons; i++) {
        const int k = v->top + i;
        const int ry = y + i * THEME.row_h;
        const bool on = k == v->sel;
        const bool red = v->spec.destructive_last && k == v->spec.n_buttons - 1;
        if (on) {
            gfx_fill_rect(THEME.pad, ry, GFX_W - 2 * THEME.pad, THEME.row_box_h, C_SEL_BG);
        } else if (THEME.row_style == ROW_BUTTON) {
            gfx_fill_rect(THEME.pad, ry, GFX_W - 2 * THEME.pad, THEME.row_box_h, C_BUTTON_BG);
        }
        const int ty = ry + (THEME.row_box_h - gfx_font_height(THEME.body)) / 2;
        gfx_text(THEME.pad + 3, ty, v->labels[k], THEME.body,
                 on ? C_SEL_FG : red ? C_ERROR : C_FG);
    }
    if (v->top > 0) {
        gfx_text(GFX_W - THEME.pad - 6, y, "^", THEME.body, C_MUTED);
    }
    if (v->top + rows < v->spec.n_buttons) {
        gfx_text(GFX_W - THEME.pad - 6, y + (rows - 1) * THEME.row_h, "v", THEME.body, C_MUTED);
    }
}

/* ---- Rendering -------------------------------------------------------- */

static gfx_color_t kind_color(notice_kind_t k)
{
    switch (k) {
    case NOTICE_SUCCESS: return C_OK;
    case NOTICE_WARNING: return C_WARN;
    case NOTICE_DIRE:    return C_DIRE;
    case NOTICE_ERROR:   return C_ERROR;
    default:             return C_INFO;
    }
}

static int buttons_height(const slot_t *v)
{
    const int shown = v->spec.n_buttons < 2 ? v->spec.n_buttons : 2;
    return shown * THEME.row_h;
}

static void render(const screen_t *s)
{
    slot_t *v = of(s);
    ui_draw_title(v->title);
    int y = THEME.top_h + THEME.pad;

    if (!v->spec.status && !v->paged) {
        for (int i = 0; i < v->n_lines; i++) {
            draw_line(THEME.pad, y, v->lines[i], v->len[i], C_MUTED);
            y += THEME.line_h;
        }
        if (v->n_lines == 0) {
            y = THEME.row_y0;
        }
        draw_buttons(v, y, (GFX_H - y) / THEME.row_h);
        return;
    }

    const gfx_color_t c = kind_color(v->spec.kind);
    const bool edged = v->spec.status && (v->spec.kind == NOTICE_WARNING || v->spec.kind == NOTICE_DIRE ||
                       v->spec.kind == NOTICE_ERROR);
    if (edged) {
        const int w = v->spec.kind == NOTICE_WARNING ? 1 : 2;
        for (int i = 0; i < w; i++) {
            gfx_rect(i, THEME.top_h + i, GFX_W - 2 * i, GFX_H - THEME.top_h - 2 * i, c);
        }
    }
    if (v->headline[0]) {
        if (GFX_MONO && edged) {
            gfx_fill_rect(2, y - 1, GFX_W - 4, THEME.line_h, C_SEL_BG);
            gfx_text_centered(0, GFX_W, y, v->headline, THEME.body, C_SEL_FG);
        } else {
            gfx_text_centered(0, GFX_W, y, v->headline, THEME.body, c);
        }
        y += THEME.line_h + 2;
    }
    v->body_top = y;
    const int bh = buttons_height(v);
    const int rows = (GFX_H - y - bh - 2) / THEME.line_h;
    for (int i = 0; i < rows && v->top + i < v->n_lines; i++) {
        draw_line(THEME.pad + 2, y + i * THEME.line_h, v->lines[v->top + i], v->len[v->top + i], C_FG);
    }
    if (v->top + rows < v->n_lines) {
        gfx_text(GFX_W - THEME.pad - 6, y + (rows - 1) * THEME.line_h, "v", THEME.body, C_MUTED);
    }
    slot_t tmp = *v;
    tmp.top = v->sel >= 2 ? v->sel - 1 : 0;
    draw_buttons(&tmp, GFX_H - bh - (edged ? 2 : 0), 2);
}

static int body_rows(const slot_t *v)
{
    return (GFX_H - v->body_top - buttons_height(v) - 2) / THEME.line_h;
}

static bool input(const screen_t *s, input_ev_t ev)
{
    slot_t *v = of(s);
    if (ev == IN_DOWN) {
        /* A status body that runs past the screen pages first. */
        if ((v->spec.status || v->paged) && v->sel == 0 && v->top + body_rows(v) < v->n_lines) {
            v->top++;
        } else if (v->sel + 1 < v->spec.n_buttons) {
            v->sel++;
        }
        return true;
    }
    if (ev == IN_UP) {
        if (v->sel > 0) {
            v->sel--;
        } else if ((v->spec.status || v->paged) && v->top > 0) {
            v->top--;
        }
        return true;
    }
    if (ev == IN_OK) {
        if (v->spec.n_buttons == 0) {
            return true;
        }
        ui_note("select %s %d %s", v->name, v->sel, v->labels[v->sel]);
        if (v->spec.select) {
            v->spec.select(v->sel);
        } else {
            ui_pop();
        }
        return true;
    }
    if (ev == IN_BACK && v->spec.back) {
        v->spec.back();
        return true;
    }
    return false;
}

static void leave(const screen_t *s)
{
    slot_t *v = of(s);
    memset(v, 0, sizeof *v);
}

/* ---- Pushing ---------------------------------------------------------- */

static const screen_t *fill(const view_spec_t *spec)
{
    slot_t *v = NULL;
    for (int i = 0; i < VIEW_SLOTS && !v; i++) {
        if (!slots[i].used) {
            v = &slots[i];
        }
    }
    if (v == NULL) {
        return NULL;
    }
    memset(v, 0, sizeof *v);
    v->used = true;
    v->spec = *spec;
    snprintf(v->name, sizeof v->name, "%s", spec->name);
    snprintf(v->title, sizeof v->title, "%s", spec->title ? spec->title : "");
    snprintf(v->headline, sizeof v->headline, "%s", spec->headline ? spec->headline : "");
    snprintf(v->text, sizeof v->text, "%s", spec->text ? spec->text : "");
    if (v->spec.n_buttons > VIEW_BUTTONS_MAX) {
        v->spec.n_buttons = VIEW_BUTTONS_MAX;
    }
    for (int i = 0; i < v->spec.n_buttons; i++) {
        snprintf(v->labels[i], VIEW_LABEL_MAX, "%s", spec->buttons[i]);
    }
    if (spec->selected > 0 && spec->selected < v->spec.n_buttons) {
        v->sel = spec->selected;
    }
    const int avail = GFX_W - 2 * THEME.pad - (spec->status ? 4 : 0);
    v->n_lines = wrap(v->text, avail, v->lines, v->len, VIEW_LINES_MAX);
    if (!spec->status && v->n_lines > 0) {
        const int y0 = THEME.top_h + THEME.pad;
        v->paged = y0 + v->n_lines * THEME.line_h + v->spec.n_buttons * THEME.row_h > GFX_H;
        v->body_top = y0;
    }
    v->screen = (screen_t){ .name = v->name, .render = render, .input = input, .leave = leave, .ctx = v };
    return &v->screen;
}

bool view_push(const view_spec_t *spec)
{
    const screen_t *s = fill(spec);
    return s && ui_push(s);
}

/* Both fill a slot first, while the strings they were given are still
   valid (they may point into a view about to be popped), and only then
   change the stack. */
bool view_replace(const view_spec_t *spec)
{
    const screen_t *s = fill(spec);
    if (s == NULL) {
        return false;
    }
    ui_replace(s);
    return ui_top() == s;
}

bool view_push_fresh(const view_spec_t *spec)
{
    const screen_t *s = fill(spec);
    if (s == NULL) {
        return false;
    }
    ui_clear_history();
    return ui_push(s);
}

int view_arg(void)
{
    const screen_t *s = ui_top();
    for (int i = 0; i < VIEW_SLOTS; i++) {
        if (s == &slots[i].screen) {
            return slots[i].spec.arg;
        }
    }
    return -1;
}

void view_clear_history(void)
{
    ui_clear_history();
}

const char *ui_selected_label(void)
{
    const screen_t *s = ui_top();
    if (s == NULL) {
        return NULL;
    }
    for (int i = 0; i < VIEW_SLOTS; i++) {
        if (s == &slots[i].screen) {
            return slots[i].spec.n_buttons ? slots[i].labels[slots[i].sel] : NULL;
        }
    }
    if (s->input == menu_input) {
        const menu_t *m = s->ctx;
        return m->items[m->sel].label;
    }
    return NULL;
}
