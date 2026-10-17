#!/usr/bin/env python3
"""
Generate sim/vectors/seedsigner_ui.h: what SeedSigner's own GUI components
put on screen, for the C widgets to be checked against.

    python gen_ui_vectors.py --seedsigner PATH > sim/vectors/seedsigner_ui.h

SeedSigner's components are run headless the way its screenshot generator
runs them (tests/screenshot_generator/generator.py mocks the display
drivers), and every string they draw is recorded:

  * BtcAmount: the text for an amount under each denomination setting
    (btc, sats, threshold, hybrid) and network.
  * FormattedAddress: how an address is cut into lines at a given number of
    characters per line and a line limit, and which parts are accented.

Needs SeedSigner's pinned requirements (see README.md).
"""
import argparse
import os
import subprocess
import sys
from unittest.mock import MagicMock


def c_str(s):
    out = []
    for ch in s.encode("utf-8"):
        c = chr(ch)
        if c in '"\\':
            out.append("\\" + c)
        elif 0x20 <= ch < 0x7F and c != "?":
            out.append(c)
        else:
            out.append("\\%03o" % ch)
    return '"' + "".join(out) + '"'


def main():
    # SeedSigner prints to stdout as it starts; the header goes there too.
    real_stdout = sys.stdout
    sys.stdout = sys.stderr
    try:
        rows = collect()
    finally:
        sys.stdout = real_stdout
    emit(*rows)


def collect():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seedsigner", required=True)
    args = ap.parse_args()
    ss = os.path.abspath(args.seedsigner)
    sys.path.insert(0, os.path.join(ss, "src"))

    # As SeedSigner's screenshot generator does: no Raspberry Pi hardware.
    for m in ["seedsigner.hardware.displays.st7789_mpy", "seedsigner.hardware.displays.ili9341",
              "RPi", "RPi.GPIO", "seedsigner.hardware.camera.Camera", "seedsigner.hardware.microsd",
              "spidev", "seedsigner.hardware.displays.ST7789"]:
        sys.modules[m] = MagicMock()

    from PIL import ImageDraw
    from seedsigner.gui.renderer import Renderer
    from seedsigner.gui import components as comp
    from seedsigner.models.settings import Settings, SettingsConstants as SC

    # SeedSigner's own test-only renderer, patched in as its generator does.
    sys.path.insert(0, ss)
    from tests.screenshot_generator.utils import ScreenshotRenderer
    ScreenshotRenderer.configure_instance()
    renderer = ScreenshotRenderer.get_instance()
    Renderer.configure_instance = MagicMock()
    Renderer.get_instance = MagicMock(return_value=renderer)
    settings = Settings.get_instance()

    drawn = []
    original_text = ImageDraw.ImageDraw.text

    def record(self, xy, text=None, *a, **kw):
        drawn.append(text if text is not None else kw.get("text"))
        return original_text(self, xy, text, *a, **kw)

    ImageDraw.ImageDraw.text = record

    denoms = [("BTC_DENOM_BTC", SC.BTC_DENOMINATION__BTC), ("BTC_DENOM_SATS", SC.BTC_DENOMINATION__SATS),
              ("BTC_DENOM_THRESHOLD", SC.BTC_DENOMINATION__THRESHOLD),
              ("BTC_DENOM_HYBRID", SC.BTC_DENOMINATION__BTCSATSHYBRID)]
    nets = [("BTC_NET_MAIN", SC.MAINNET), ("BTC_NET_TEST", SC.TESTNET), ("BTC_NET_REGTEST", SC.REGTEST)]
    amounts = [0, 1, 7, 99, 999, 1000, 1001, 12345, 99999, 100000, 543210, 999999, 1000000,
               1000001, 1234567, 9999999, 10000000, 12000000, 12345678, 50000000, 99999999,
               100000000, 100000001, 123456789, 120000000, 1000000000, 1234567890, 2100000000,
               9999999999, 10000000000, 10000000001, 12345678901, 210000000000, 2100000000000000,
               1000000100, 3000000, 3000001, 1000000001]
    amount_rows = []
    for dname, dval in denoms:
        settings.set_value(SC.SETTING__BTC_DENOMINATION, dval)
        for nname, nval in nets:
            settings.set_value(SC.SETTING__NETWORK, nval)
            for sats in amounts:
                drawn.clear()
                comp.BtcAmount(total_sats=sats)
                # Icons are private-use glyphs; keep the text.
                texts = [t for t in drawn if t and all(ord(c) < 0x2000 for c in t)]
                amount_rows.append((sats, dname, nname, "\t".join(texts)))

    # FormattedAddress: choose widths that give 8 to 26 characters a line.
    font = comp.Fonts.get_font(comp.GUIConstants.FIXED_WIDTH_FONT_NAME, 24)
    left, top, right, bottom = font.getbbox("Q")
    cw = right - left
    addrs = ["bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu", "37VucYSaXLCAsxYyAPfbSi9eh4iEcbShgf",
             "bc1p5cyxnuxmeuwuvkwfem96lqzszd02n6xdcjrs20cac6yqjjwudpxqkedrcr",
             "bcrt1q6rz28mcfaxtmd6v789l9rrlrusdprr9pz3cppk", "1LqBGSKuX5yYUonjxT5qGfpUsXKYYWeabA",
             "bc1qwqdg6squsna38e46795at95yu9atm8azzmyvckulcc7kytlcckxswvvzej"]
    addr_rows = []
    for a in addrs:
        for chars in [10, 12, 14, 16, 17, 18, 20, 21, 22, 24, 26]:
            for max_lines in [None, 1, 2, 3]:
                try:
                    fa = comp.FormattedAddress(width=chars * cw + cw - 1, address=a, max_lines=max_lines)
                except Exception as e:
                    # Some widths make SeedSigner's own layout raise; record it.
                    addr_rows.append((a, chars, max_lines or 0, "!" + type(e).__name__))
                    continue
                lines = {}
                for (x, y), text, color, f in fa.text_params:
                    lines.setdefault(y, []).append((x, text, color == comp.GUIConstants.ACCENT_COLOR))
                out = []
                for y in sorted(lines):
                    segs = sorted(lines[y])
                    x0 = min(s[0] for s in segs)
                    line, col = "", 0
                    for x, text, accent in segs:
                        pad = (x - x0) // cw - col
                        line += " " * max(0, pad) + ("[" + text + "]" if accent else text)
                        col += max(0, pad) + len(text)
                    out.append(line)
                addr_rows.append((a, chars, max_lines or 0, "\n".join(out)))

    commit = subprocess.run(["git", "-C", ss, "rev-parse", "HEAD"], capture_output=True,
                            text=True).stdout.strip() or "unknown"
    return amount_rows, addr_rows, commit


def emit(amount_rows, addr_rows, commit):
    w = sys.stdout.write
    w("/* Generated by scripts/vectors/gen_ui_vectors.py from SeedSigner %s.\n" % commit)
    w(" * Do not edit. SeedSigner is MIT licensed, Copyright (c) 2021 SeedSigner. */\n")
    w("#pragma once\n#include <stdint.h>\n#include \"widgets.h\"\n\n")
    w("/* Every text string BtcAmount drew (icons left out), in order, tab-separated. */\n")
    w("typedef struct { uint64_t sats; btc_denom_t denom; btc_net_t net; const char *drawn; } uiv_amount_t;\n")
    w("static const uiv_amount_t UI_AMOUNTS[] = {\n")
    for sats, d, n, t in amount_rows:
        w("    { %dull, %s, %s, %s },\n" % (sats, d, n, c_str(t)))
    w("};\n\n")
    w("/* FormattedAddress lines, accented runs in [brackets], at `chars` a line. */\n")
    w("typedef struct { const char *address; int chars, max_lines; const char *lines; } uiv_address_t;\n")
    w("static const uiv_address_t UI_ADDRESSES[] = {\n")
    for a, c, m, l in addr_rows:
        w("    { %s, %d, %d, %s },\n" % (c_str(a), c, m, c_str(l)))
    w("};\n")


if __name__ == "__main__":
    main()
