#!/usr/bin/env python3
"""
Generate sim/vectors/seedsigner_electrum.h: Electrum seeds as SeedSigner
handles them, by running its own code (see README.md).

  * ElectrumSeed: which 12-word mnemonics it takes (the ones SeedSigner's
    tests use, more drawn at random until enough pass its version check,
    and as many that fail it), the seed bytes and fingerprint, and the
    custom extension as normalize_electrum_passphrase() leaves it.
  * What SeedSigner exports for one: the static and Specter xpub QRs
    (detect_version: zpub or Zpub) and the UR crypto-account CBOR, at
    derivation_override()'s m/0h and m/1h, on every network.
  * The addresses the explorer lists and address verification searches:
    get_single_sig_address() below the xpub at m/0h, every script type
    (verification keeps the scanned address's type, at m/0h).
  * sign_message() with an Electrum seed's bytes.

    python gen_electrum_vectors.py --seedsigner PATH > sim/vectors/seedsigner_electrum.h
"""
import argparse
import os
import random
import subprocess
import sys


def c_str(s):
    if s is None:
        return "NULL"
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


NETS = {"M": "BTC_NET_MAIN", "T": "BTC_NET_TEST", "R": "BTC_NET_REGTEST"}
SCRIPTS = {"nat": "BTC_SCRIPT_P2WPKH", "nes": "BTC_SCRIPT_P2SH_P2WPKH", "leg": "BTC_SCRIPT_P2PKH",
           "tr": "BTC_SCRIPT_P2TR"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seedsigner", required=True)
    args = ap.parse_args()
    ss = os.path.abspath(args.seedsigner)
    sys.path.insert(0, os.path.join(ss, "src"))

    from embit import bip39
    from seedsigner.helpers import embit_utils
    from seedsigner.models.encode_qr import (SpecterLegacyXPubQrEncoder, StaticXpubQrEncoder,
                                             UrXpubQrEncoder)
    from seedsigner.models.seed import ElectrumSeed, InvalidSeedException
    from seedsigner.models.settings_definition import SettingsConstants as SC

    nets = {"M": SC.MAINNET, "T": SC.TESTNET, "R": SC.REGTEST}

    # The mnemonics SeedSigner's tests give ElectrumSeed (tests/test_seed.py,
    # test_flows_seed.py, test_flows_tools.py, test_flows_psbt.py).
    from_tests = [
        "regular reject rare profit once math fringe chase until ketchup century escape",
        "bomb congress scorpion mutual word stamp tongue valid permit salmon yellow spy",
        "morning pretty hobby click extend color wait joke define sausage boost salmon",
        "basket print toy noodle betray weird filter ticket insect copy force machine",
        "only gain spot output unknown craft simple cram absorb suggest ridge famous",
        "apple drip silly junior language resource unaware whale snake copy gravity tank",
        "pioneer divide volcano art victory family grow novel mandate bicycle senior adjust",
        "gentle combine cool hamster ghost harvest gossip lend dismiss slam any toast",
        "enough board blossom stamp fire buffalo digital solution sadness random number stone",
    ]
    rng = random.Random(0xe1ec)
    valid_rand, invalid_rand = [], []
    while len(valid_rand) < 16 or len(invalid_rand) < 16:
        m = " ".join(rng.choice(bip39.WORDLIST) for _ in range(12))
        try:
            ElectrumSeed(m.split())
            if len(valid_rand) < 16:
                valid_rand.append(m)
        except InvalidSeedException:
            if len(invalid_rand) < 16:
                invalid_rand.append(m)

    extensions = ["", "test", "monkey fling orange coin good", "  Monkey   FLING orange  coin good ",
                  "ABC", " ", "   ", "a  b", "x" * 100, "~!@#$%^&*()_+{}|:\"<>?`-=[]\\;',./ Z"]

    seeds = []
    for i, m in enumerate(from_tests + valid_rand + invalid_rand):
        exts = extensions if i < len(from_tests) + 4 else ["", "test"]
        for ext in exts:
            try:
                s = ElectrumSeed(m.split(), passphrase=ext)
            except InvalidSeedException as e:
                seeds.append(dict(mnemonic=m, ext=ext, error=str(e)))
                break       # the passphrase plays no part in the check
            seeds.append(dict(mnemonic=m, ext=ext, error=None, normalized=s.passphrase,
                              seed=s.seed_bytes.hex(), fp=s.get_fingerprint()))

    # Wrong lengths: SeedSigner raises before looking at the words.
    lengths = []
    for n in (11, 13, 24, 1):
        try:
            ElectrumSeed(["regular"] * n)
            lengths.append((n, None))
        except InvalidSeedException as e:
            lengths.append((n, str(e)))

    valid = [m for m in from_tests + valid_rand
             if any(r["mnemonic"] == m and r["error"] is None for r in seeds)]
    xpubs = []
    for m in valid[:8]:
        for ext in ("", "test"):
            seed = ElectrumSeed(m.split(), passphrase=ext)
            for sig in (SC.SINGLE_SIG, SC.MULTISIG):
                path = seed.derivation_override(sig)
                for net in "MTR":
                    static = StaticXpubQrEncoder(seed=seed, derivation=path, network=nets[net],
                                                 sig_type=sig).next_part()
                    e = SpecterLegacyXPubQrEncoder(seed=seed, derivation=path, network=nets[net],
                                                   sig_type=sig, qr_density=SC.DENSITY__MEDIUM)
                    specter = [e.next_part() for _ in range(e.seq_len())]
                    u = UrXpubQrEncoder(seed=seed, derivation=path, network=nets[net], sig_type=sig,
                                        qr_density=SC.DENSITY__MEDIUM)
                    xpubs.append(dict(mnemonic=m, ext=ext, sig=sig, path=path, net=net, static=static,
                                      specter=specter, cbor=bytes(u.ur2_encode.ur.cbor).hex()))

    addresses = []
    for m in valid[:6]:
        for ext in ("", "test"):
            seed = ElectrumSeed(m.split(), passphrase=ext)
            path = seed.derivation_override(SC.SINGLE_SIG)
            for net in "MTR":
                xpub = seed.get_xpub(wallet_path=path, network=nets[net])
                enet = embit_utils.get_embit_network_name(nets[net])
                for script in ("nat", "nes", "leg", "tr"):
                    count = 12 if script == "nat" else 3
                    for change in (False, True):
                        for idx in range(count):
                            addresses.append(dict(
                                mnemonic=m, ext=ext, net=net, script=script, change=change, index=idx,
                                address=embit_utils.get_single_sig_address(
                                    xpub=xpub, script_type=script, index=idx, is_change=change,
                                    embit_network=enet)))

    signs = []
    for m in valid[:4]:
        seed = ElectrumSeed(m.split())
        for path in ("m/84h/0h/0h/0/0", "m/84h/1h/0h/1/3", "m/0h/0/0"):
            for msg in ("hello world", "", "Electrum"):
                signs.append(dict(mnemonic=m, path=path, message=msg,
                                  signature=embit_utils.sign_message(seed_bytes=seed.seed_bytes,
                                                                     derivation=path, msg=msg.encode())))

    commit = subprocess.run(["git", "-C", ss, "rev-parse", "HEAD"], capture_output=True,
                            text=True).stdout.strip() or "unknown"
    out = []
    w = out.append
    w("/*")
    w(" * GENERATED by scripts/vectors/gen_electrum_vectors.py - do not edit.")
    w(" * Electrum seeds by SeedSigner's own code at commit %s:" % commit)
    w(" * %d ElectrumSeed constructions, %d wrong lengths, %d xpub exports," % (len(seeds), len(lengths),
                                                                            len(xpubs)))
    w(" * %d addresses, %d message signatures." % (len(addresses), len(signs)))
    w(" * SeedSigner is MIT licensed, Copyright (c) 2021 SeedSigner.")
    w(" */")
    w("#pragma once")
    w("#include <stdbool.h>")
    w('#include "btc.h"')
    w("")
    w("typedef struct {")
    w("    const char *mnemonic, *extension;")
    w("    const char *error;          /* InvalidSeedException's message, or NULL */")
    w("    const char *normalized;     /* the passphrase as ElectrumSeed keeps it */")
    w("    const char *seed_hex, *fingerprint;")
    w("} vec_electrum_seed_t;")
    w("typedef struct { unsigned words; const char *error; } vec_electrum_length_t;")
    w("typedef struct {")
    w("    const char *mnemonic, *extension;")
    w("    btc_sig_t sig;")
    w("    const char *path;")
    w("    btc_net_t net;")
    w("    const char *xpub_static;")
    w("    const char *const *specter; unsigned n_specter;  /* medium density */")
    w("    const char *cbor_hex;       /* UrXpubQrEncoder's crypto-account */")
    w("} vec_electrum_xpub_t;")
    w("typedef struct {")
    w("    const char *mnemonic, *extension;")
    w("    btc_net_t net;")
    w("    btc_script_t script;")
    w("    bool change;")
    w("    unsigned index;")
    w("    const char *address;")
    w("} vec_electrum_address_t;")
    w("typedef struct { const char *mnemonic, *path, *message, *signature; } vec_electrum_sign_t;")
    w("")
    w("static const vec_electrum_seed_t VEC_ELECTRUM_SEEDS[] = {")
    for r in seeds:
        if r["error"]:
            w("    { %s, %s, %s, NULL, NULL, NULL }," % (c_str(r["mnemonic"]), c_str(r["ext"]),
                                                     c_str(r["error"])))
        else:
            w("    { %s, %s, NULL, %s, %s, %s }," % (c_str(r["mnemonic"]), c_str(r["ext"]),
                                                   c_str(r["normalized"]), c_str(r["seed"]),
                                                   c_str(r["fp"])))
    w("};")
    w("static const vec_electrum_length_t VEC_ELECTRUM_LENGTHS[] = {")
    for n, e in lengths:
        w("    { %d, %s }," % (n, c_str(e)))
    w("};")
    for i, x in enumerate(xpubs):
        w("static const char *const VEC_ELECTRUM_SPECTER_%d[] = { %s };" % (
            i, ", ".join(c_str(p) for p in x["specter"])))
    w("static const vec_electrum_xpub_t VEC_ELECTRUM_XPUBS[] = {")
    for i, x in enumerate(xpubs):
        w("    { %s, %s, %s, %s, %s, %s, VEC_ELECTRUM_SPECTER_%d, %d, %s }," % (
            c_str(x["mnemonic"]), c_str(x["ext"]),
            "BTC_SIG_SINGLE" if x["sig"] == SC.SINGLE_SIG else "BTC_SIG_MULTI", c_str(x["path"]),
            NETS[x["net"]], c_str(x["static"]), i, len(x["specter"]), c_str(x["cbor"])))
    w("};")
    w("static const vec_electrum_address_t VEC_ELECTRUM_ADDRESSES[] = {")
    for a in addresses:
        w("    { %s, %s, %s, %s, %s, %d, %s }," % (
            c_str(a["mnemonic"]), c_str(a["ext"]), NETS[a["net"]], SCRIPTS[a["script"]],
            "true" if a["change"] else "false", a["index"], c_str(a["address"])))
    w("};")
    w("static const vec_electrum_sign_t VEC_ELECTRUM_SIGNS[] = {")
    for s in signs:
        w("    { %s, %s, %s, %s }," % (c_str(s["mnemonic"]), c_str(s["path"]), c_str(s["message"]),
                                      c_str(s["signature"])))
    w("};")
    print("\n".join(out))


if __name__ == "__main__":
    main()
