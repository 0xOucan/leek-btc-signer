"""
pytest plugin: record every SettingsQR SeedSigner's Settings.parse_settingsqr
is asked to parse while its own tests run, with what it returned or the
exception it raised. Run by gen_settings_vectors.py.

Output file: $LBS_SETTINGS_CAPTURE (JSON lines).
"""
import json
import os

from seedsigner.models import settings as settings_module

_original = settings_module.Settings.parse_settingsqr.__func__


def _write(record):
    with open(os.environ["LBS_SETTINGS_CAPTURE"], "a") as f:
        f.write(json.dumps(record, sort_keys=True) + "\n")


def _capturing(cls, data):
    record = {"data": data}
    try:
        name, updates = _original(cls, data)
    except Exception as e:
        record["error"] = type(e).__name__
        _write(record)
        raise
    record["name"] = name
    record["updates"] = updates
    _write(record)
    return name, updates


settings_module.Settings.parse_settingsqr = classmethod(_capturing)
