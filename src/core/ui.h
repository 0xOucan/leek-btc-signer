/*
 * The screen engine: a stack of screens, like SeedSigner's View back stack.
 *
 * A screen is a table of hooks plus a context pointer. `enter` runs when it
 * is pushed, `leave` when it is popped - and `leave` is where a screen that
 * showed or held a secret wipes it, on every path off the stack (BACK, HOLD,
 * or a later screen sending the user home). Covered screens keep their state,
 * so returning to a menu keeps its selection.
 *
 * BACK pops the top screen unless the screen handles it. HOLD (BACK held)
 * goes to the hold handler, which by default returns home.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "input.h"

typedef struct screen screen_t;

struct screen {
    const char *name;
    void (*enter)(const screen_t *s);
    void (*render)(const screen_t *s);
    bool (*input)(const screen_t *s, input_ev_t ev);   /* true = handled */
    void (*leave)(const screen_t *s);
    /* Called while the screen is on top, between inputs, about every
       UI_TICK_MS: for work that does not come from a key, such as decoding
       scanned frames. True = redraw. */
    bool (*tick)(const screen_t *s);
    void *ctx;
};

#define UI_TICK_MS 20

#define UI_STACK_MAX 16

void ui_init(const screen_t *root);
bool ui_push(const screen_t *s);
void ui_pop(void);
void ui_home(void);
void ui_input(input_ev_t ev);
/* Draw the top screen into the framebuffer. */
void ui_render(void);
const screen_t *ui_top(void);
int ui_depth(void);
/* Run the top screen's tick; true if it wants a redraw. */
bool ui_tick(void);
/* True if the top screen has a tick, so the loop must not block forever. */
bool ui_wants_tick(void);
/* Run `fn` once the current input or tick has been handled: for a screen's
   leave hook that must move the stack (SeedSigner's "then go to the main
   menu"), which it cannot do while it is itself being popped. */
void ui_after(void (*fn)(void));

/* Replace the top screen with `s` (the root is never replaced). */
void ui_replace(const screen_t *s);

/* Milliseconds from the platform's clock (set at boot; tests set a fake). */
uint32_t ui_now_ms(void);
void ui_set_clock(uint32_t (*fn)(void));

/* A one-line machine-readable note on what a screen concluded ("seed 12
   73c5da0a"), for the QEMU harness and host tests to check without reading
   pixels. Silent unless a hook is set; never carries a secret. */
void ui_note(const char *fmt, ...);
void ui_set_note_hook(void (*fn)(const char *note));
void ui_set_hold_handler(void (*fn)(void));

/* ---- Widgets ----------------------------------------------------------- */

void ui_draw_title(const char *title);
/* Draw `n` labels as a list with `sel` highlighted, scrolling `*top`. */
void ui_draw_list(const char *const *labels, int n, int sel, int *top);
/* Word-wrapped text from y; returns the y below the last line. */
int ui_draw_wrapped(int y, const char *text, gfx_color_t c);

/* ---- Menu screens ------------------------------------------------------ */

typedef struct {
    const char *label;
    const screen_t *next;       /* pushed on OK */
    void (*action)(void);       /* or this is called instead */
} menu_item_t;

#define MENU_MAX_ITEMS 16

typedef struct {
    const char *title;
    const menu_item_t *items;
    int n;
    int sel, top;               /* state */
} menu_t;

void menu_enter(const screen_t *s);
void menu_render(const screen_t *s);
bool menu_input(const screen_t *s, input_ev_t ev);

#define SCREEN_MENU(name_, menu_) \
    { .name = (name_), .enter = menu_enter, .render = menu_render, \
      .input = menu_input, .ctx = (void *)&(menu_) }

/* ---- Message screens --------------------------------------------------- */

typedef struct {
    const char *title;
    const char *body;
} message_t;

void message_render(const screen_t *s);
bool message_input(const screen_t *s, input_ev_t ev);

#define SCREEN_MESSAGE(name_, msg_) \
    { .name = (name_), .render = message_render, .input = message_input, \
      .ctx = (void *)&(msg_) }
