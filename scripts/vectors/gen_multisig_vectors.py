#!/usr/bin/env python3
"""
Generate sim/vectors/seedsigner_multisig.h: SeedSigner's multisig handling,
from its own code.

  * Wallet descriptor scans: the frames, and the descriptor SeedSigner's
    DecodeQR returns. Those its tests decode (test_decodepsbtqr.py), plus
    every descriptor below in each text format SeedSigner reads: a generic
    descriptor, Specter's wallet JSON (whole and animated "pXofY"), a
    multisig setup file, and that file as UR bytes.
  * For each descriptor, what ScanView makes of it: the "/0/*" -> "/{0,1}/*"
    rewrite, whether it is a basic multisig, get_multisig_policy(), the key
    fingerprints, and get_multisig_address() for receive and change.
  * PSBTParser.verify_multisig_output() as SeedSigner's tests call it.
  * test_flows_tools.py's multisig flows: what the explorer lists and the
    address verification finds for their descriptor.

    python gen_multisig_vectors.py --seedsigner PATH > sim/vectors/seedsigner_multisig.h

Runs pytest with multisig_capture.py loaded; same environment as
gen_qr_vectors.py (see README.md).
"""
import argparse
import contextlib
import json
import os
import re
import subprocess
import sys
import tempfile


def c_str(s):
    if s is None:
        return "NULL"
    out = []
    for ch in s:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ord(ch) < 0x20 or ord(ch) > 0x7e:
            out.extend("\\%03o" % b for b in ch.encode())
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


def scanview_rewrite(descriptor_str):
    """ScanView's rewrite of `/0/*` to `/{0,1}/*`, as scan_views.py does it."""
    orig = descriptor_str
    try:
        if len(re.findall(r'\[([0-9,a-f,A-F]+?)(\/[0-9,\/,h\']+?)\].*?(\/0\/\*)', descriptor_str)) > 0:
            p = re.compile(r'(\[[0-9,a-f,A-F]+?\/[0-9,\/,h\']+?\].*?)(\/0\/\*)')
            descriptor_str = p.sub(r'\1/{0,1}/*', descriptor_str)
        elif len(re.findall(r'(\[[0-9,a-f,A-F]+?\/[0-9,\/,h,\']+?\][a-z,A-Z,0-9]*?)([\,,\)])', descriptor_str)) > 0:
            p = re.compile(r'(\[[0-9,a-f,A-F]+?\/[0-9,\/,h,\']+?\][a-z,A-Z,0-9]*?)([\,,\)])')
            descriptor_str = p.sub(r'\1/{0,1}/*\2', descriptor_str)
    except Exception:
        descriptor_str = orig
    return descriptor_str


def setup_file(descriptor):
    """A multisig setup file (Coldcard's format) for a sortedmulti descriptor."""
    m = re.match(r'^(sh\(wsh|wsh|sh)\(sortedmulti\((\d+),(.*)\)\)\)?(#\w+)?$', descriptor)
    if not m:
        return None
    fmt = {"sh(wsh": "P2SH-P2WSH", "wsh": "P2WSH", "sh": "P2SH"}[m.group(1)]
    keys = re.findall(r'\[([0-9a-fA-F]{8})((?:/[0-9]+[h\']?)+)\]([0-9A-Za-z]+)', m.group(3))
    if not keys or len({k[1] for k in keys}) != 1:
        return None
    lines = ["# Exported for a test", "Name: Test Wallet", "Policy: %s of %d" % (m.group(2), len(keys)),
             "Derivation: m%s" % keys[0][1].replace("h", "'"), "Format: %s" % fmt, ""]
    lines += ["%s: %s" % (fp.upper(), xpub) for fp, _, xpub in keys]
    return "\n".join(lines)


def edge_cases(native, nested):
    """Frames at the corners of SeedSigner's wallet decoders: its JSON
    reading, the pXofY parsing, the setup file parser's quirks, and the
    ScanView-ending raises."""
    plain = native.split("#")[0]
    keys = re.findall(r'\[([0-9a-f]{8})/([0-9h/]+)\]([0-9A-Za-z]+)', plain)
    path = "m/" + keys[0][1].replace("h", "'")
    key_lines = ["%s: %s" % (fp.upper(), xpub) for fp, _, xpub in keys]
    other = nested.split("#")[0]

    def js(**kw):
        return json.dumps(dict(label="Edge", **kw))

    def setup(*head, keys_first=False, keys=None):
        lines = ["# Coldcard Multisig setup file (created by a test)", "#"]
        body = list(head)
        k = key_lines if keys is None else keys
        return "\n".join(lines + (k + body if keys_first else body + [""] + k))

    std = ("Name: Edge", "Policy: 2 of 3", "Derivation: " + path, "Format: P2WSH")
    whole = js(descriptor=plain)
    out = [
        # JSON: the last of two keys, an escaped key, an escaped value.
        ['{"label": "x", "descriptor": "%s", "descriptor": "%s"}' % (other, plain)],
        ['p1of1 {"label": "x", "descr\\u0069ptor": "%s"}' % plain],
        ['p1of1 {"label": "x", "descriptor": "%s"}' % plain.replace("/", "\\/")],
        # JSON that holds "descriptor" but is not a dict; and a number.
        ['p1of1 ["descriptor", "sortedmulti"]'],
        ['p1of1 "a descriptor"'],
        ['p1of1 ["nothing"]'],
        ['p1of1 5'],
        ['p1of1 null'],
        ['p1of1 {"label": "x", "descriptor": 5}'],
        ['p1of1 {"label": "x"}'],
        # JSON's corners: NaN and friends, numbers, escapes, nesting.
        ['p1of1 {"a": NaN, "b": -Infinity, "c": [1.5e3, -0, 0.25E-2, true, false, null, {}, []], '
         '"d": "\\ud83d\\ude00 \\ud83d \\n\\t\\"", "descriptor": "%s"}' % plain],
        ['p1of1 {"a": 01, "descriptor": "%s"}' % plain],
        ['p1of1 {"a": 1., "descriptor": "%s"}' % plain],
        ['p1of1 {"a": .5, "descriptor": "%s"}' % plain],
        ['p1of1 {"a": 1e, "descriptor": "%s"}' % plain],
        ['p1of1 {"a": -NaN, "descriptor": "%s"}' % plain],
        ['p1of1 {"a": "\\x41", "descriptor": "%s"}' % plain],
        ['p1of1 {"a": "\\u12g4", "descriptor": "%s"}' % plain],
        ['p1of1 {"a": "tab\there", "descriptor": "%s"}' % plain],
        ['p1of1 {"descriptor": "%s",}' % plain],
        ['p1of1 {"descriptor": "%s"} x' % plain],
        ['p1of1 {"descriptor": "%s"} \n ' % plain],
        ['p1of1 {"a": %s, "descriptor": "%s"}' % ("[" * 40 + "]" * 40, plain)],
        # pXofY: case, a trailing newline, a line break inside, whitespace.
        ['P1OF1 ' + whole],
        ['p1of2 ' + whole[:40] + "\n", 'p2of2 ' + whole[40:]],
        ['p1of1 {"label": "x",\n"descriptor": "%s"}' % plain],
        ['  \n' + whole],
        ['p1of2 ' + whole[:40], 'p1of2 ' + whole[:40], 'p2of2 ' + whole[40:]],
        ['p1of2 ' + whole[:40], 'p1of3 ' + whole[40:]],
        ['p0of2 ' + whole[40:], 'p1of2 ' + whole[:40]],
        # Generic: a checksum embit does not check, and trailing junk.
        [plain + "#abcdefgh"],
        [plain + " x"],
        # Setup files.
        [setup(*std)],
        [setup("Name: Edge", "Policy: 23", "Derivation: " + path, "Format: P2WSH")],
        [setup("Name: Edge", "POLICY: 2 of 3", "derivation:" + path.replace("'", "h"), "FORMAT: p2sh-p2wsh")],
        [setup("  Name: Edge", "  Policy: 2 of 3  ", "  Derivation: " + path, "  Format: P2WSH-P2SH\r")],
        [setup("Name: Edge", "Derivation: " + path, "Format: P2WSH", "Policy: 2 of 3", keys_first=True)],
        [setup("Name: Edge", "Policy: 2 of 3", "Derivation: " + path, "Format: P2SH")],
        [setup("Name: Edge", "Policy: 2 of 3", "Derivation: m" + path, "Format: P2WSH")],
        [setup(*std, keys=key_lines[:2])],
        [setup(*std, keys=key_lines + ["  #abcdefg: x"])],
        [setup("Name: Edge", "Policy: 2 of 10", "Derivation: " + path, "Format: P2WSH")],
        [setup("Name: Edge", "Policy: two of three", "Derivation: " + path, "Format: P2WSH")],
        [setup("Name: Edge", "Policy: 2 of 3", "Derivation: ", "Format: P2WSH")],
        [setup("Name: Edge", "Policy: 2 of 3", "Derivation: m", "Format: P2WSH")],
        [setup("Name: Edge", "Policy: 2 of 3", "Derivation: " + path, "Format: P2WSH", "Format: bogus")],
    ]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seedsigner", required=True)
    args = ap.parse_args()
    ss = os.path.abspath(args.seedsigner)
    here = os.path.dirname(os.path.abspath(__file__))

    with tempfile.TemporaryDirectory() as tmp:
        cap = os.path.join(tmp, "capture.jsonl")
        env = dict(os.environ, LBS_MULTISIG_CAPTURE=cap,
                   PYTHONPATH=os.pathsep.join([here, os.path.join(ss, "src")]))
        subprocess.run([sys.executable, "-m", "pytest", "-q", "-p", "multisig_capture", "-p", "no:cacheprovider",
                        "tests/test_decodepsbtqr.py", "tests/test_embit_utils.py", "tests/test_psbt_parser.py",
                        "tests/test_flows_tools.py"],
                       cwd=ss, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        records = [json.loads(l) for l in open(cap)]

    sys.path.insert(0, os.path.join(ss, "src"))
    sys.path.insert(0, os.path.join(ss, "tests"))
    with contextlib.redirect_stdout(sys.stderr):
        from embit.descriptor import Descriptor
        from urtypes import Bytes
        from seedsigner.helpers import embit_utils
        from seedsigner.helpers.ur2.ur import UR
        from seedsigner.helpers.ur2.ur_encoder import UREncoder
        from seedsigner.models.decode_qr import DecodeQR
        from psbt_testing_util import PSBTTestData

    # The descriptors: SeedSigner's own, from its tests.
    descriptors = []
    for r in records:
        if r["kind"] in ("decode", "address", "policy", "verify") and r.get("descriptor"):
            descriptors.append(r["descriptor"])
    descriptors += [PSBTTestData.MULTISIG_NATIVE_SEGWIT_DESCRIPTOR, PSBTTestData.MULTISIG_NESTED_SEGWIT_DESCRIPTOR,
                    PSBTTestData.MULTISIG_LEGACY_P2SH_DESCRIPTOR]
    seen, uniq = set(), []
    for d in descriptors:
        if d not in seen:
            seen.add(d)
            uniq.append(d)
    descriptors = uniq

    def decode(frames):
        """DecodeQR on the frames, as ScanView feeds it: a raise ends the scan."""
        d = DecodeQR()
        status = None
        for f in frames:
            try:
                status = d.add_data(f).name
            except Exception:
                status = "RAISES"
                break
        desc = None
        err = None
        try:
            if d.is_complete and d.is_wallet_descriptor:
                desc = d.get_wallet_descriptor()
                if not isinstance(desc, str):
                    # ScanView's Descriptor.from_string() raises on anything else.
                    desc = None
        except Exception as e:
            err = type(e).__name__
        return {"kind": "decode", "frames": [{"text": f} if isinstance(f, str) else {"hex": f.hex()} for f in frames],
                "qr_type": str(d.qr_type), "complete": bool(d.is_complete), "descriptor": desc, "error": err,
                "status": status}

    # Generated scans in each text format.
    scans = [r for r in records if r["kind"] == "decode"]
    for d in descriptors:
        plain = d.split("#")[0]
        scans.append(decode([d]))
        js = json.dumps({"label": "Test Wallet", "blockheight": 0, "descriptor": d})
        scans.append(decode([js]))
        third = (len(js) + 2) // 3
        parts = [js[i * third:(i + 1) * third] for i in range(3)]
        scans.append(decode(["p%dof3 %s" % (i + 1, p) for i, p in enumerate(parts)]))
        scans.append(decode(["p%dof3 %s" % (i + 1, p) for i, p in reversed(list(enumerate(parts)))]))
        f = setup_file(plain)
        if f:
            scans.append(decode([f]))
            ur = UR("bytes", Bytes(f.encode()).to_cbor())
            enc = UREncoder(ur, 4000)
            scans.append(decode([enc.next_part().upper()]))

    # What ScanView makes of each descriptor scanned.
    infos = []
    descs = sorted({s["descriptor"] for s in scans if s.get("descriptor")})
    scans += [decode(f) for f in edge_cases(PSBTTestData.MULTISIG_NATIVE_SEGWIT_DESCRIPTOR,
                                           PSBTTestData.MULTISIG_NESTED_SEGWIT_DESCRIPTOR)]
    for d in descs:
        info = {"descriptor": d, "rewritten": scanview_rewrite(d)}
        try:
            desc = Descriptor.from_string(info["rewritten"])
            info["basic"] = bool(desc.is_basic_multisig)
            if desc.is_basic_multisig:
                info["policy"] = list(embit_utils.get_multisig_policy(desc))
                info["fingerprints"] = [k.fingerprint.hex() for k in desc.keys]
                nets = ["main"] if "xpub" in d or "Zpub" in d or "Ypub" in d else ["test", "regtest"]
                info["addresses"] = []
                for net in nets:
                    for change in (False, True):
                        for i in range(6):
                            a = embit_utils.get_multisig_address(desc, index=i, is_change=change, embit_network=net)
                            info["addresses"].append((net, change, i, a))
        except Exception as e:
            info["error"] = type(e).__name__
        infos.append(info)

    # The multisig flows in test_flows_tools.py: their descriptor and
    # address, and what the explorer lists (on the default network, mainnet)
    # and SeedAddressVerificationView finds (on regtest) for them.
    from seedsigner.models.settings_definition import SettingsConstants
    flows_src = open(os.path.join(ss, "tests", "test_flows_tools.py")).read()
    flow_desc = re.search(r'p2sh_descriptor = "([^"]+)"', flows_src).group(1).replace("<0;1>", "{0,1}")
    flow_addr = re.search(r'add_data\("(2N\w+)"\)', flows_src).group(1)
    fd = Descriptor.from_string(scanview_rewrite(flow_desc))
    main_net = SettingsConstants.map_network_to_embit(SettingsConstants.MAINNET)
    reg_net = SettingsConstants.map_network_to_embit(SettingsConstants.REGTEST)
    flow = {"descriptor": flow_desc, "address": flow_addr,
            "receive": [embit_utils.get_multisig_address(fd, index=i, is_change=False, embit_network=main_net)
                        for i in range(20)],
            "change": [embit_utils.get_multisig_address(fd, index=i, is_change=True, embit_network=main_net)
                       for i in range(20)]}
    for i in range(1000):
        if embit_utils.get_multisig_address(fd, index=i, is_change=False, embit_network=reg_net) == flow_addr:
            flow["verified"] = (i, False)
            break
        if embit_utils.get_multisig_address(fd, index=i, is_change=True, embit_network=reg_net) == flow_addr:
            flow["verified"] = (i, True)
            break

    verifies = []
    vseen = set()
    for r in records:
        if r["kind"] != "verify":
            continue
        k = (r["psbt"], r["mnemonic"], r["passphrase"], r["network"], r["descriptor"], r["change_num"])
        if k not in vseen:
            vseen.add(k)
            verifies.append(r)

    commit = subprocess.run(["git", "-C", ss, "rev-parse", "HEAD"], capture_output=True,
                            text=True).stdout.strip() or "unknown"
    nets_c = {"main": "BTC_NET_MAIN", "test": "BTC_NET_TEST", "regtest": "BTC_NET_REGTEST",
              "M": "BTC_NET_MAIN", "T": "BTC_NET_TEST", "R": "BTC_NET_REGTEST"}
    out = []
    w = out.append
    w("/*")
    w(" * GENERATED by scripts/vectors/gen_multisig_vectors.py - do not edit.")
    w(" * SeedSigner's multisig handling at commit %s: %d descriptor scans," % (commit, len(scans)))
    w(" * %d descriptors, %d change verifications." % (len(infos), len(verifies)))
    w(" * SeedSigner is MIT licensed, Copyright (c) 2021 SeedSigner.")
    w(" */")
    w("#pragma once")
    w("#include <stdbool.h>")
    w('#include "btc.h"')
    w("")
    w("typedef struct { const char *text; const char *hex; } vec_ms_frame_t;")
    w("typedef struct {")
    w("    const vec_ms_frame_t *frames; unsigned n_frames;")
    w("    const char *qr_type;            /* SeedSigner's QRType */")
    w("    bool complete;")
    w("    const char *descriptor;         /* get_wallet_descriptor(), or NULL */")
    w("    const char *status;             /* the last add_data(), RAISES, or NULL: not recorded */")
    w("} vec_ms_scan_t;")
    w("typedef struct { btc_net_t net; bool change; unsigned index; const char *address; } vec_ms_addr_t;")
    w("typedef struct {")
    w("    const char *descriptor, *rewritten;")
    w("    const char *error;              /* Descriptor.from_string raised, or NULL */")
    w("    bool basic;                     /* is_basic_multisig */")
    w("    const char *threshold, *n;      /* get_multisig_policy() */")
    w("    const char *fingerprints;       /* comma-joined, in the descriptor's order */")
    w("    const vec_ms_addr_t *addresses; unsigned n_addresses;")
    w("} vec_ms_descriptor_t;")
    w("typedef struct {")
    w("    const char *psbt, *mnemonic, *passphrase;")
    w("    btc_net_t net;")
    w("    const char *descriptor;")
    w("    unsigned change_num;")
    w("    bool result;")
    w("} vec_ms_verify_t;")
    w("")
    for i, s in enumerate(scans):
        w("static const vec_ms_frame_t VEC_MS_FRAMES_%d[] = {" % i)
        for f in s["frames"]:
            w("    { %s, %s }," % (c_str(f.get("text")), c_str(f.get("hex"))))
        w("};")
    w("static const vec_ms_scan_t VEC_MS_SCANS[] = {")
    for i, s in enumerate(scans):
        w("    { VEC_MS_FRAMES_%d, %d, %s, %s, %s, %s }," % (
            i, len(s["frames"]), c_str(s["qr_type"].split(".")[-1].lower()),
            "true" if s.get("complete", True) else "false", c_str(s.get("descriptor")), c_str(s.get("status"))))
    w("};")
    for i, d in enumerate(infos):
        if d.get("addresses"):
            w("static const vec_ms_addr_t VEC_MS_ADDRS_%d[] = {" % i)
            for net, change, idx, a in d["addresses"]:
                w("    { %s, %s, %d, %s }," % (nets_c[net], "true" if change else "false", idx, c_str(a)))
            w("};")
    w("static const vec_ms_descriptor_t VEC_MS_DESCRIPTORS[] = {")
    for i, d in enumerate(infos):
        pol = d.get("policy") or [None, None]
        w("    { %s, %s, %s, %s, %s, %s, %s, %s, %d }," % (
            c_str(d["descriptor"]), c_str(d["rewritten"]), c_str(d.get("error")),
            "true" if d.get("basic") else "false", c_str(pol[0]), c_str(pol[1]),
            c_str(",".join(d["fingerprints"]) if d.get("fingerprints") else None),
            ("VEC_MS_ADDRS_%d" % i) if d.get("addresses") else "NULL", len(d.get("addresses") or [])))
    w("};")
    w("/* test_flows_tools.py's multisig flows: the descriptor scanned, the")
    w("   explorer's first 20 receive and change addresses on mainnet, and the")
    w("   address verified on regtest, at that index. */")
    w("static const char VEC_MS_FLOW_DESCRIPTOR[] = %s;" % c_str(flow["descriptor"]))
    for k in ("receive", "change"):
        w("static const char *const VEC_MS_FLOW_%s[] = {" % k.upper())
        for a in flow[k]:
            w("    %s," % c_str(a))
        w("};")
    w("static const char VEC_MS_FLOW_ADDRESS[] = %s;" % c_str(flow["address"]))
    w("static const unsigned VEC_MS_FLOW_VERIFIED_INDEX = %d;" % flow["verified"][0])
    w("static const bool VEC_MS_FLOW_VERIFIED_CHANGE = %s;" % ("true" if flow["verified"][1] else "false"))
    w("static const vec_ms_verify_t VEC_MS_VERIFY[] = {")
    for r in verifies:
        w("    { %s, %s, %s, %s, %s, %d, %s }," % (c_str(r["psbt"]), c_str(r["mnemonic"]), c_str(r["passphrase"]),
                                              nets_c[r["network"]], c_str(r["descriptor"]), r["change_num"],
                                              "true" if r["result"] else "false"))
    w("};")
    print("\n".join(out))


if __name__ == "__main__":
    main()
