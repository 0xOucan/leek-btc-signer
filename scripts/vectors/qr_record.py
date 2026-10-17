"""
What a scan did, as SeedSigner's DecodeQR saw it: shared by qr_capture.py
(SeedSigner's own tests) and gen_qr_vectors.py (generated scans).
"""


def _percent(d, weighted):
    try:
        return d.get_percent_complete(weight_mixed_frames=weighted)
    except Exception:
        return None


def _type(d):
    return d.qr_type if d.qr_type is not None else "none"


class Recorder:
    def __init__(self, name):
        self.name = name
        self.frames = []

    def add(self, d, data, add_data):
        raw = data.encode("utf-8") if isinstance(data, str) else bytes(data)
        frame = {"data": raw.hex()}
        try:
            frame["status"] = int(add_data(d, data))
        except Exception as e:
            frame["status"] = "type_changed" if "Unexpected Type Change" in str(e) else "error"
            frame["error"] = type(e).__name__
            self.frames.append(frame)
            frame["type"] = _type(d)
            frame["pct"] = _percent(d, False)
            frame["pctw"] = _percent(d, True)
            raise
        frame["type"] = _type(d)
        frame["pct"] = _percent(d, False)
        frame["pctw"] = _percent(d, True)
        self.frames.append(frame)
        return frame["status"]

    def result(self, d):
        r = {"name": self.name, "frames": self.frames, "type": _type(d),
             "complete": bool(d.is_complete)}

        def attempt(fn):
            try:
                return fn()
            except Exception as e:
                return "!" + type(e).__name__

        if d.is_psbt:
            data = attempt(d.get_data_psbt)
            r["psbt"] = data if isinstance(data, str) else (data.hex() if data is not None else None)
        if d.is_seed and d.is_complete:
            words = attempt(d.get_seed_phrase)
            r["seed"] = " ".join(words) if isinstance(words, list) else words
        if d.is_address and d.is_complete:
            r["address"] = d.get_address()
            r["address_type"] = list(d.get_address_type())
        if d.is_settings and d.is_complete:
            r["settings"] = d.get_settings_data()
        if d.is_sign_message and d.is_complete:
            q = d.get_qr_data()
            r["msg_path"] = q["derivation_path"]
            r["msg"] = q["message"]
        if d.qr_type in ("output__ur", "account__ur", "bytes__ur") and d.is_complete:
            r["ur_cbor"] = attempt(lambda: bytes(d.decoder.result_message().cbor).hex())
        return r
