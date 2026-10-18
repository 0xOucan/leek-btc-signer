"""
pytest plugin: record SeedSigner's multisig handling while its own tests
run. Run by gen_multisig_vectors.py.

  * DecodeQR: the frames each decoder is given, and the wallet descriptor
    get_wallet_descriptor() returns from them.
  * embit_utils.get_multisig_address() and get_multisig_policy().
  * PSBTParser.verify_multisig_output(): the PSBT, seed, network,
    descriptor, change number and result.

Output file: $LBS_MULTISIG_CAPTURE (JSON lines).
"""
import base64
import json
import os

from seedsigner.helpers import embit_utils
from seedsigner.models import decode_qr, psbt_parser


def _write(record):
    with open(os.environ["LBS_MULTISIG_CAPTURE"], "a") as f:
        f.write(json.dumps(record, sort_keys=True) + "\n")


def _frame(data):
    if isinstance(data, (bytes, bytearray)):
        return {"hex": bytes(data).hex()}
    return {"text": data}


_add_data = decode_qr.DecodeQR.add_data


def add_data(self, data):
    frames = self.__dict__.setdefault("_lbs_frames", [])
    frames.append(_frame(data))
    return _add_data(self, data)


_get_wallet_descriptor = decode_qr.DecodeQR.get_wallet_descriptor


def get_wallet_descriptor(self):
    result = _get_wallet_descriptor(self)
    _write({"kind": "decode", "frames": self.__dict__.get("_lbs_frames", []),
            "qr_type": str(self.qr_type), "descriptor": None if result is None else str(result)})
    return result


_get_multisig_address = embit_utils.get_multisig_address


def get_multisig_address(descriptor, index=0, is_change=False, embit_network="main"):
    address = _get_multisig_address(descriptor=descriptor, index=index, is_change=is_change,
                                    embit_network=embit_network)
    _write({"kind": "address", "descriptor": str(descriptor), "index": index, "is_change": is_change,
            "network": embit_network, "address": address})
    return address


_get_multisig_policy = embit_utils.get_multisig_policy


def get_multisig_policy(descriptor):
    policy = _get_multisig_policy(descriptor)
    _write({"kind": "policy", "descriptor": str(descriptor), "policy": list(policy)})
    return policy


_parser_init = psbt_parser.PSBTParser.__init__


def parser_init(self, p, seed, network="M"):
    try:
        self._lbs_psbt = base64.b64encode(p.serialize()).decode() if p is not None else None
    except Exception:
        self._lbs_psbt = None
    self._lbs_seed = (seed.mnemonic_str, seed.passphrase) if seed is not None else None
    self._lbs_network = network
    return _parser_init(self, p, seed, network)


_verify = psbt_parser.PSBTParser.verify_multisig_output


def verify_multisig_output(self, descriptor, change_num):
    result = _verify(self, descriptor, change_num)
    if self._lbs_psbt and self._lbs_seed:
        _write({"kind": "verify", "psbt": self._lbs_psbt, "mnemonic": self._lbs_seed[0],
                "passphrase": self._lbs_seed[1], "network": self._lbs_network,
                "descriptor": str(descriptor), "change_num": change_num, "result": bool(result)})
    return result


decode_qr.DecodeQR.add_data = add_data
decode_qr.DecodeQR.get_wallet_descriptor = get_wallet_descriptor
embit_utils.get_multisig_address = get_multisig_address
embit_utils.get_multisig_policy = get_multisig_policy
psbt_parser.PSBTParser.__init__ = parser_init
psbt_parser.PSBTParser.verify_multisig_output = verify_multisig_output
