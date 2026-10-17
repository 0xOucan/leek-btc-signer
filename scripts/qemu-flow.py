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
    return ("RECORDED", frames, "psbt %d %s" % (len(psbt) // 2, digest))


# (actions, screen expected after them[, note expected]). An action is a key
# name, a QR frame (bytes or str), or recorded(...).
FLOWS = {
    "home-menus": [
        ([], "MainMenuView"),
        (["DOWN", "OK"], "LoadSeedView"),
        (["DOWN", "DOWN", "DOWN", "OK"], "ToolsMenuView"),
        (["BACK"], "LoadSeedView"),
        (["BACK"], "MainMenuView"),
        (["DOWN", "DOWN", "OK"], "SettingsMenuView"),
        (["DOWN", "DOWN", "DOWN", "OK"], "VersionView"),
        (["OK"], "SettingsMenuView"),
        (["HOLD"], "MainMenuView"),
    ],
    "io-test": [
        ([], "MainMenuView"),
        (["DOWN", "DOWN", "DOWN", "OK"], "SettingsMenuView"),
        (["DOWN", "DOWN", "OK"], "IOTestView"),
        (["UP", "DOWN", "OK", "BACK"], "IOTestView"),   # counted, not obeyed
        (["HOLD"], "MainMenuView"),
    ],
    "scan-compactseedqr": [
        ([], "MainMenuView"),
        (["OK"], "ScanView"),
        ([bytes(16)], "SeedFinalizeView", "fingerprint 73c5da0a"),
        (["OK"], "SeedOptionsView"),
        (["BACK"], "MainMenuView"),
    ],
    "scan-seedqr-wrong-type": [
        (["DOWN", "OK", "OK"], "ScanSeedQRView"),
        (["bitcoin:bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu"], "ErrorView",
         "wrong-type bitcoin_address"),
        (["BACK"], "LoadSeedView"),
    ],
    "scan-animated-ur-psbt": [
        (["OK"], "ScanView"),
        ([recorded("ur_psbt_3_medium_lossy")], "NotYetImplementedView"),
        (["HOLD"], "MainMenuView"),
    ],
    "scan-bbqr-zlib": [
        (["OK"], "ScanView"),
        ([recorded("bbqr_5_Z_shuffled")], "NotYetImplementedView"),
    ],
    # SeedSigner's test_scan_seedqr_flow, test_export_xpub_standard_flow and
    # test_discard_seed_flow, on one device: abandon x11 about, BIP-84.
    "seed-lifecycle": [
        (["OK"], "ScanView"),
        ([bytes(16)], "SeedFinalizeView", "fingerprint 73c5da0a"),
        (["OK"], "SeedOptionsView"),
        (["BACK"], "MainMenuView"),
        (["DOWN", "OK"], "SeedsMenuView"),
        (["OK"], "SeedOptionsView"),
        (["DOWN", "OK"], "SeedExportXpubSigTypeView"),
        (["OK"], "SeedExportXpubScriptTypeView"),
        (["OK"], "SeedExportXpubQRFormatView"),
        (["OK"], "SeedExportXpubWarningView"),
        (["OK"], "SeedExportXpubDetailsView",
         ("prefix", "xpub m/84'/0'/0' zpub6rFR7y4Q2AijBEqTUquhVz398htDFrtymD9xYYfG1m4wAcvPhXNfE3EfH1r1ADqtfSdVCToUG868RvUUkgDGf4wpg7FcomedGtc6jKPrGFh")),
        (["OK"], "SeedExportXpubQRDisplayView", ("prefix", "xpub-qr m/84'/0'/0'")),
        (["OK"], "MainMenuView"),
        (["DOWN", "OK"], "SeedsMenuView"),
        (["OK"], "SeedOptionsView"),
        (["DOWN", "DOWN", "DOWN", "DOWN", "OK"], "SeedDiscardView"),
        (["DOWN", "OK"], "MainMenuView", "discarded"),
        (["DOWN", "OK"], "LoadSeedView"),
    ],
    "scan-invalid": [
        (["OK"], "ScanView"),
        (["hello world"], "ScanInvalidQRTypeView", "invalid"),
    ],
}

KEYS = {"UP", "DOWN", "OK", "BACK", "HOLD"}


def act(dev, a):
    """Do one action; return the note it must produce, if any."""
    if isinstance(a, tuple) and a[0] == "RECORDED":
        _, frames, want = a
        for f in frames:
            dev.qr(f)
            if dev.screen != "ScanView":
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
                    if isinstance(want, tuple) and want[0] == "prefix":
                        assert any(n.startswith(want[1]) for n in dev.notes[-4:]), \
                            "expected a note starting %r, got %r" % (want[1], dev.notes[-4:])
                    elif want:
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
