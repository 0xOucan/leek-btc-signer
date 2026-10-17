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
    "MainMenuView": "HOME: sim/test_ui.c; qemu-flow home-menus",
    "BackStackView": "the screen stack: sim/test_ui.c (ten levels, BACK and HOLD)",
    "NotYetImplementedView": "the placeholder screens: sim/test_ui.c walks every one",
    "ErrorView": "status screens: sim/test_screens.c; Wrong QR Type in qemu-flow",
    "LoadSeedView": "LOAD_SEED, SeedSigner's menu with Electrum off (its default): qemu-flow home-menus",
    "ScanView": "SCAN: sim/test_scan.c (every recorded scan); qemu-flow scan-*",
    "ScanSeedQRView": "SCAN_SEED: sim/test_scan.c; qemu-flow scan-seedqr-wrong-type",
    "ScanInvalidQRTypeView": "invalid code: sim/test_scan.c; qemu-flow scan-invalid",
    "ToolsMenuView": "TOOLS, SeedSigner's five entries: qemu-flow home-menus",
    "IOTestView": "IO_TEST: qemu-flow io-test",
    "VersionView": "VERSION: qemu-flow home-menus",
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
