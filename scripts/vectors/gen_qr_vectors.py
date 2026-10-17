#!/usr/bin/env python3
"""
Generate sim/vectors/seedsigner_qr.h: what SeedSigner's QR code does, for the
C port to be checked against.

    python gen_qr_vectors.py --seedsigner PATH > sim/vectors/seedsigner_qr.h

Three parts, all produced by running SeedSigner's own code:

  * Scans. Every scan in SeedSigner's tests/test_decodepsbtqr.py, captured
    frame by frame with qr_capture.py, plus generated ones: SeedSigner's UR
    encoder at each density with frames dropped, BBQr in all three encodings,
    Specter pXofY, base64 and base43, every seed format, addresses and URIs,
    sign-message requests, settings, and the frames that make SeedSigner
    raise. For each frame: the status add_data() returned, the type, and the
    progress; for each scan: what the getters return.
  * Detection. detect_segment_type() over every frame above and a list of
    edge cases (Unicode whitespace, invalid UTF-8, near-miss prefixes).
  * Encoding. SeedQR, CompactSeedQR, static and Specter xpubs, UR
    crypto-account and UR crypto-psbt at each density, part by part.

Needs SeedSigner's pinned requirements and pytest (see README.md).
"""
import argparse
import base64
import glob
import json
import os
import random
import re
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


# ---------------------------------------------------------------- C output

def c_bytes(b):
    """A C string literal for arbitrary bytes. Octal escapes are fixed width,
    so nothing that follows can extend them, and '?' is escaped so no
    trigraph can form."""
    out = []
    for c in b:
        ch = chr(c)
        if ch == '"' or ch == "\\":
            out.append("\\" + ch)
        elif 0x20 <= c < 0x7F and ch != "?":
            out.append(ch)
        else:
            out.append("\\%03o" % c)
    return '"' + "".join(out) + '"'


def c_str(s):
    return "NULL" if s is None else c_bytes(s.encode("utf-8"))


def c_type(t):
    return "QR_TYPE_" + t.upper().replace("__", "_")


STATUS = {1: "QR_STATUS_PART_COMPLETE", 2: "QR_STATUS_PART_EXISTING", 3: "QR_STATUS_COMPLETE",
          4: "QR_STATUS_FALSE", 5: "QR_STATUS_INVALID", "type_changed": "QR_STATUS_TYPE_CHANGED",
          "error": "QR_STATUS_ERROR"}
SCRIPTS = {"leg": "BTC_SCRIPT_P2PKH", "nes": "BTC_SCRIPT_P2SH_P2WPKH",
           "nat": "BTC_SCRIPT_P2WPKH", "tr": "BTC_SCRIPT_P2TR"}
NETS = {"M": "BTC_NET_MAIN", "T": "BTC_NET_TEST", "R": "BTC_NET_REGTEST"}


# ---------------------------------------------------------------- encodings
# Encoders for the formats SeedSigner only decodes. What they produce goes
# through SeedSigner's decoder, and only its verdict is recorded, so these
# need to be plausible, not authoritative.

B43 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ$*+-./:"


def base43_encode(data):
    n = int.from_bytes(data, "big")
    out = ""
    while n > 0:
        n, r = divmod(n, 43)
        out = B43[r] + out
    lead = len(data) - len(data.lstrip(b"\0"))
    return "0" * lead + out


def b36(n):
    d = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    return d[n // 36] + d[n % 36]


def bbqr_parts(raw, encoding, chunk):
    """BBQr: B$ <encoding> P <total> <index> <data>, each part decodable on
    its own (base32 parts are multiples of 8 characters)."""
    if encoding == "H":
        body = raw.hex().upper()
        step = chunk - chunk % 2
    else:
        if encoding == "Z":
            z = zlib.compressobj(wbits=-10)
            raw = z.compress(raw) + z.flush()
        body = base64.b32encode(raw).decode().rstrip("=")
        step = chunk - chunk % 8
    pieces = [body[i:i + step] for i in range(0, len(body), step)] or [""]
    n = len(pieces)
    return ["B$%sP%s%s%s" % (encoding, b36(n), b36(i), p) for i, p in enumerate(pieces)]


# ---------------------------------------------------------------- scans

def run_scan(DecodeQR, Recorder, name, frames, stop_at_complete=False):
    d = DecodeQR()
    rec = Recorder(name)
    for f in frames:
        try:
            st = rec.add(d, f, DecodeQR.add_data)
        except Exception:
            continue
        if stop_at_complete and st == 3:
            break
    return rec.result(d)


def psbts_from_tests(ss):
    """Every distinct base64 PSBT literal in SeedSigner's tests."""
    found = set()
    for path in glob.glob(os.path.join(ss, "tests", "*.py")):
        for m in re.finditer(r"cHNidP8[A-Za-z0-9+/=]+", open(path).read()):
            found.add(m.group(0))
    out = []
    for s in sorted(found):
        try:
            out.append(base64.b64decode(s, validate=True))
        except Exception:
            pass
    return sorted(out, key=lambda b: (len(b), b))


def generated_scans(ss, mods, rng):
    DecodeQR, Recorder = mods["DecodeQR"], mods["Recorder"]
    PSBT, Seed = mods["PSBT"], mods["Seed"]
    UrPsbtQrEncoder, SC = mods["UrPsbtQrEncoder"], mods["SettingsConstants"]
    SeedQrEncoder, CompactSeedQrEncoder = mods["SeedQrEncoder"], mods["CompactSeedQrEncoder"]
    UrXpubQrEncoder = mods["UrXpubQrEncoder"]
    scans = []

    def scan(name, frames, **kw):
        scans.append(run_scan(DecodeQR, Recorder, name, frames, **kw))

    psbts = psbts_from_tests(ss)
    parsable = []
    for raw in psbts:
        try:
            PSBT.parse(raw)
            parsable.append(raw)
        except Exception:
            pass
    # Small, middling and the largest, so the fountain sees 1 to ~200 parts.
    pick = sorted(set([0, 1, 2, len(parsable) // 4, len(parsable) // 2,
                       3 * len(parsable) // 4, len(parsable) - 2, len(parsable) - 1]))
    chosen = [parsable[i] for i in pick if 0 <= i < len(parsable)]

    densities = [("low", SC.DENSITY__LOW), ("medium", SC.DENSITY__MEDIUM), ("high", SC.DENSITY__HIGH)]
    for pi, raw in enumerate(chosen):
        for dname, dens in densities:
            if dname == "low" and len(raw) > 800:
                continue
            e = UrPsbtQrEncoder(psbt=PSBT.parse(raw), qr_density=dens)
            n = e.seq_len()
            parts = [e.next_part() for _ in range(min(3 * n + 4, 400))]
            scan("ur_psbt_%d_%s_in_order" % (pi, dname), parts[:n])
            # Join late and drop a third of what goes past, as a hand does.
            start = rng.randrange(0, max(1, n))
            lossy = [p for p in parts[start:] if rng.random() > 0.35]
            scan("ur_psbt_%d_%s_lossy" % (pi, dname), lossy)
            if n > 1:
                scan("ur_psbt_%d_%s_fountain_only" % (pi, dname), parts[n:])
                scan("ur_psbt_%d_%s_repeats" % (pi, dname), parts[:2] + parts[:2] + parts[1:n])

    for pi, raw in enumerate(chosen[:6]):
        for enc in "H2Z":
            parts = bbqr_parts(raw, enc, 200 if pi % 2 else 600)
            scan("bbqr_%d_%s" % (pi, enc), parts)
            if len(parts) > 1:
                shuffled = parts[:] + parts[:1]
                rng.shuffle(shuffled)
                scan("bbqr_%d_%s_shuffled" % (pi, enc), shuffled)
    raw = chosen[2]
    parts = bbqr_parts(raw, "2", 200)
    scan("bbqr_bad_base32", [parts[0][:12] + "1" + parts[0][13:]] + parts[1:])
    scan("bbqr_lowercase_data", [p[:8] + p[8:].lower() for p in parts])
    scan("bbqr_padding_in_data", [parts[0] + "=" * 3] + parts[1:])
    zparts = bbqr_parts(raw, "Z", 200)
    scan("bbqr_truncated_zlib", zparts[:-1] + [zparts[-1][:-8]] if len(zparts[-1]) > 16 else zparts)
    hparts = bbqr_parts(raw, "H", 200)
    scan("bbqr_hex_spaces", [hparts[0][:8] + " ".join(hparts[0][8 + i:8 + i + 2] for i in range(0, len(hparts[0]) - 8, 2))] + hparts[1:])
    scan("bbqr_index_past_total", [hparts[0][:6] + b36(len(hparts) + 3) + hparts[0][8:]])
    scan("bbqr_total_zero", ["B$HP0000" + hparts[0][8:]])
    if len(hparts) > 1:
        scan("bbqr_total_changes", [hparts[0], hparts[1][:4] + b36(len(hparts) + 1) + hparts[1][6:]])

    for pi, raw in enumerate(chosen[:6]):
        b64 = base64.b64encode(raw).decode()
        size = 120 + 80 * pi
        pieces = [b64[i:i + size] for i in range(0, len(b64), size)]
        n = len(pieces)
        parts = ["p%dof%d %s" % (i + 1, n, p) for i, p in enumerate(pieces)]
        scan("specter_psbt_%d" % pi, parts)
        if n > 1:
            mixed = parts[::-1] + parts[:1]
            scan("specter_psbt_%d_reversed" % pi, mixed)
            scan("specter_psbt_%d_p0" % pi, ["p0of%d %s" % (n, pieces[-1])] + parts)
            scan("specter_psbt_%d_past_total" % pi, [parts[0], "p%dof%d %s" % (n + 1, n, pieces[0])])
            scan("specter_psbt_%d_total_changes" % pi, [parts[0], "p2of%d %s" % (n + 1, pieces[1])])
        scan("specter_psbt_%d_upper" % pi, [p.replace("p", "P", 1).replace("of", "OF", 1) for p in parts])
        scan("base64_psbt_%d" % pi, [b64])
        scan("base43_psbt_%d" % pi, [base43_encode(raw)])
    scan("specter_psbt_noncanonical", ["p1of2 " + base64.b64encode(chosen[1]).decode()[:-4], "p2of2 QQ=="])
    scan("specter_total_zero", ["p1of0 QUJD"])
    scan("base43_leading_zeros", ["00" + base43_encode(chosen[0])])

    # Seeds
    mnemonics = [
        "attack pizza motion avocado network gather crop fresh patrol unusual wild holiday candy pony ranch winter theme error hybrid van cereal salon goddess expire",
        "forum undo fragile fade shy sign arrest garment culture tube off merit",
        "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about",
    ]
    from embit import bip39
    for i in range(10):
        mnemonics.append(bip39.mnemonic_from_bytes(bytes(rng.randrange(256) for _ in range(16 if i % 2 else 32))))
    for i, m in enumerate(mnemonics):
        words = m.split()
        digits = SeedQrEncoder(mnemonic=words).next_part()
        compact = CompactSeedQrEncoder(mnemonic=words).next_part()
        scan("seedqr_%d" % i, [digits])
        scan("compactseedqr_%d" % i, [compact])
        scan("mnemonic_%d" % i, [m])
        scan("mnemonic4_%d" % i, [" ".join(w[:4] for w in words)])
    d0 = SeedQrEncoder(mnemonic=mnemonics[1].split()).next_part()
    scan("seedqr_negative_index", ["-001" + d0[4:]])
    scan("seedqr_spaced_chunk", [" 001" + d0[4:] + "0000"])
    scan("seedqr_underscore_chunk", ["0_01" + d0[4:]])
    scan("seedqr_index_too_big", ["9999" + d0[4:]])
    scan("seedqr_50_digits", [d0 + "77"])
    scan("seedqr_13_words", [d0 + "0001"])
    scan("seedqr_embedded", ["xx" + d0 + "yy"])
    scan("seedqr_bad_checksum", [d0[:-4] + "0000"])
    scan("seedqr_100_digits", [d0 + d0[:52]])
    scan("mnemonic_bad_checksum", ["abandon " * 11 + "abandon"])
    scan("mnemonic_13_words", ["abandon " * 12 + "about"])
    scan("mnemonic_padded", ["　 " + mnemonics[1] + "\n "])
    scan("mnemonic_double_space", [mnemonics[1].replace(" ", "  ", 1)])
    scan("mnemonic4_short_words", ["add " + " ".join(w[:4] for w in mnemonics[1].split()[1:])])
    scan("compact_then_address", [CompactSeedQrEncoder(mnemonic=mnemonics[0].split()).next_part(),
                                  "bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu"])
    scan("compact_binary_16", [bytes(range(0x80, 0x90))])
    scan("compact_binary_32", [bytes(range(0xE0, 0x100))])

    # Addresses
    addrs = ["1LqBGSKuX5yYUonjxT5qGfpUsXKYYWeabA", "mkpZhYtJu2r87Js3pDiWJDmPte2NRZ8bJV",
             "37VucYSaXLCAsxYyAPfbSi9eh4iEcbShgf", "2Mww8dCYPUpKHofjgcXcBCEGmniw9CoaiD2",
             "bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu", "tb1q6rz28mcfaxtmd6v789l9rrlrusdprr9pqcpvkl",
             "bcrt1q6rz28mcfaxtmd6v789l9rrlrusdprr9pz3cppk",
             "bc1p5cyxnuxmeuwuvkwfem96lqzszd02n6xdcjrs20cac6yqjjwudpxqkedrcr",
             "tb1p8wpt9v4frpf3tkn0srd97pksgsxc5hs52lafxwru9kgeephvs7rqlqt9zj",
             "bcrt1p8wpt9v4frpf3tkn0srd97pksgsxc5hs52lafxwru9kgeephvs7rqjeprhg",
             "n1KSZGmQgB8iSZqv6UVhGkCGUbEdw8Lm3Q",
             "bc1qwqdg6squsna38e46795at95yu9atm8azzmyvckulcc7kytlcckxswvvzej"]
    for i, a in enumerate(addrs):
        for j, v in enumerate([a, "bitcoin:" + a, "BITCOIN:" + a + "?amount=0.1", a.upper(),
                               "bitcoin:" + a.upper() + "?label=x:y", a + "\n"]):
            scan("address_%d_%d" % (i, j), [v])
    for i, v in enumerate(["bitcoin=bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq",
                           "bitcoin:bcfakehrp1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq",
                           "abcbc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq",
                           "bitcoin:abcbc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq",
                           "bitcon:bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq",
                           "bitcoin:", "bitcoin:1short", "bitcoin:bc1q" + "a" * 70,
                           "1" + "A" * 25, "1" + "A" * 63, "bc1x" + "q" * 30]):
        scan("address_odd_%d" % i, [v])

    # Sign message, settings
    for i, v in enumerate(["signmessage m/84h/0h/0h/0/0 ascii:hello world",
                           "signmessage m/84h/0h/0h/0/0 ascii:a ascii:b",
                           "signmessage m/84'/0'/0'/0/0 ascii:",
                           "signmessage m/84h/1h/0h/0/3 utf8:hola",
                           "signmessage m/84h/0h/0h/0/0",
                           "signmessage m/84h/0h/0h/0/0 ascii",
                           "signmessage  m/44h/0h/0h/0/0\tascii:tabs　and more",
                           "signmessageX ascii:q ascii:r"]):
        scan("signmessage_%d" % i, [v])
    for i, v in enumerate(["settings::v1 name=Test persistent=E coords=spa,spd",
                           "settings::", "settings::v1\nname=x"]):
        scan("settings_%d" % i, [v])

    # UR beyond PSBTs: account, bytes, output; and a mid-scan single part.
    seed = Seed(mnemonics[1].split(), passphrase="")
    for dname, dens in densities:
        e = UrXpubQrEncoder(seed=seed, network=SC.MAINNET, derivation="m/84h/0h/0h", qr_density=dens)
        n = e.seq_len()
        scan("ur_account_%s" % dname, [e.next_part() for _ in range(n + 2)])
    UREncoder, UR, Bytes = mods["UREncoder"], mods["UR"], mods["Bytes"]
    text = ("# Coldcard Multisig setup file\nName: test\nPolicy: 2 of 3\n" * 3).encode()
    ue = UREncoder(UR("bytes", Bytes(text).to_cbor()), 30)
    scan("ur_bytes", [ue.next_part().upper() for _ in range(ue.fountain_encoder.seq_len() + 3)])
    single_psbt = UREncoder(UR("crypto-psbt", mods["UR_PSBT"](chosen[0]).to_cbor()), 2000).next_part()
    e = UrPsbtQrEncoder(psbt=PSBT.parse(chosen[3]), qr_density=SC.DENSITY__MEDIUM)
    scan("ur_single_part_mid_scan", [e.next_part(), e.next_part(), single_psbt])
    scan("ur_psbt_lowercase", [p.lower() for p in [UrPsbtQrEncoder(psbt=PSBT.parse(chosen[1]), qr_density=SC.DENSITY__HIGH).next_part() for _ in range(1)]])

    # Types SeedSigner raises on, and the ones decoded with T47.
    scan("type_change", [d0, "bitcoin:bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu"])
    scan("invalid_then_other", ["hello", d0])
    scan("wallet_generic", ["wsh(sortedmulti(1,xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8/0/*))"])
    scan("wallet_configfile", ["# Multisig setup file\nName: x\nPolicy: 1 of 1\n"])
    scan("wallet_specter", ['{"label": "x", "descriptor": "wpkh(...)"}'])
    scan("nothing", ["hello world"])
    return scans


# ---------------------------------------------------------------- detection

def detect_corpus(scans, rng):
    items = []
    for s in scans:
        for f in s["frames"]:
            items.append(bytes.fromhex(f["data"]))
    extra = ["", " ", "\n", "abandon", "　abandon abandon abandon ",
             "UR:CRYPTO-PSBT/", "ur:crypto-psbt/x", "Ur:Bytes/x", "ur:crypto-output/", "ur:crypto-account/",
             "UR:CRYPTO-PSBTX", "ur:bytesx/", "p1of2 abc", "p1of2 abc def", "P01OF02 QUJD", "p1of2 abc\n",
             "p1of2 ab\nc", "p1of2 ", "p1of2", "pof2 abc", "p1o2 abc", "QUJD", "QUJD\n", "B$ZP0100",
             "B$zP0100", "B$HP01", "B$HP01x0", "B$2PZZ0Aab", '{"label": "x", "descriptor": "y"}',
             '{"LABEL":1,"DESCRIPTOR":2}', '{ "label" :1, "descriptor":2}', '{"label"\n:1,"descriptor":2}',
             "Multisig Setup File", "MULTISIG SETUP FILE!", "wsh(sortedmulti(2,a,b))", "SORTEDMULTI",
             "abc" + "1" * 48 + "xyz", "1" * 47, "1" * 47 + "a" + "1" * 47, "bitcoin:", "BITCOIN:abc",
             "bitcoin;x", "signmessage", "signmessag", "settings::", "settings:", "Settings::x",
             "0123456789ABCDEF", "abcdefghijklmnop", "abcdefghijklmnopabcdefghijklmnop",
             "é" * 8, "é" * 16, "add act", "add act zoo", "zoo zoo", "zoo  zoo",
             "1" + "a" * 25, "1" + "a" * 24, "3" + "Z" * 62, "3" + "Z" * 63, "M" + "z" * 30,
             "bcr" + "1" * 25, "tb1" + "x" * 62 + "\n", "tb1" + "x" * 62 + "\n\n", "bc1" + "!" * 30,
             "000", "0", "$*+-./:", "0123456789"]
    for s in extra:
        items.append(s.encode("utf-8"))
    items += [b"\xed\xa0\x80" + b"a" * 13, b"\xc0\x80" * 8, b"\xf0\x9f\x98\x80" * 4,
              b"\xff" * 16, b"\xff" * 17, b"\xff" * 32, b"\x00" * 16, b"\x00" * 33,
              b"abc\x00def\x00ghi\x00jkl\x00", "a b".encode() * 4]
    for _ in range(40):
        items.append(bytes(rng.randrange(256) for _ in range(rng.choice([15, 16, 17, 31, 32, 33]))))
    seen, out = set(), []
    for b in items:
        if b not in seen:
            seen.add(b)
            out.append(b)
    return out


# ---------------------------------------------------------------- encoders

def encoder_vectors(mods, rng):
    Seed, SC = mods["Seed"], mods["SettingsConstants"]
    from embit import bip39
    seqs = []
    seedqrs = []
    mnemonics = ["attack pizza motion avocado network gather crop fresh patrol unusual wild holiday candy pony ranch winter theme error hybrid van cereal salon goddess expire",
                 "forum undo fragile fade shy sign arrest garment culture tube off merit",
                 "obscure bone gas open exotic abuse virus bunker shuffle nasty ship dash"]
    for i in range(12):
        mnemonics.append(bip39.mnemonic_from_bytes(bytes(rng.randrange(256) for _ in range(16 if i % 2 else 32))))
    for m in mnemonics:
        w = m.split()
        seedqrs.append((m, mods["SeedQrEncoder"](mnemonic=w).next_part(),
                        bytes(mods["CompactSeedQrEncoder"](mnemonic=w).next_part()).hex()))

    densities = [("QR_DENSITY_LOW", SC.DENSITY__LOW), ("QR_DENSITY_MEDIUM", SC.DENSITY__MEDIUM),
                 ("QR_DENSITY_HIGH", SC.DENSITY__HIGH)]
    paths = ["m/84h/0h/0h", "m/49h/0h/0h", "m/44h/0h/0h", "m/86h/0h/0h", "m/48h/0h/0h/2h",
             "m/48h/0h/0h/1h", "m/48h/1h/0h/2h", "m/84h/1h/0h", "m/45h", "m/0h", "m/48h/0h/0h",
             "m/48h/0h/0h/3h", "m/84h/0h/7h/1/2", "m/0'/1'"]
    seeds = [(mnemonics[2], "pass"), (mnemonics[1], ""), (mnemonics[0], "TREZOR")]
    xpubs = []
    for si, (m, pw) in enumerate(seeds):
        seed = Seed(m.split(), passphrase=pw)
        for pi, path in enumerate(paths):
            for net in ["M", "T", "R"]:
                if (si + pi) % 2 and net == "R":
                    continue
                static = mods["StaticXpubQrEncoder"](seed=seed, derivation=path, network=net).next_part()
                entry = {"mnemonic": m, "passphrase": pw, "path": path, "net": net, "static": static,
                         "specter": [], "ur": []}
                for dname, dens in densities:
                    e = mods["SpecterLegacyXPubQrEncoder"](seed=seed, network=net, derivation=path, qr_density=dens)
                    entry["specter"].append((dname, [e.next_part() for _ in range(e.seq_len())]))
                    u = mods["UrXpubQrEncoder"](seed=seed, network=net, derivation=path, qr_density=dens)
                    n = u.seq_len()
                    cbor = bytes(u.ur2_encode.ur.cbor).hex()
                    entry["ur"].append((dname, cbor, [u.next_part() for _ in range(n + 3)]))
                xpubs.append(entry)

    psbts = []
    for raw in mods["chosen_psbts"]:
        psbt = mods["PSBT"].parse(raw)
        ser = psbt.serialize()
        for dname, dens in densities:
            if dname == "QR_DENSITY_LOW" and len(ser) > 800:
                continue
            e = mods["UrPsbtQrEncoder"](psbt=psbt, qr_density=dens)
            n = e.seq_len()
            psbts.append((ser.hex(), dname, [e.next_part() for _ in range(min(n + 5, 300))]))
    return seedqrs, xpubs, psbts


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seedsigner", required=True)
    args = ap.parse_args()
    ss = os.path.abspath(args.seedsigner)
    sys.path.insert(0, os.path.join(ss, "src"))
    sys.path.insert(0, HERE)

    from embit.psbt import PSBT
    from seedsigner.models.decode_qr import DecodeQR, DecodeQRStatus  # noqa: F401
    from seedsigner.models.qr_type import QRType  # noqa: F401
    from seedsigner.models.seed import Seed
    from seedsigner.models.settings_definition import SettingsConstants
    from seedsigner.models.encode_qr import (CompactSeedQrEncoder, SeedQrEncoder,
                                             SpecterLegacyXPubQrEncoder, StaticXpubQrEncoder,
                                             UrPsbtQrEncoder, UrXpubQrEncoder)
    from seedsigner.helpers.ur2.ur_encoder import UREncoder
    from seedsigner.helpers.ur2.ur import UR
    from urtypes.bytes import Bytes
    from urtypes.crypto import PSBT as UR_PSBT
    from qr_record import Recorder

    rng = random.Random(0x1eec)
    mods = dict(DecodeQR=DecodeQR, Recorder=Recorder, PSBT=PSBT, Seed=Seed,
                SettingsConstants=SettingsConstants, SeedQrEncoder=SeedQrEncoder,
                CompactSeedQrEncoder=CompactSeedQrEncoder, StaticXpubQrEncoder=StaticXpubQrEncoder,
                SpecterLegacyXPubQrEncoder=SpecterLegacyXPubQrEncoder, UrPsbtQrEncoder=UrPsbtQrEncoder,
                UrXpubQrEncoder=UrXpubQrEncoder, UREncoder=UREncoder, UR=UR, Bytes=Bytes, UR_PSBT=UR_PSBT)

    # SeedSigner's own decode tests, frame by frame.
    with tempfile.TemporaryDirectory() as tmp:
        cap = os.path.join(tmp, "capture.jsonl")
        env = dict(os.environ, LBS_QR_CAPTURE=cap,
                   PYTHONPATH=os.pathsep.join([HERE, os.path.join(ss, "src")]))
        subprocess.run([sys.executable, "-m", "pytest", "-q", "-p", "qr_capture",
                        "-p", "no:cacheprovider", "tests/test_decodepsbtqr.py"],
                       cwd=ss, env=env, stdout=subprocess.DEVNULL, check=True)
        captured = [json.loads(l) for l in open(cap)]

    gen = generated_scans(ss, mods, rng)
    scans = captured + gen
    detect = [(b, DecodeQR.detect_segment_type(b, wordlist_language_code=SettingsConstants.WORDLIST_LANGUAGE__ENGLISH))
              for b in detect_corpus(scans, rng)]

    psbts = psbts_from_tests(ss)
    parsable = [r for r in psbts if _parses(PSBT, r)]
    mods["chosen_psbts"] = [parsable[i] for i in sorted(set([0, len(parsable) // 3, 2 * len(parsable) // 3,
                                                             len(parsable) - 1]))]
    seedqrs, xpubs, enc_psbts = encoder_vectors(mods, rng)

    commit = subprocess.run(["git", "-C", ss, "rev-parse", "HEAD"], capture_output=True,
                            text=True).stdout.strip() or "unknown"
    emit(scans, detect, seedqrs, xpubs, enc_psbts, commit, len(captured))


def _parses(PSBT, raw):
    try:
        PSBT.parse(raw)
        return True
    except Exception:
        return False


def emit(scans, detect, seedqrs, xpubs, enc_psbts, commit, n_captured):
    w = sys.stdout.write
    w("/* Generated by scripts/vectors/gen_qr_vectors.py from SeedSigner %s.\n" % commit)
    w(" * Do not edit. SeedSigner is MIT licensed, Copyright (c) 2021 SeedSigner.\n")
    w(" * The first %d scans are SeedSigner's own tests/test_decodepsbtqr.py. */\n" % n_captured)
    w("#pragma once\n#include <stdbool.h>\n#include \"qr-decode.h\"\n#include \"qr-encode.h\"\n\n")

    w("typedef struct { const char *data; unsigned len; signed char status, type, pct, pctw; } qrv_frame_t;\n")
    w("typedef struct {\n    const char *name;\n    const qrv_frame_t *frames;\n    unsigned n_frames;\n"
      "    signed char type;\n    bool complete;\n    const char *psbt;     /* hex; NULL for None; \"!...\" raised */\n"
      "    const char *seed;\n    const char *address;\n    signed char addr_script, addr_net;\n"
      "    const char *settings, *msg_path, *msg, *ur_cbor;\n} qrv_scan_t;\n\n")

    for i, s in enumerate(scans):
        w("static const qrv_frame_t QRV_F%d[] = {\n" % i)
        for f in s["frames"]:
            b = bytes.fromhex(f["data"])
            pct = -1 if f["pct"] is None else f["pct"]
            pctw = -1 if f["pctw"] is None else f["pctw"]
            w("    { %s, %d, %s, %s, %d, %d },\n" % (c_bytes(b), len(b), STATUS[f["status"]],
                                                     c_type(f["type"]), pct, pctw))
        w("};\n")
    w("\nstatic const qrv_scan_t QR_SCANS[] = {\n")
    for i, s in enumerate(scans):
        at = s.get("address_type")
        w("    { %s, QRV_F%d, %d, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s },\n" % (
            c_str(s["name"]), i, len(s["frames"]), c_type(s["type"]), "true" if s["complete"] else "false",
            c_str(s.get("psbt")), c_str(s.get("seed")), c_str(s.get("address")),
            SCRIPTS[at[0]] if at else "0", NETS[at[1]] if at else "0",
            c_str(s.get("settings")), c_str(s.get("msg_path")), c_str(s.get("msg")), c_str(s.get("ur_cbor"))))
    w("};\n\n")

    w("typedef struct { const char *data; unsigned len; signed char type; } qrv_detect_t;\n")
    w("static const qrv_detect_t QR_DETECT[] = {\n")
    for b, t in detect:
        w("    { %s, %d, %s },\n" % (c_bytes(b), len(b), c_type(t)))
    w("};\n\n")

    w("typedef struct { const char *mnemonic, *standard, *compact_hex; } qrv_seedqr_t;\n")
    w("static const qrv_seedqr_t QR_SEEDQR[] = {\n")
    for m, std, comp in seedqrs:
        w("    { %s, %s, %s },\n" % (c_str(m), c_str(std), c_str(comp)))
    w("};\n\n")

    w("typedef struct { qr_density_t density; const char *cbor_hex; const char *const *parts; unsigned n_parts; } qrv_density_t;\n")
    w("typedef struct {\n    const char *mnemonic, *passphrase, *path;\n    btc_net_t net;\n"
      "    const char *xpub_static;\n    qrv_density_t specter[3], ur[3];\n} qrv_xpub_t;\n")
    for i, x in enumerate(xpubs):
        for k, (dname, parts) in enumerate(x["specter"]):
            w("static const char *const QRV_XS%d_%d[] = { %s };\n" % (i, k, ", ".join(c_str(p) for p in parts)))
        for k, (dname, cbor, parts) in enumerate(x["ur"]):
            w("static const char *const QRV_XU%d_%d[] = { %s };\n" % (i, k, ", ".join(c_str(p) for p in parts)))
    w("static const qrv_xpub_t QR_XPUBS[] = {\n")
    for i, x in enumerate(xpubs):
        sp = ", ".join("{ %s, NULL, QRV_XS%d_%d, %d }" % (d, i, k, len(p)) for k, (d, p) in enumerate(x["specter"]))
        ur = ", ".join("{ %s, %s, QRV_XU%d_%d, %d }" % (d, c_str(c), i, k, len(p)) for k, (d, c, p) in enumerate(x["ur"]))
        w("    { %s, %s, %s, %s, %s, { %s }, { %s } },\n" % (
            c_str(x["mnemonic"]), c_str(x["passphrase"]), c_str(x["path"]), NETS[x["net"]],
            c_str(x["static"]), sp, ur))
    w("};\n\n")

    w("typedef struct { const char *psbt_hex; qr_density_t density; const char *const *parts; unsigned n_parts; } qrv_psbt_out_t;\n")
    for i, (h, d, parts) in enumerate(enc_psbts):
        w("static const char *const QRV_PO%d[] = { %s };\n" % (i, ", ".join(c_str(p) for p in parts)))
    w("static const qrv_psbt_out_t QR_PSBT_OUT[] = {\n")
    for i, (h, d, parts) in enumerate(enc_psbts):
        w("    { %s, %s, QRV_PO%d, %d },\n" % (c_str(h), d, i, len(parts)))
    w("};\n")


if __name__ == "__main__":
    main()
