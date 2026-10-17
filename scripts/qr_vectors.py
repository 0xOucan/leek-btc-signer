"""
Read scans back out of sim/vectors/seedsigner_qr.h, so QEMU flows can feed
the firmware the same frames, recorded from SeedSigner, that the host suites
check. Only what the flows need: a scan's frames and its PSBT.
"""
import os
import re

HEADER = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                      "sim", "vectors", "seedsigner_qr.h")
_LIT = r'"((?:[^"\\]|\\.)*)"'


def _unescape(lit):
    out, i = bytearray(), 0
    while i < len(lit):
        c = lit[i]
        if c == "\\":
            n = lit[i + 1]
            if n in "01234567":
                out.append(int(lit[i + 1:i + 4], 8))
                i += 4
                continue
            out.append(ord(n))
            i += 2
            continue
        out.append(ord(c))
        i += 1
    return bytes(out)


def scan(name):
    """(frames, psbt_hex or None) of the scan called `name`."""
    text = open(HEADER).read()
    table = text[text.index("QR_SCANS[] = {"):]
    row = re.compile(r"\{ " + _LIT + r", QRV_F(\d+), \d+, \w+, \w+, (NULL|" + _LIT + ")")
    m = row.search(table)
    while m and _unescape(m.group(1)).decode() != name:
        m = row.search(table, m.end())
    if not m:
        raise KeyError(name)
    psbt = None if m.group(3) == "NULL" else _unescape(m.group(4)).decode()
    start = text.index("QRV_F%s[] = {" % m.group(2))
    body = text[start:text.index("};", start)]
    frames = [_unescape(f) for f in re.findall(r"\{ " + _LIT + r", \d+,", body)]
    return frames, psbt
