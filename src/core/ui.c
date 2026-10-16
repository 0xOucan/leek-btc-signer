/*
 * The screen stack. See ui.h.
 */
#include "ui.h"

#include <stddef.h>

#include "gfx.h"

static const screen_t *stack[UI_STACK_MAX];
static int depth;
static void (*hold_handler)(void);

static void enter(const screen_t *s)
{
    if (s->enter) {
        s->enter(s);
    }
}

static void leave(const screen_t *s)
{
    if (s->leave) {
        s->leave(s);
    }
}

void ui_init(const screen_t *root)
{
    while (depth > 0) {
        leave(stack[--depth]);
    }
    hold_handler = ui_home;
    stack[depth++] = root;
    enter(root);
}

bool ui_push(const screen_t *s)
{
    if (s == NULL || depth >= UI_STACK_MAX) {
        return false;
    }
    stack[depth++] = s;
    enter(s);
    return true;
}

void ui_pop(void)
{
    if (depth <= 1) {
        return;     /* the root stays */
    }
    leave(stack[--depth]);
    stack[depth] = NULL;
}

void ui_home(void)
{
    while (depth > 1) {
        ui_pop();
    }
}

void ui_set_hold_handler(void (*fn)(void))
{
    hold_handler = fn ? fn : ui_home;
}

void ui_input(input_ev_t ev)
{
    if (depth == 0 || ev == IN_NONE) {
        return;
    }
    if (ev == IN_HOLD) {
        hold_handler();
        return;
    }
    const screen_t *s = stack[depth - 1];
    bool handled = s->input ? s->input(s, ev) : false;
    if (!handled && ev == IN_BACK) {
        ui_pop();
    }
}

void ui_render(void)
{
    if (depth == 0) {
        return;
    }
    const screen_t *s = stack[depth - 1];
    gfx_clear(C_BG);
    if (s->render) {
        s->render(s);
    }
}

const screen_t *ui_top(void) { return depth ? stack[depth - 1] : NULL; }
int ui_depth(void) { return depth; }
