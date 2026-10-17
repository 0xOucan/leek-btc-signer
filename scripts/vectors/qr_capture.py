"""
pytest plugin: record every frame SeedSigner's DecodeQR is fed.

Run SeedSigner's own tests/test_decodepsbtqr.py with this loaded (see
gen_qr_vectors.py). Each DecodeQR instance becomes one scan: the frames in
order, what add_data() returned for each (or the exception it raised), and
what the decoder's getters give at the end.

Output file: $LBS_QR_CAPTURE, one JSON object per scan.
"""
import json
import os

from seedsigner.models import decode_qr as dq

from qr_record import Recorder

_original_init = dq.DecodeQR.__init__
_original_add = dq.DecodeQR.add_data
_scans = []


def _init(self, *a, **kw):
    _original_init(self, *a, **kw)
    self._lbs = Recorder(os.environ.get("PYTEST_CURRENT_TEST", "").split(" ")[0])
    _scans.append(self)


def _add(self, data):
    return self._lbs.add(self, data, _original_add)


dq.DecodeQR.__init__ = _init
dq.DecodeQR.add_data = _add


def pytest_sessionfinish(session, exitstatus):
    with open(os.environ["LBS_QR_CAPTURE"], "w") as f:
        for d in _scans:
            if d._lbs.frames:
                f.write(json.dumps(d._lbs.result(d)) + "\n")
