#!/usr/bin/env python3
"""
Flows on the real firmware image under QEMU (T6). Each flow is a sequence
of key presses, and scanned QR frames, and the screens they must reach,
mirroring the host test in sim/test_ui.c - the same navigation, proven on
the Xtensa build. Scanned frames are SeedSigner's own, read from
sim/vectors/seedsigner_qr.h, and the note the Scan screen prints must say
what SeedSigner decoded from them.

    ./scripts/qemu-flow.py                 run every flow
    ./scripts/qemu-flow.py --frames DIR    also save each screen as a PPM
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from qemu_harness import Device  # noqa: E402
import hashlib  # noqa: E402
import qr_vectors  # noqa: E402


def recorded(name):
    """A scan SeedSigner recorded: feed its frames (stopping when the
    device completes it) and expect the PSBT SeedSigner got from it."""
    frames, psbt = qr_vectors.scan(name)
    digest = hashlib.sha256(bytes.fromhex(psbt)).hexdigest()[:8]
    return ("SCAN", frames, "psbt %d %s" % (len(psbt) // 2, digest))


# (actions, screen expected after them[, note expected]). An action is a key
# name, a QR frame (bytes or str), or recorded(...).
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
    "scan-compactseedqr": [
        ([], "HOME"),
        (["OK"], "SCAN"),
        ([bytes(16)], "SCAN_RESULT", "seed 12 73c5da0a"),
        (["BACK"], "HOME"),
    ],
    "scan-seedqr-wrong-type": [
        (["DOWN", "OK", "OK"], "SCAN_SEED"),
        (["bitcoin:bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu"], "SCAN_RESULT",
         "wrong-type bitcoin_address"),
        (["BACK"], "LOAD_SEED"),
    ],
    "scan-animated-ur-psbt": [
        (["OK"], "SCAN"),
        ([recorded("ur_psbt_3_medium_lossy")], "SCAN_RESULT"),
        (["HOLD"], "HOME"),
    ],
    "scan-bbqr-zlib": [
        (["OK"], "SCAN"),
        ([recorded("bbqr_5_Z_shuffled")], "SCAN_RESULT"),
    ],
    "scan-invalid": [
        (["OK"], "SCAN"),
        (["hello world"], "SCAN_RESULT", "invalid"),
    ],
}

KEYS = {"UP", "DOWN", "OK", "BACK", "HOLD"}


def act(dev, a):
    """Do one action; return the note it must produce, if any."""
    if isinstance(a, tuple) and a[0] == "SCAN":
        _, frames, want = a
        for f in frames:
            dev.qr(f)
            if dev.screen != "SCAN":
                break
        return want
    if isinstance(a, str) and a in KEYS:
        dev.key(a)
        return None
    dev.qr(a)
    return None

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
                for i, step in enumerate(steps):
                    actions, screen = step[0], step[1]
                    want = step[2] if len(step) > 2 else None
                    for a in actions:
                        want = act(dev, a) or want
                    assert dev.screen == screen, "expected %s, on %s" % (screen, dev.screen)
                    if want:
                        assert dev.notes and dev.notes[-1] == want, \
                            "expected note %r, got %r" % (want, dev.notes[-1:])
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
