/*
 * Button state machine. See input.h.
 */
#include "input.h"

#include <string.h>

static const char *const NAMES[] = {
    [IN_NONE] = "NONE", [IN_UP] = "UP", [IN_DOWN] = "DOWN",
    [IN_OK] = "OK", [IN_BACK] = "BACK", [IN_HOLD] = "HOLD",
};

const char *input_name(input_ev_t ev)
{
    if ((unsigned)ev >= sizeof NAMES / sizeof NAMES[0]) {
        return "?";
    }
    return NAMES[ev];
}

input_ev_t input_from_name(const char *name)
{
    for (unsigned i = 1; i < sizeof NAMES / sizeof NAMES[0]; i++) {
        if (strcmp(name, NAMES[i]) == 0) {
            return (input_ev_t)i;
        }
    }
    return IN_NONE;
}

void input_buttons_init(input_buttons_t *b, uint8_t n, const input_ev_t *map)
{
    memset(b, 0, sizeof *b);
    b->n = n > INPUT_MAX_BUTTONS ? INPUT_MAX_BUTTONS : n;
    memcpy(b->map, map, b->n * sizeof map[0]);
}

static void on_press(input_buttons_t *b, int i, int64_t now,
                     input_sink_t sink, void *ctx)
{
    b->pressed_at[i] = now;
    b->hold_fired[i] = false;
    b->next_repeat[i] = now + INPUT_REPEAT_DELAY_US;
    if (b->map[i] != IN_BACK) {
        sink(b->map[i], ctx);
    }
}

static void on_release(input_buttons_t *b, int i, input_sink_t sink, void *ctx)
{
    if (b->map[i] == IN_BACK && !b->hold_fired[i]) {
        sink(IN_BACK, ctx);
    }
}

static void while_held(input_buttons_t *b, int i, int64_t now,
                       input_sink_t sink, void *ctx)
{
    const input_ev_t ev = b->map[i];
    if (ev == IN_BACK) {
        if (!b->hold_fired[i] && now - b->pressed_at[i] >= INPUT_HOLD_US) {
            b->hold_fired[i] = true;
            sink(IN_HOLD, ctx);
        }
    } else if (ev == IN_UP || ev == IN_DOWN) {
        if (now >= b->next_repeat[i]) {
            b->next_repeat[i] = now + INPUT_REPEAT_EVERY_US;
            sink(ev, ctx);
        }
    }
}

void input_buttons_poll(input_buttons_t *b, int64_t now_us, uint32_t pressed,
                        input_sink_t sink, void *ctx)
{
    for (int i = 0; i < b->n; i++) {
        const bool raw = (pressed >> i) & 1u;
        if (raw != b->last_raw[i]) {
            b->last_raw[i] = raw;
            b->last_change[i] = now_us;
        }
        if (raw != b->stable[i] &&
            now_us - b->last_change[i] >= INPUT_DEBOUNCE_US) {
            b->stable[i] = raw;
            if (raw) {
                on_press(b, i, now_us, sink, ctx);
            } else {
                on_release(b, i, sink, ctx);
            }
        } else if (b->stable[i] && raw) {
            /* Only while the pin still reads pressed: during a release's
             * debounce the stable state lags, and repeating or reaching the
             * hold time then would act on a key the user has let go. */
            while_held(b, i, now_us, sink, ctx);
        }
    }
}

input_ev_t input_from_detent(int step, bool reverse)
{
    if (step == 0) {
        return IN_NONE;
    }
    if (reverse) {
        step = -step;
    }
    return step > 0 ? IN_DOWN : IN_UP;
}
