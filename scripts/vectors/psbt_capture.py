"""
pytest plugin: record every PSBT SeedSigner's PSBTParser is asked to parse.

Run SeedSigner's own test_psbt_parser.py with this loaded (see
gen_psbt_vectors.py). For each parse it writes one JSON line: the PSBT as it
was handed in, the seed, the network, and either the exception SeedSigner
raised or everything the parse established. For a parse that succeeds it also
signs a copy exactly as SeedSigner's PSBTFinalizeView does (sign_with, then
trim) and records the result.

Output file: $LBS_PSBT_CAPTURE.
"""
import base64
import copy
import json
import os

from embit import bip32

from seedsigner.models import psbt_parser as pp

_original_init = pp.PSBTParser.__init__


def _paths(verified):
    return [[dp.fingerprint.hex(), list(dp.derivation)] for dp in verified]


def _result(parser):
    policy = parser.policy or {}
    return {
        "policy_type": policy.get("type"),
        "m": policy.get("m"),
        "n": policy.get("n"),
        "has_cosigners": "cosigners" in policy,
        "num_inputs": parser.num_inputs,
        "input_amount": parser.input_amount,
        "spend_amount": parser.spend_amount,
        "change_amount": parser.change_amount,
        "fee_amount": parser.fee_amount,
        "change": [{"output_index": c["output_index"], "address": c["address"],
                    "amount": c["amount"], "path": list(c["verified_derivation_path"])}
                   for c in parser.change_data],
        "destinations": [[a, v] for a, v in zip(parser.destination_addresses, parser.destination_amounts)],
        "op_return": parser.op_return_data.hex() if parser.op_return_data is not None else None,
        "is_high_fee": parser.is_high_fee,
        "verified_inputs": [_paths(v) for v in parser.verified_input_derivation_paths],
        "verified_outputs": [_paths(v) for v in parser.verified_output_derivation_paths],
    }


def _write(record):
    with open(os.environ["LBS_PSBT_CAPTURE"], "a") as f:
        f.write(json.dumps(record, sort_keys=True) + "\n")


def _capturing_init(self, p, seed, network="M"):
    record = None
    if p is not None and seed is not None:
        try:
            raw = p.serialize()
        except Exception:
            raw = None
        if raw is not None:
            record = {
                "test": os.environ.get("PYTEST_CURRENT_TEST", "").split(" ")[0],
                "psbt": base64.b64encode(raw).decode(),
                "mnemonic": seed.mnemonic_str,
                "passphrase": seed.passphrase,
                "network": network,
            }
    try:
        _original_init(self, p, seed, network)
    except Exception as e:
        if record:
            record["error"] = type(e).__name__
            _write(record)
        raise
    if record is None or self.policy is None:
        return
    record["result"] = _result(self)
    try:
        signed = copy.deepcopy(self.psbt)
        record["sigs_before"] = pp.PSBTParser.sig_count(signed)
        signed.sign_with(self.root)
        trimmed = pp.PSBTParser.trim(signed)
        record["sigs_after"] = pp.PSBTParser.sig_count(trimmed)
        record["signed"] = trimmed.to_base64()
    except Exception as e:
        record["sign_error"] = type(e).__name__
    _write(record)


pp.PSBTParser.__init__ = _capturing_init
