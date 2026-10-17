#!/usr/bin/env python3
"""
How much of SeedSigner a user can do on Leek BTC Signer, measured.

SeedSigner's user-facing capability is its set of View classes
(src/seedsigner/views/*.py): every screen-step a user can reach. That list
is the denominator, extracted from SeedSigner's source, so it is
SeedSigner's definition and not ours:

    ./scripts/coverage.py --extract PATH/TO/seedsigner   # refresh the list

A view counts as done only when the device does what SeedSigner's view does
and a test proves it: a QEMU flow on the firmware image or a host suite.
Engine work (keys, PSBT rules, QR codes) earns nothing on its own; it is
what makes the views quick to finish. Every view weighs the same.

    ./scripts/coverage.py     # print the figure, write docs/COVERAGE.md and
                              # docs/img/progress.svg

Run it, and commit the outputs, whenever a view is finished.
"""
import argparse
import ast
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VIEWS = os.path.join(ROOT, "docs", "seedsigner-views.txt")
OUT_MD = os.path.join(ROOT, "docs", "COVERAGE.md")
OUT_SVG = os.path.join(ROOT, "docs", "img", "progress.svg")

AREAS = [("view.py", "System"), ("scan_views.py", "Scan"), ("seed_views.py", "Seeds"),
         ("psbt_views.py", "PSBT signing"), ("tools_views.py", "Tools"),
         ("settings_views.py", "Settings"), ("screensaver.py", "Splash")]

# View -> the test that proves our equivalent. Only finished views go here.
DONE = {
    # System and menus
    "MainMenuView": "sim/test_ui.c; qemu-flow home-menus",
    "BackStackView": "the screen stack: sim/test_ui.c (ten levels, BACK and HOLD)",
    "NotYetImplementedView": "placeholders naming the View not ported yet: sim/test_ui.c, test_seed_flows.c",
    "ErrorView": "status screens: sim/test_screens.c; Wrong QR Type in qemu-flow",
    "LoadSeedView": "SeedSigner's menu with Electrum off (its default): test_seed_flows.c; qemu-flow",
    "ToolsMenuView": "SeedSigner's five entries: qemu-flow home-menus",
    "IOTestView": "qemu-flow io-test",
    "VersionView": "qemu-flow home-menus",
    # Scanning
    "ScanView": "sim/test_scan.c (every recorded scan); qemu-flow scan-*, seed-lifecycle",
    "ScanSeedQRView": "sim/test_scan.c; qemu-flow scan-seedqr-wrong-type",
    "ScanInvalidQRTypeView": "sim/test_scan.c; qemu-flow scan-invalid",
    "ScanAddressView": "refuses other codes: test_tools_flows.c; qemu-flow verify-address",
    # Seeds: SeedSigner's tests/test_flows_seed.py, ported in sim/test_seed_flows.c
    "SeedsMenuView": "test_seed_flows.c (redirect with no seeds, list with one); qemu-flow seed-lifecycle",
    "SeedMnemonicEntryView": "test_mnemonic_entry_flow, 12 and 24 words typed key by key: test_seed_flows.c",
    "SeedMnemonicInvalidView": "test_invalid_mnemonic: test_seed_flows.c",
    "SeedFinalizeView": "test_scan_seedqr_flow: test_seed_flows.c; qemu-flow seed-lifecycle",
    "SeedAddPassphraseView": "test_passphrase_entry_flow, typed on the wheel: test_seed_flows.c",
    "SeedAddPassphraseExitDialogView": "test_passphrase_entry_flow (discard, edit, skip): test_seed_flows.c",
    "SeedReviewPassphraseView": "test_passphrase_entry_flow: test_seed_flows.c",
    "SeedOptionsView": "test_seed_flows.c; qemu-flow seed-lifecycle",
    "SeedDiscardView": "test_discard_seed_flow: test_seed_flows.c; qemu-flow seed-lifecycle",
    "SeedBackupView": "test_seed_flows.c",
    "SeedWordsWarningView": "test_seed_flows.c",
    "SeedWordsView": "pages of four words, BACK pages back, on to the backup test: test_seed_flows.c",
    "SeedExportXpubSigTypeView": "test_export_xpub_standard_flow, all 21 combinations: test_seed_flows.c",
    "SeedExportXpubScriptTypeView": "test_export_xpub_standard_flow: test_seed_flows.c; qemu-flow seed-lifecycle",
    "SeedExportXpubQRFormatView": "test_export_xpub_standard_flow: test_seed_flows.c; qemu-flow seed-lifecycle",
    "SeedExportXpubWarningView": "test_export_xpub_standard_flow: test_seed_flows.c; qemu-flow seed-lifecycle",
    "SeedExportXpubDetailsView": "BIP-84 zpub on the firmware: qemu-flow seed-lifecycle; test_seed_flows.c",
    "SeedExportXpubQRDisplayView": "parts identical to SeedSigner's (test_qr_encode.c); qemu-flow seed-lifecycle",
    # Address verification: SeedSigner's test__verify_address__singlesig__flow,
    # and 144 addresses from its explorer's lists found at their index
    "AddressVerificationStartView": "single sig, nested, P2WSH routes: test_tools_flows.c; qemu-flow verify-address",
    "AddressVerificationSigTypeView": "nested segwit, BACK: test_tools_flows.c",
    "SeedSelectSeedView": "loaded seeds, a seed loaded mid-flow: test_tools_flows.c; qemu-flow verify-address",
    "SeedAddressVerificationView": "144 addresses at SeedSigner's index and branch, Skip 10, Cancel: test_tools_flows.c; qemu-flow verify-address",
    "SeedAddressVerificationSuccessView": "test_tools_flows.c; qemu-flow verify-address",
    # BIP-85 child seeds: every child mnemonic SeedSigner's code derived
    "SeedBIP85SelectNumWordsView": "test_seed_flows.c",
    "SeedBIP85SelectChildIndexView": "18 children word for word, on five profiles: test_seed_flows.c",
    "SeedBIP85InvalidChildIndexView": "2^31 and an empty index: test_seed_flows.c",
    # The backup test and SeedQR transcription
    "SeedWordsBackupTestPromptView": "verify and skip, loaded and pending seeds: test_seed_flows.c",
    "SeedWordsBackupTestView": "every word asked once, decoys, secure RNG: test_seed_flows.c",
    "SeedWordsBackupTestMistakeView": "try again, review words: test_seed_flows.c",
    "SeedWordsBackupTestSuccessView": "test_seed_flows.c",
    "SeedTranscribeSeedQRFormatView": "standard and compact, 12 and 24 words: test_seed_flows.c; qemu-flow seedqr-transcribe",
    "SeedTranscribeSeedQRWarningView": "test_seed_flows.c; qemu-flow seedqr-transcribe",
    "SeedTranscribeSeedQRWholeQRView": "the panel reads back as the SeedQR, every format, five profiles: test_seed_flows.c; qemu-flow seedqr-transcribe",
    "SeedTranscribeSeedQRZoomedInView": "every zone, A-1 to F-6: test_seed_flows.c; qemu-flow seedqr-transcribe",
    "SeedTranscribeSeedQRConfirmQRPromptView": "test_transcribe_seedqr_and_verify: test_seed_flows.c; qemu-flow seedqr-transcribe",
    "SeedTranscribeSeedQRConfirmScanView": "test_transcribe_seedqr_and_verify: test_seed_flows.c",
    "SeedTranscribeSeedQRConfirmWrongSeedView": "test_transcribe_seedqr_and_verify: test_seed_flows.c",
    "SeedTranscribeSeedQRConfirmInvalidQRView": "unreadable and non-SeedQR codes: test_seed_flows.c",
    "SeedTranscribeSeedQRConfirmSuccessView": "test_transcribe_seedqr_and_verify: test_seed_flows.c",
    # PSBT signing: SeedSigner's tests/test_flows_psbt.py, ported in
    # sim/test_psbt_flows.c, plus every PSBT of its parser tests walked
    # through the screens to the signed QR ("the sweep")
    "PSBTSelectSeedView": "every flow test, \"(?)\" hint, autoroute: test_psbt_flows.c; qemu-flow psbt-sign",
    "PSBTOverviewView": "test_psbt_flows.c (the sweep checks spend, change, fee per PSBT); qemu-flow psbt-sign",
    "PSBTUnsupportedScriptTypeWarningView": "an unrecognised input script: test_psbt_flows.c (SeedSigner's own check never fires)",
    "PSBTNoChangeWarningView": "the sweep, 65 PSBTs: test_psbt_flows.c",
    "PSBTHighFeeWarningView": "the sweep, 21 PSBTs: test_psbt_flows.c",
    "PSBTMathView": "the sweep; BACK skips the warnings: test_psbt_flows.c; qemu-flow psbt-sign",
    "PSBTAddressDetailsView": "every recipient's address and amount, the sweep: test_psbt_flows.c; qemu-flow psbt-sign",
    "PSBTChangeDetailsView": "every change output and path, the sweep; multisig skips verification until descriptors (T47): test_psbt_flows.c",
    "PSBTSeedCannotSignView": "test_wrong_seed_routes_back_to_seed_selection_flow: test_psbt_flows.c",
    "PSBTOutputOwnershipClaimFailedView": "test_forged_output_claim_terminates_signing_flow: test_psbt_flows.c",
    "PSBTInputOwnershipClaimFailedView": "test_forged_input_claim_terminates_signing_flow: test_psbt_flows.c",
    "PSBTSurplusDerivationPathsView": "test_surplus_derivation_paths_terminate_signing_flow: test_psbt_flows.c",
    "PSBTMixedDerivationPathTypesView": "test_mixed_derivation_path_types_terminate_signing_flow: test_psbt_flows.c",
    "PSBTOutputOwnershipContradictionView": "test_output_ownership_contradiction_terminates_signing_flow: test_psbt_flows.c",
    "PSBTOpReturnView": "test_parse_and_display_op_return_content: test_psbt_flows.c",
    "PSBTFinalizeView": "signs to SeedSigner's exact PSBT, 155 PSBTs: test_psbt_flows.c; qemu-flow psbt-sign",
    "PSBTSignedQRDisplayView": "test_psbt_flows.c; qemu-flow psbt-sign",
    "PSBTSigningErrorView": "test_scan_multisig_psbt_seed_already_signed_flow: test_psbt_flows.c",
    # Tools: SeedSigner has no flow tests for these, so its own Views were run
    # through its FlowTest harness to record the results (gen_tools_vectors.py)
    "ToolsDiceEntropyMnemonicLengthView": "test_tools_flows.c; qemu-flow dice-seed",
    "ToolsDiceEntropyEntryView": "12 roll sequences give SeedSigner's mnemonics; DEL, BACK wipes: test_tools_flows.c; qemu-flow dice-seed",
    "ToolsCalcFinalWordNumWordsView": "test_tools_flows.c",
    "ToolsCalcFinalWordFinalizePromptView": "all three entropy sources, BACK: test_tools_flows.c",
    "ToolsCalcFinalWordCoinFlipsView": "test_tools_flows.c",
    "ToolsCalcFinalWordShowFinalWordView": "18 cases: SeedSigner's bits, checksum and final word: test_tools_flows.c",
    "ToolsCalcFinalWordDoneView": "load and discard: test_tools_flows.c",
    # The address explorer: SeedSigner's test_flows_tools.py cases, plus every
    # address list its own explorer made (960 addresses)
    "ToolsAddressExplorerSelectSourceView": "test__address_explorer__flow, loadseed sideflow, wrong QR type: test_tools_flows.c; qemu-flow address-explorer",
    "ToolsAddressExplorerAddressTypeView": "all four back_button scenarios: test_tools_flows.c; qemu-flow address-explorer",
    "ToolsAddressExplorerAddressListView": "48 lists, 960 addresses as SeedSigner's, pages and BACK: test_tools_flows.c; qemu-flow address-explorer",
    "ToolsAddressExplorerAddressView": "returns to the list at the address: test_tools_flows.c; qemu-flow address-explorer",
}


def extract(ss):
    rows = []
    for path in sorted(glob.glob(os.path.join(ss, "src", "seedsigner", "views", "*.py"))):
        tree = ast.parse(open(path).read())
        for node in tree.body:
            # The abstract base class is not a screen.
            if isinstance(node, ast.ClassDef) and node.name.endswith("View") and node.name != "View":
                rows.append((os.path.basename(path), node.name))
    commit = subprocess.run(["git", "-C", ss, "rev-parse", "HEAD"], capture_output=True,
                            text=True).stdout.strip() or "unknown"
    with open(VIEWS, "w") as f:
        f.write("# SeedSigner's View classes at %s, extracted by scripts/coverage.py\n" % commit)
        for module, name in rows:
            f.write("%s %s\n" % (module, name))
    print("%d views from SeedSigner %s" % (len(rows), commit))


def load():
    rows = []
    for line in open(VIEWS):
        if line.startswith("#") or not line.strip():
            continue
        module, name = line.split()
        rows.append((module, name))
    return rows


def svg(areas, done_total, total):
    pct = 100.0 * done_total / total
    W, row, top, barx, barw = 860, 34, 70, 250, 380
    H = top + row * len(areas) + 34
    o = ['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d" '
         'font-family="-apple-system,Segoe UI,Helvetica,Arial,sans-serif">' % (W, H, W, H),
         '<style>.t{fill:#57606a;font-size:13px}.h{fill:#24292f;font-size:16px;font-weight:600}'
         '.n{fill:#57606a;font-size:12px}.track{fill:#d0d7de}.done{fill:#f7931a}'
         '@media (prefers-color-scheme:dark){.t,.n{fill:#9198a1}.h{fill:#e6edf3}.track{fill:#30363d}}</style>',
         '<text class="h" x="10" y="24">SeedSigner on Leek BTC Signer: %d of %d views (%.0f%%)</text>'
         % (done_total, total, pct),
         '<rect class="track" x="10" y="36" width="%d" height="14" rx="7"/>' % (W - 20),
         '<rect class="done" x="10" y="36" width="%.1f" height="14" rx="7"/>' % ((W - 20) * pct / 100)]
    for i, (area, done, n) in enumerate(areas):
        y = top + i * row
        o.append('<text class="t" x="10" y="%d">%s</text>' % (y + 14, area))
        o.append('<rect class="track" x="%d" y="%d" width="%d" height="12" rx="6"/>' % (barx, y + 3, barw))
        if done:
            o.append('<rect class="done" x="%d" y="%d" width="%.1f" height="12" rx="6"/>'
                     % (barx, y + 3, barw * done / n))
        o.append('<text class="n" x="%d" y="%d">%d of %d views</text>' % (barx + barw + 10, y + 14, done, n))
    o.append('<text class="n" x="10" y="%d">Each of SeedSigner\'s views counts when it works on the '
             'device and a test proves it (docs/COVERAGE.md).</text>' % (H - 10))
    o.append("</svg>")
    with open(OUT_SVG, "w") as f:
        f.write("\n".join(o) + "\n")


def report():
    rows = load()
    names = {n for _, n in rows}
    unknown = sorted(set(DONE) - names)
    if unknown:
        sys.exit("not SeedSigner views: %s" % ", ".join(unknown))
    total = len(rows)
    done_total = sum(1 for _, n in rows if n in DONE)
    areas = []
    for module, area in AREAS:
        n = [v for m, v in rows if m == module]
        if n:
            areas.append((area, sum(1 for v in n if v in DONE), len(n)))
    svg(areas, done_total, total)

    header = open(VIEWS).readline().strip("# \n")
    md = ["# SeedSigner coverage", "",
          "How much of SeedSigner a user can do on this device. The list is every",
          "View class in SeedSigner's `src/seedsigner/views/` (%s)." % header, "",
          "A view counts as done only when the device does what SeedSigner's view",
          "does and a test proves it. Every view weighs the same. `scripts/coverage.py`",
          "writes this page and the README chart.", "",
          "**%d of %d views: %.1f%%.**" % (done_total, total, 100.0 * done_total / total), ""]
    for module, area in AREAS:
        views = [v for m, v in rows if m == module]
        if not views:
            continue
        d = sum(1 for v in views if v in DONE)
        md += ["## %s: %d of %d" % (area, d, len(views)), "", "| View | Done | Evidence |", "|---|---|---|"]
        for v in views:
            md.append("| `%s` | %s | %s |" % (v, "✅" if v in DONE else "", DONE.get(v, "")))
        md.append("")
    with open(OUT_MD, "w") as f:
        f.write("\n".join(md))

    filled = int(round(20 * done_total / total))
    print("%d of %d views: %.1f%%" % (done_total, total, 100.0 * done_total / total))
    print("SeedSigner  %s%s  %d of %d views" % ("█" * filled, "░" * (20 - filled), done_total, total))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--extract", metavar="SEEDSIGNER")
    args = ap.parse_args()
    if args.extract:
        extract(os.path.abspath(args.extract))
    report()


if __name__ == "__main__":
    main()
