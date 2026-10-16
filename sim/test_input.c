/* input-core.c: debounce, BACK release and hold, auto-repeat, detents. */
#include "check.h"
#include "input.h"

static input_ev_t got[64];
static int ngot;
static void sink(input_ev_t ev, void *ctx) { (void)ctx; if (ngot < 64) got[ngot++] = ev; }

static input_buttons_t b;
static const input_ev_t MAP[4] = { IN_UP, IN_DOWN, IN_BACK, IN_OK };
enum { UP = 1, DOWN = 2, BACK = 4, OK = 8 };

/* Hold `mask` from t0 to t1, polling every 1 ms like the firmware. */
static int64_t hold(int64_t t0, int64_t ms, uint32_t mask)
{
    for (int64_t t = t0; t < t0 + ms * 1000; t += 1000) input_buttons_poll(&b, t, mask, sink, NULL);
    return t0 + ms * 1000;
}

static void reset(void) { input_buttons_init(&b, 4, MAP); ngot = 0; }

int main(void)
{
    reset();
    int64_t t = hold(0, 50, 0);
    t = hold(t, 150, OK);
    t = hold(t, 150, 0);
    CHECK(ngot == 1 && got[0] == IN_OK, "one OK press, got %d events", ngot);

    reset();
    t = 0;
    for (int i = 0; i < 10; i++) { t = hold(t, 5, OK); t = hold(t, 5, 0); }
    t = hold(t, 200, 0);
    CHECK(ngot == 0, "100 ms of chatter produced %d events", ngot);

    reset();
    t = hold(0, 300, BACK);
    CHECK(ngot == 0, "BACK does not fire while held");
    t = hold(t, 200, 0);
    CHECK(ngot == 1 && got[0] == IN_BACK, "BACK fires on release");

    reset();
    t = hold(0, 3200, BACK);
    CHECK(ngot == 1 && got[0] == IN_HOLD, "3 s of BACK is HOLD");
    t = hold(t, 2000, BACK);
    t = hold(t, 200, 0);
    CHECK(ngot == 1, "HOLD once, and no BACK on release (%d events)", ngot);

    reset();
    t = hold(0, 1000, DOWN);
    t = hold(t, 200, 0);
    /* Press at 100 ms, repeats from 600 ms every 150 ms up to 1100 ms. */
    CHECK(ngot == 4, "held DOWN repeats: %d events", ngot);
    for (int i = 0; i < ngot; i++) CHECK(got[i] == IN_DOWN, "all DOWN");

    reset();
    t = hold(0, 300, OK | UP);
    CHECK(ngot == 2, "two buttons at once both count");

    CHECK(input_from_detent(1, false) == IN_DOWN, "clockwise is DOWN");
    CHECK(input_from_detent(-1, false) == IN_UP, "anticlockwise is UP");
    CHECK(input_from_detent(1, true) == IN_UP, "reversed");
    CHECK(input_from_detent(0, false) == IN_NONE, "no step");

    CHECK(input_from_name("HOLD") == IN_HOLD, "names round-trip");
    CHECK(input_from_name("nope") == IN_NONE, "unknown name");
    (void)t;
    return CHECK_DONE("test_input");
}
