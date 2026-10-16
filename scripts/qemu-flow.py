#!/usr/bin/env python3
"""
Flows on the real firmware image under QEMU (T6). Each flow is a sequence
of key presses and the screens they must reach, mirroring the host test in
sim/test_ui.c - the same navigation, proven on the Xtensa build.

    ./scripts/qemu-flow.py                 run every flow
    ./scripts/qemu-flow.py --frames DIR    also save each screen as a PPM
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu_harness import Device  # noqa: E402

# (keys to press, screen expected after them)
FLOWS = {
    "home-menus": [
        ([], "HOME"),
        (["DOWN", "OK"], "LOAD_SEED"),
        (["DOWN", "DOWN", "DOWN", "OK"], "TOOLS"),
        (["BACK"], "LOAD_SEED"),
        (["BACK"], "HOME"),
        (["DOWN", "DOWN", "OK"], "SETTINGS"),
        (["DOWN", "DOWN", "DOWN", "OK"], "VERSION"),
        (["OK"], "SETTINGS"),
        (["HOLD"], "HOME"),
    ],
    "io-test": [
        ([], "HOME"),
        (["DOWN", "DOWN", "DOWN", "OK"], "SETTINGS"),
        (["DOWN", "DOWN", "OK"], "IO_TEST"),
        (["UP", "DOWN", "OK", "BACK"], "IO_TEST"),   # counted, not obeyed
        (["HOLD"], "HOME"),
    ],
}

# Extra checks on the frame after a step: (flow, step) -> function(dev).
def io_counts(dev):
    # UP, DOWN, OK and BACK were each pressed once, so every counter row must
    # show the glyph "1" (5x7 font columns 00 42 7F 40 00): the digit's
    # first column dark, its middle column lit top to bottom. A "0" lights
    # its first column, so it cannot pass. The digit sits after "%-5s ",
    # six cells in from the 2 px padding; rows start 16 px down, 10 apart.
    lit = lambda x, y: dev.pixel(x, y) not in (0, 5, 6)
    x0 = 2 + 6 * 6
    for row, name in enumerate(["UP", "DOWN", "OK", "BACK"]):
        y0 = 16 + row * 10
        assert not any(lit(x0, y0 + r) for r in range(7)), "%s count is not 1" % name
        assert all(lit(x0 + 2, y0 + r) for r in range(7)), "%s count is not 1" % name

CHECKS = {("io-test", 3): io_counts}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", help="save a PPM of every screen reached")
    args = ap.parse_args()
    if args.frames:
        os.makedirs(args.frames, exist_ok=True)

    failed = 0
    for name, steps in FLOWS.items():
        try:
            with Device() as dev:
                for i, (keys, screen) in enumerate(steps):
                    for k in keys:
                        dev.key(k)
                    assert dev.screen == screen, "expected %s, on %s" % (screen, dev.screen)
                    assert dev.frame is not None, "no frame received"
                    if (name, i) in CHECKS:
                        CHECKS[(name, i)](dev)
                    if args.frames:
                        dev.save_frame(os.path.join(args.frames, "%s-%02d-%s.ppm" % (name, i, screen)))
            print("PASS %s (%d steps)" % (name, len(steps)))
        except AssertionError as e:
            failed += 1
            print("FAIL %s: %s" % (name, e))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
