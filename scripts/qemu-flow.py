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


def flow_psbt(test):
    """The PSBT one of SeedSigner's PSBT flow tests scans, as base64 text,
    from sim/vectors/seedsigner_psbt_flows.h."""
    import re
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sim", "vectors",
                        "seedsigner_psbt_flows.h")
    m = re.search(r'"TestPSBTFlows::%s", "([^"]+)"' % test, open(path).read())
    return m.group(1)


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
        (["DOWN"] * 6 + ["OK"], "VersionView"),
        (["OK"], "SettingsMenuView"),
        (["HOLD"], "MainMenuView"),
    ],
    "io-test": [
        ([], "MainMenuView"),
        (["DOWN", "DOWN", "DOWN", "OK"], "SettingsMenuView"),
        (["DOWN"] * 4 + ["OK"], "IOTestView"),
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
        ([recorded("ur_psbt_3_medium_lossy")], "PSBTSelectSeedView"),
        (["HOLD"], "MainMenuView"),
    ],
    "scan-bbqr-zlib": [
        (["OK"], "ScanView"),
        ([recorded("bbqr_5_Z_shuffled")], "PSBTSelectSeedView"),
    ],
    # SeedSigner's test_scan_seedqr_flow, test_export_xpub_standard_flow and
    # test_discard_seed_flow, on one device: abandon x11 about, whose BIP-84
    # account xpub is BIP-84's own test vector.
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
         ("prefix", "xpub m/84'/0'/0' zpub6rFR7y4Q2AijBEqTUquhVz398htDFrtymD9xYYfG1m4wAcvPhXNfE3EfH1r1ADqtfSdVCToUG868RvUUkgDKf31mGDtKsAYz2oz2AGutZYs")),
        (["OK"], "SeedExportXpubQRDisplayView", ("prefix", "xpub-qr m/84'/0'/0'")),
        (["OK"], "MainMenuView"),
        (["DOWN", "OK"], "SeedsMenuView"),
        (["OK"], "SeedOptionsView"),
        (["DOWN", "DOWN", "DOWN", "DOWN", "OK"], "SeedDiscardView"),
        (["DOWN", "OK"], "MainMenuView", "discarded"),
        (["DOWN", "OK"], "LoadSeedView"),
    ],
    # SeedSigner's test_scan_psbt_first_then_correct_seedqr_flow: scan the
    # PSBT, then the seed that signs it, review every output and sign.
    "psbt-sign": [
        (["OK"], "ScanView"),
        ([flow_psbt("test_scan_psbt_first_then_correct_seedqr_flow")], "PSBTSelectSeedView"),
        (["OK"], "ScanSeedQRView"),
        (["080115060387063104071857067618681125136207731354"], "SeedFinalizeView",
         "fingerprint 394aed14"),
        (["OK"], "PSBTOverviewView", ("prefix", "overview 123456 ")),
        (["OK"], "PSBTMathView"),
        (["OK"], "PSBTAddressDetailsView"),
        (["OK"], "PSBTChangeDetailsView"),
        (["OK", "OK", "OK"], "PSBTFinalizeView"),
        (["OK"], "PSBTSignedQRDisplayView", ("prefix", "sign OK 2")),
        (["OK"], "MainMenuView"),
    ],
    # A new seed from 50 rolls of 1: SeedSigner's own Views give fingerprint
    # 65fb43fe (sim/vectors/seedsigner_tools.h).
    "dice-seed": [
        (["DOWN", "DOWN", "OK"], "ToolsMenuView"),
        (["DOWN", "OK"], "ToolsDiceEntropyMnemonicLengthView"),
        (["OK"], "ToolsDiceEntropyEntryView"),
        (["OK"] * 50, "SeedWordsWarningView", "dice 12 65fb43fe"),
        (["BACK"], "MainMenuView"),
    ],
    # SeedSigner's test__address_explorer__flow on the firmware: abandon x11
    # about's first BIP-84 receive address, BIP-84's own test vector.
    "address-explorer": [
        (["OK"], "ScanView"),
        ([bytes(16)], "SeedFinalizeView", "fingerprint 73c5da0a"),
        (["OK"], "SeedOptionsView"),
        (["BACK"], "MainMenuView"),
        (["DOWN", "DOWN", "OK"], "ToolsMenuView"),
        (["DOWN", "DOWN", "DOWN", "OK"], "ToolsAddressExplorerSelectSourceView"),
        (["OK"], "SeedExportXpubScriptTypeView"),
        (["OK"], "ToolsAddressExplorerAddressTypeView", ("prefix", "explorer 73c5da0a m/84'/0'/0'")),
        (["OK"], "ToolsAddressExplorerAddressListView"),
        (["OK"], "ToolsAddressExplorerAddressView",
         "address 0 bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu"),
        (["OK"], "ToolsAddressExplorerAddressListView"),
        (["HOLD"], "MainMenuView"),
    ],
    # SeedSigner's test_transcribe_seedqr_and_verify, to the point a camera
    # is needed: export abandon x11 about as a standard SeedQR.
    "seedqr-transcribe": [
        (["OK"], "ScanView"),
        ([bytes(16)], "SeedFinalizeView", "fingerprint 73c5da0a"),
        (["OK"], "SeedOptionsView"),
        (["DOWN", "DOWN", "DOWN", "OK"], "SeedBackupView"),
        (["DOWN", "OK"], "SeedTranscribeSeedQRFormatView"),
        (["OK"], "SeedTranscribeSeedQRWarningView"),
        (["OK"], "SeedTranscribeSeedQRWholeQRView", "seedqr 25x25"),
        (["OK"], "SeedTranscribeSeedQRZoomedInView"),
        (["DOWN"], "SeedTranscribeSeedQRZoomedInView", "zone A-2"),
        (["OK"], "SeedTranscribeSeedQRConfirmQRPromptView"),
        (["DOWN", "OK"], "SeedOptionsView"),
    ],
    # Verify BIP-84's first address against abandon x11 about, loading the
    # seed mid-flow: the search runs on the firmware's tick.
    "verify-address": [
        (["DOWN", "DOWN", "OK"], "ToolsMenuView"),
        (["DOWN", "DOWN", "DOWN", "DOWN", "OK"], "ScanAddressView"),
        (["bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu"], "SeedSelectSeedView"),
        (["OK"], "ScanView"),
        ([bytes(16)], "SeedFinalizeView", "fingerprint 73c5da0a"),
        (["OK"], "SeedAddressVerificationSuccessView",
         ("prefix", "verified 0 receive bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu")),
        (["OK"], "MainMenuView"),
    ],
    # SeedSigner's settings menus on the firmware: Advanced, then Bitcoin
    # network set to Testnet; choosing it again returns to the menu.
    "settings-network": [
        (["DOWN", "DOWN", "DOWN", "OK"], "SettingsMenuView"),
        (["DOWN", "DOWN", "DOWN", "OK"], "SettingsMenuView"),
        (["OK"], "SettingsEntryUpdateSelectionView"),
        (["DOWN", "OK"], "SettingsEntryUpdateSelectionView", "setting network=T"),
        (["OK"], "SettingsMenuView"),
        (["HOLD"], "MainMenuView"),
    ],
    # SeedSigner's test_settingsqr: its Total_noob_mode SettingsQR. With no
    # microSD card, persistent=E is applied as Disabled.
    "settingsqr": [
        (["OK"], "ScanView"),
        (["settings::v1 name=Total_noob_mode persistent=E xpub_qr=urca,sta denom=thr network=M "
          "qr_density=M sigs=ss scripts=nat xpub_details=E passphrase=E camera=0 compact_seedqr=E "
          "bip85=D priv_warn=E dire_warn=E partners=E"], "SettingsIngestSettingsQRView",
         "settingsqr applied 15"),
        (["OK"], "MainMenuView"),
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
