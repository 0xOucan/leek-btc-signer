"""
SeedSigner's own Views, run through its FlowTest harness, for the Tools flows
its test suite does not cover: a new seed from dice rolls, and calculating
the final word (from coin flips, a chosen word, or zeros), for 12 and 24
words. Each case records what the Views computed. Run by
gen_tools_vectors.py, which writes sim/vectors/seedsigner_tools.h.

Output file: $LBS_TOOLS_CAPTURE (JSON lines).
"""
import json
import os
import random

from base import FlowStep, FlowTest

from embit import bip39
from seedsigner.models.seed import Seed
from seedsigner.views import seed_views, tools_views
from seedsigner.views.view import MainMenuView

WORDS = bip39.WORDLIST


def _write(record):
    with open(os.environ["LBS_TOOLS_CAPTURE"], "a") as f:
        f.write(json.dumps(record, sort_keys=True) + "\n")


def _rolls(rng, n):
    return "".join(rng.choice("123456") for _ in range(n))


class TestCaptureToolsFlows(FlowTest):

    def test_dice(self):
        rng = random.Random(85)
        cases = [("1" * 50), ("6" * 99), ("123456" * 9)[:50], ("654321" * 17)[:99]]
        cases += [_rolls(rng, 50) for _ in range(4)] + [_rolls(rng, 99) for _ in range(4)]
        for rolls in cases:
            self.run_sequence([
                FlowStep(MainMenuView, button_data_selection=MainMenuView.TOOLS),
                FlowStep(tools_views.ToolsMenuView, button_data_selection=tools_views.ToolsMenuView.DICE),
                FlowStep(tools_views.ToolsDiceEntropyMnemonicLengthView, screen_return_value=0 if len(rolls) == 50 else 1),
                FlowStep(tools_views.ToolsDiceEntropyEntryView, screen_return_value=rolls),
                FlowStep(seed_views.SeedWordsWarningView),
            ])
            seed = self.controller.storage.pending_seed
            _write({"kind": "dice", "rolls": rolls, "mnemonic": seed.mnemonic_str,
                    "fingerprint": seed.get_fingerprint()})
            self.controller.storage.clear_pending_seed()

    def test_final_word(self):
        rng = random.Random(24)
        for n in (12, 24):
            for mode in ("coins", "word", "zeros"):
                for k in range(3):
                    words = [rng.choice(WORDS) for _ in range(n - 1)]
                    flips = "".join(rng.choice("01") for _ in range(7 if n == 12 else 3))
                    chosen = rng.choice(WORDS)
                    got = {}

                    def capture_show(view):
                        got["selected_final_word"] = view.selected_final_word
                        got["selected_final_bits"] = view.selected_final_bits
                        got["checksum_bits"] = view.checksum_bits
                        got["actual_final_word"] = view.actual_final_word

                    seq = [
                        FlowStep(MainMenuView, button_data_selection=MainMenuView.TOOLS),
                        FlowStep(tools_views.ToolsMenuView, button_data_selection=tools_views.ToolsMenuView.KEYBOARD),
                        FlowStep(tools_views.ToolsCalcFinalWordNumWordsView, screen_return_value=0 if n == 12 else 1),
                    ]
                    seq += [FlowStep(seed_views.SeedMnemonicEntryView, screen_return_value=w) for w in words]
                    prompt = tools_views.ToolsCalcFinalWordFinalizePromptView
                    if mode == "coins":
                        seq += [FlowStep(prompt, button_data_selection=prompt.COIN_FLIPS),
                                FlowStep(tools_views.ToolsCalcFinalWordCoinFlipsView, screen_return_value=flips)]
                    elif mode == "word":
                        seq += [FlowStep(prompt, button_data_selection=prompt.SELECT_WORD),
                                FlowStep(seed_views.SeedMnemonicEntryView, screen_return_value=chosen)]
                    else:
                        seq += [FlowStep(prompt, button_data_selection=prompt.ZEROS)]
                    seq += [
                        FlowStep(tools_views.ToolsCalcFinalWordShowFinalWordView, before_run=capture_show,
                                 button_data_selection=tools_views.ToolsCalcFinalWordShowFinalWordView.NEXT),
                        FlowStep(tools_views.ToolsCalcFinalWordDoneView,
                                 button_data_selection=tools_views.ToolsCalcFinalWordDoneView.LOAD),
                        FlowStep(seed_views.SeedFinalizeView),
                    ]
                    self.run_sequence(seq)
                    seed = self.controller.storage.pending_seed
                    got["mnemonic"] = seed.mnemonic_str
                    got["fingerprint"] = seed.get_fingerprint()
                    _write({"kind": "final_word", "n": n, "mode": mode, "words": " ".join(words),
                            "flips": flips if mode == "coins" else None,
                            "chosen": chosen if mode == "word" else None, **got})
                    self.controller.storage.clear_pending_seed()


class TestCaptureAddressExplorer(FlowTest):
    """The addresses ToolsAddressExplorerAddressListView lists, page by page,
    for every single-sig script type, receive and change, on each network."""

    def test_address_explorer(self):
        from seedsigner.gui.screens.screen import ButtonOption
        from seedsigner.models.settings_definition import SettingsConstants, SettingsDefinition
        mnemonics = ["abandon " * 11 + "about",
                     "blush twice taste dawn feed second opinion lazy thumb play neglect impact"]
        script_types = [SettingsConstants.NATIVE_SEGWIT, SettingsConstants.NESTED_SEGWIT,
                        SettingsConstants.LEGACY_P2PKH, SettingsConstants.TAPROOT]
        networks = [SettingsConstants.MAINNET, SettingsConstants.TESTNET, SettingsConstants.REGTEST]
        entry = SettingsDefinition.get_settings_entry(SettingsConstants.SETTING__SCRIPT_TYPES)
        for mnemonic in mnemonics:
            for network in networks:
                for script_type in script_types:
                    for is_change in (False, True):
                        self.reset_controller()
                        from seedsigner.controller import Controller
                        self.controller = Controller.get_instance()
                        self.settings = self.controller.settings
                        self.settings.set_value(SettingsConstants.SETTING__NETWORK, network)
                        self.settings.set_value(SettingsConstants.SETTING__SCRIPT_TYPES, script_types)
                        seed = Seed(mnemonic.split())
                        self.controller.storage.set_pending_seed(seed)
                        self.controller.storage.finalize_pending_seed()
                        addr_type = tools_views.ToolsAddressExplorerAddressTypeView
                        self.run_sequence([
                            FlowStep(MainMenuView, button_data_selection=MainMenuView.TOOLS),
                            FlowStep(tools_views.ToolsMenuView, button_data_selection=tools_views.ToolsMenuView.ADDRESS_EXPLORER),
                            FlowStep(tools_views.ToolsAddressExplorerSelectSourceView, screen_return_value=0),
                            FlowStep(seed_views.SeedExportXpubScriptTypeView, button_data_selection=ButtonOption(
                                entry.get_selection_option_display_name_by_value(script_type), return_data=script_type)),
                            FlowStep(addr_type, button_data_selection=addr_type.CHANGE if is_change else addr_type.RECEIVE),
                            FlowStep(tools_views.ToolsAddressExplorerAddressListView, screen_return_value=10),  # NEXT page
                            FlowStep(tools_views.ToolsAddressExplorerAddressListView, screen_return_value=4),
                            FlowStep(tools_views.ToolsAddressExplorerAddressView),
                        ])
                        data = self.controller.address_explorer_data
                        key = "change_addrs" if is_change else "receive_addrs"
                        _write({"kind": "explorer", "mnemonic": mnemonic, "network": network,
                                "script_type": script_type, "is_change": is_change,
                                "derivation_path": data["derivation_path"],
                                "fingerprint": seed.get_fingerprint(network),
                                "addresses": data[key]})
