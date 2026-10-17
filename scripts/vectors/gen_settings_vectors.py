#!/usr/bin/env python3
"""
Generate sim/vectors/seedsigner_settings.h: SeedSigner's settings definition
(every SettingsEntry, as its to_dict() gives it) and SettingsQR parses
(Settings.parse_settingsqr), from SeedSigner's own code.

The parses are those SeedSigner's tests make (test_settings.py,
test_settingsqr_decoder.py, test_flows_settings.py, captured by
settings_capture.py) plus generated ones: every option of every setting,
multiselect combinations, and malformed inputs.

    python gen_settings_vectors.py --seedsigner PATH > sim/vectors/seedsigner_settings.h

Needs the same environment as gen_psbt_vectors.py (see README.md).
"""
import argparse
import json
import os
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
            for b in ch.encode():
                out.append("\\%03o" % b)
        else:
            out.append(ch)
    return '"' + "".join(out) + '"'


def value_str(v):
    """A setting's value as one string: lists comma-joined, ints in decimal."""
    if isinstance(v, list):
        return ",".join(str(x) for x in v)
    return str(v)


def generated_cases(entries):
    cases = []
    for e in entries:
        abbr = e["abbreviated_name"]
        opts = [o["value"] for o in (e["selection_options"] or [])]
        for o in opts:
            cases.append("settings::v1 %s=%s" % (abbr, o))
        if e["type"] == "multiselect" and len(opts) > 1:
            cases.append("settings::v1 %s=%s" % (abbr, ",".join(str(o) for o in opts)))
            cases.append("settings::v1 %s=%s" % (abbr, ",".join(str(o) for o in reversed(opts[:2]))))
        cases.append("settings::v1 %s=not_an_option" % abbr)
        cases.append("settings::v1 %s=" % abbr)
    cases += [
        "settings::v1",
        "settings::v1 name=Just_a_name",
        "settings::v1 name=My_Config network=T denom=sats",
        "settings::v2 network=M",
        "settings network=M",
        "network=M",
        "settings::v1 favorite_food=bacon network=R",
        "settings::v1\nnetwork=T\nqr_density=H",
        "settings::v1 network=T network=R",
        "settings::v1 qr_brightness=200",
        "settings::v1 camera=90 camera=270",
        "settings::v1 scripts=nat,nat",
        "settings::v1 sigs=ss,",
        "settings::v1 persistent=E",
    ]
    return cases


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seedsigner", required=True)
    args = ap.parse_args()
    ss = os.path.abspath(args.seedsigner)
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, os.path.join(ss, "src"))

    with tempfile.TemporaryDirectory() as tmp:
        cap = os.path.join(tmp, "capture.jsonl")
        env = dict(os.environ, LBS_SETTINGS_CAPTURE=cap,
                   PYTHONPATH=os.pathsep.join([here, os.path.join(ss, "src")]))
        subprocess.run([sys.executable, "-m", "pytest", "-q", "-p", "settings_capture",
                        "-p", "no:cacheprovider", "tests/test_settings.py",
                        "tests/test_settingsqr_decoder.py", "tests/test_flows_settings.py"],
                       cwd=ss, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        records = [json.loads(l) for l in open(cap)] if os.path.exists(cap) else []

    # SeedSigner prints as it loads; keep stdout for the header.
    import contextlib
    with contextlib.redirect_stdout(sys.stderr):
        from seedsigner.hardware.microsd import MicroSD
        from seedsigner.models.settings import Settings
        from seedsigner.models.settings_definition import SettingsDefinition

    # The device has no microSD card: everything is recorded as SeedSigner OS
    # has it with the card removed, through SeedSigner's own handler for that
    # (Persistent settings then offers only Disabled).
    Settings.HOSTNAME = Settings.SEEDSIGNER_OS
    with contextlib.redirect_stdout(sys.stderr):
        Settings.handle_microsd_state_change(MicroSD.ACTION__REMOVED)
    entries = [e.to_dict() for e in SettingsDefinition.settings_entries]

    # Every input SeedSigner's tests parsed, and the generated ones, parsed
    # again in that state.
    unique, seen = [], set()
    for data in [r["data"] for r in records] + generated_cases(entries):
        if data in seen:
            continue
        seen.add(data)
        r = {"data": data}
        try:
            with contextlib.redirect_stdout(sys.stderr):
                name, updates = Settings.parse_settingsqr(data)
            r["name"], r["updates"] = name, updates
        except Exception as e:
            r["error"] = type(e).__name__
        unique.append(r)

    commit = subprocess.run(["git", "-C", ss, "rev-parse", "HEAD"], capture_output=True,
                            text=True).stdout.strip() or "unknown"
    out = []
    w = out.append
    w("/*")
    w(" * GENERATED by scripts/vectors/gen_settings_vectors.py - do not edit.")
    w(" * SeedSigner's settings definition and SettingsQR parses, as SeedSigner OS")
    w(" * has them with no microSD card inserted (the device has none), at commit")
    w(" * %s: %d settings, %d SettingsQR cases." % (commit, len(entries), len(unique)))
    w(" * SeedSigner is MIT licensed, Copyright (c) 2021 SeedSigner.")
    w(" */")
    w("#pragma once")
    w("")
    w("typedef struct { const char *value, *display_name; } vec_setting_option_t;")
    w("typedef struct {")
    w("    const char *category, *attr_name, *abbreviated_name, *display_name;")
    w("    const char *visibility, *type, *help_text;")
    w("    const vec_setting_option_t *options; unsigned n_options;")
    w("    const char *default_value;      /* lists comma-joined */")
    w("} vec_setting_t;")
    w("typedef struct {")
    w("    const char *data;")
    w("    const char *error;              /* SeedSigner's exception class, or NULL */")
    w("    const char *name;               /* the config name, or NULL */")
    w("    const char *updates;            /* \"attr=value\" lines, in SeedSigner's order */")
    w("} vec_settingsqr_t;")
    w("")
    for i, e in enumerate(entries):
        opts = e["selection_options"] or []
        if opts:
            w("static const vec_setting_option_t VEC_SETTING_OPTIONS_%d[] = {" % i)
            for o in opts:
                w("    { %s, %s }," % (c_str(value_str(o["value"])), c_str(o["display_name"])))
            w("};")
    w("static const vec_setting_t VEC_SETTINGS[] = {")
    for i, e in enumerate(entries):
        opts = e["selection_options"] or []
        w("    { %s, %s, %s, %s, %s, %s, %s, %s, %d, %s }," % (
            c_str(e["category"]), c_str(e["attr_name"]), c_str(e["abbreviated_name"]),
            c_str(e["display_name"]), c_str(e["visibility"]), c_str(e["type"]), c_str(e["help_text"]),
            ("VEC_SETTING_OPTIONS_%d" % i) if opts else "NULL", len(opts),
            c_str(value_str(e["default_value"]) if e["default_value"] is not None else None)))
    w("};")
    w("static const vec_settingsqr_t VEC_SETTINGSQR[] = {")
    for r in unique:
        updates = None
        if "updates" in r:
            updates = "".join("%s=%s\n" % (k, value_str(v)) for k, v in r["updates"].items())
        w("    { %s, %s, %s, %s }," % (c_str(r["data"]), c_str(r.get("error")), c_str(r.get("name")),
                                     c_str(updates)))
    w("};")
    print("\n".join(out))


if __name__ == "__main__":
    main()
