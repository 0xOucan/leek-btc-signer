# Test vectors from SeedSigner

The C suites in `sim/` check this firmware against **SeedSigner's own
behaviour**. The vectors are produced by running SeedSigner's Python code (and
the BIP vectors in its test files) under the embit version SeedSigner pins, and
are written to `sim/vectors/*.h`. The generated headers are committed, so
building and testing never need Python or embit.

To regenerate, after changing a generator or moving to a newer SeedSigner:

```
python3 -m venv ~/.cache/leek-btc-signer/venv
~/.cache/leek-btc-signer/venv/bin/pip install --require-hashes -r scripts/vectors/requirements.txt
~/.cache/leek-btc-signer/venv/bin/python scripts/vectors/gen_seedsigner_vectors.py \
    --seedsigner PATH/TO/seedsigner > sim/vectors/seedsigner_core.h
```

The other generators run the same way:

| Generator | Writes | Also needs |
|---|---|---|
| `gen_psbt_vectors.py` | `sim/vectors/seedsigner_psbt.h`; with `--tests tests/test_flows_psbt.py --array VEC_PSBT_FLOWS`, `sim/vectors/seedsigner_psbt_flows.h` | pytest |
| `gen_tools_vectors.py` | `sim/vectors/seedsigner_tools.h` (SeedSigner's dice and final-word Views, run through its FlowTest harness by `tools_flows_capture.py`) | pytest |
| `gen_settings_vectors.py` | `sim/vectors/seedsigner_settings.h` (SeedSigner's SettingsDefinition and SettingsQR parses, as SeedSigner OS has them with no microSD card; `settings_capture.py` records the parses its tests make) | pytest |
| `gen_message_vectors.py` | `sim/vectors/seedsigner_message.h` (parse_derivation_path, and the address and signature of SeedSigner's message signing) | |
| `gen_multisig_vectors.py` | `sim/vectors/seedsigner_multisig.h` (wallet descriptor scans through DecodeQR, ScanView's rewrite, embit's policy, fingerprints and multisig addresses, `verify_multisig_output()`, and what `test_flows_tools.py`'s multisig flows list and verify; `multisig_capture.py` records the calls SeedSigner's tests make) | pytest, urtypes |
| `gen_qr_vectors.py` | `sim/vectors/seedsigner_qr.h` | pytest, and SeedSigner's full `requirements.txt` (urtypes, pyzbar, qrcode, Pillow), installed with `--require-hashes` |

The output names the SeedSigner commit it was generated from.

SeedSigner is MIT licensed (Copyright (c) 2021 SeedSigner). The generated
vectors carry that notice.
