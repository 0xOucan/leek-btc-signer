/*
 * Input: what the user did, independent of how.
 *
 * The encoder, the four buttons, the TFT modules' single key, the QEMU
 * harness and the desktop simulator all produce these same five events, so
 * no screen knows which hardware it runs on. Every screen must work with
 * UP, DOWN, OK and BACK alone.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    IN_NONE = 0,
    IN_UP,      /* encoder anticlockwise, or button UP */
    IN_DOWN,    /* encoder clockwise, or button DOWN */
    IN_OK,      /* encoder push, or button OK */
    IN_BACK,    /* BACK, released before the hold time */
    IN_HOLD,    /* BACK held: clear the seed and go home */
} input_ev_t;

const char *input_name(input_ev_t ev);
/* Parse a name as input_name() prints it; IN_NONE if unknown. */
input_ev_t input_from_name(const char *name);

typedef void (*input_sink_t)(input_ev_t ev, void *ctx);

/*
 * Debouncing, auto-repeat and BACK-hold for up to four buttons, as a pure
 * state machine: the caller samples the pins and supplies the time, so host
 * tests can drive it with a fake clock.
 *
 * Per button:
 *  - UP and DOWN fire on press and repeat while held.
 *  - OK fires on press.
 *  - BACK fires on release, so that a long press can mean something else:
 *    held for INPUT_HOLD_US it fires IN_HOLD once, and no BACK on release.
 */
#define INPUT_MAX_BUTTONS 4

/* LeekWallet measured 100 ms on this button module; shorter let bounce
 * through as double presses. */
#define INPUT_DEBOUNCE_US      100000
#define INPUT_REPEAT_DELAY_US  500000
#define INPUT_REPEAT_EVERY_US  150000
#define INPUT_HOLD_US         3000000

typedef struct {
    uint8_t n;
    input_ev_t map[INPUT_MAX_BUTTONS];
    bool stable[INPUT_MAX_BUTTONS];     /* debounced: true = pressed */
    bool last_raw[INPUT_MAX_BUTTONS];
    int64_t last_change[INPUT_MAX_BUTTONS];
    int64_t pressed_at[INPUT_MAX_BUTTONS];
    int64_t next_repeat[INPUT_MAX_BUTTONS];
    bool hold_fired[INPUT_MAX_BUTTONS];
} input_buttons_t;

void input_buttons_init(input_buttons_t *b, uint8_t n, const input_ev_t *map);

/* `pressed` bit i set = button i is down right now (after polarity). */
void input_buttons_poll(input_buttons_t *b, int64_t now_us, uint32_t pressed,
                        input_sink_t sink, void *ctx);

/* Encoder detents to events: +1 (clockwise) is DOWN, unless reversed. */
input_ev_t input_from_detent(int step, bool reverse);
