# SeedSigner coverage

How much of SeedSigner a user can do on this device. The list is every
View class in SeedSigner's `src/seedsigner/views/` (SeedSigner's View classes at b2199c68b475eab0035b863b156f149757a1b4a6, extracted by scripts/coverage.py).

A view counts as done only when the device does what SeedSigner's view
does and a test proves it. Every view weighs the same. `scripts/coverage.py`
writes this page and the README chart.

**103 of 109 views: 94.5%.**

## System: 10 of 12

| View | Done | Evidence |
|---|---|---|
| `BackStackView` | ✅ | the screen stack: sim/test_ui.c (ten levels, BACK and HOLD) |
| `MainMenuView` | ✅ | sim/test_ui.c; qemu-flow home-menus |
| `PowerOptionsView` | ✅ | the main menu's power button is BACK: test_settings_flows.c; qemu-flow power |
| `RestartView` | ✅ | wipes every seed, then the platform restarts: test_settings_flows.c |
| `PowerOffView` | ✅ | test_settings_flows.c; qemu-flow power |
| `NotYetImplementedView` | ✅ | placeholders naming the View not ported yet: sim/test_ui.c, test_seed_flows.c |
| `ErrorView` | ✅ | status screens: sim/test_screens.c; Wrong QR Type in qemu-flow |
| `NetworkMismatchErrorView` | ✅ | test_sign_message_network_mismatch_flow, three networks: test_message_flows.c |
| `UnhandledExceptionView` | ✅ | where SeedSigner raises: a SettingsQR, a PSBT, a multisig message path: test_settings_flows.c, test_psbt_flows.c, test_message_flows.c |
| `CameraConnectionErrorView` |  |  |
| `OptionDisabledView` | ✅ | test_sign_message_option_disabled: test_message_flows.c |
| `RemoveMicroSDWarningView` |  |  |

## Scan: 6 of 6

| View | Done | Evidence |
|---|---|---|
| `ScanView` | ✅ | sim/test_scan.c (every recorded scan); qemu-flow scan-*, seed-lifecycle |
| `ScanPSBTView` | ✅ | a seed's Scan transaction, wrong type refused: test_psbt_flows.c |
| `ScanSeedQRView` | ✅ | sim/test_scan.c; qemu-flow scan-seedqr-wrong-type |
| `ScanWalletDescriptorView` | ✅ | 123 descriptor scans as SeedSigner's DecodeQR (test_descriptor.c); refuses other codes: test_multisig_flows.c; qemu-flow multisig-explorer |
| `ScanAddressView` | ✅ | refuses other codes: test_tools_flows.c; qemu-flow verify-address |
| `ScanInvalidQRTypeView` | ✅ | sim/test_scan.c; qemu-flow scan-invalid |

## Seeds: 47 of 48

| View | Done | Evidence |
|---|---|---|
| `SeedsMenuView` | ✅ | test_seed_flows.c (redirect with no seeds, list with one); qemu-flow seed-lifecycle |
| `SeedSelectSeedView` | ✅ | loaded seeds, a seed loaded mid-flow: test_tools_flows.c; qemu-flow verify-address |
| `LoadSeedView` | ✅ | SeedSigner's menu with Electrum off (its default): test_seed_flows.c; qemu-flow |
| `SeedMnemonicEntryView` | ✅ | test_mnemonic_entry_flow, 12 and 24 words typed key by key: test_seed_flows.c |
| `SeedMnemonicInvalidView` | ✅ | test_invalid_mnemonic: test_seed_flows.c |
| `SeedFinalizeView` | ✅ | test_scan_seedqr_flow: test_seed_flows.c; qemu-flow seed-lifecycle |
| `SeedAddPassphraseView` | ✅ | test_passphrase_entry_flow, typed on the wheel: test_seed_flows.c |
| `SeedAddPassphraseExitDialogView` | ✅ | test_passphrase_entry_flow (discard, edit, skip): test_seed_flows.c |
| `SeedReviewPassphraseView` | ✅ | test_passphrase_entry_flow: test_seed_flows.c |
| `SeedDiscardView` | ✅ | test_discard_seed_flow: test_seed_flows.c; qemu-flow seed-lifecycle |
| `SeedElectrumMnemonicStartView` |  |  |
| `SeedOptionsView` | ✅ | test_seed_flows.c; qemu-flow seed-lifecycle |
| `SeedBackupView` | ✅ | test_seed_flows.c |
| `SeedExportXpubSigTypeView` | ✅ | test_export_xpub_standard_flow, all 21 combinations: test_seed_flows.c |
| `SeedExportXpubScriptTypeView` | ✅ | test_export_xpub_standard_flow: test_seed_flows.c; qemu-flow seed-lifecycle |
| `SeedExportXpubCustomDerivationView` | ✅ | test_export_xpub_custom_derivation_flow; the explorer's custom path: test_seed_flows.c, test_tools_flows.c |
| `SeedExportXpubQRFormatView` | ✅ | test_export_xpub_standard_flow: test_seed_flows.c; qemu-flow seed-lifecycle |
| `SeedExportXpubWarningView` | ✅ | test_export_xpub_standard_flow: test_seed_flows.c; qemu-flow seed-lifecycle |
| `SeedExportXpubDetailsView` | ✅ | BIP-84 zpub on the firmware: qemu-flow seed-lifecycle; test_seed_flows.c |
| `SeedExportXpubQRDisplayView` | ✅ | parts identical to SeedSigner's (test_qr_encode.c); qemu-flow seed-lifecycle |
| `SeedWordsWarningView` | ✅ | test_seed_flows.c |
| `SeedWordsView` | ✅ | pages of four words, BACK pages back, on to the backup test: test_seed_flows.c |
| `SeedBIP85SelectNumWordsView` | ✅ | test_seed_flows.c |
| `SeedBIP85SelectChildIndexView` | ✅ | 18 children word for word, on five profiles: test_seed_flows.c |
| `SeedBIP85InvalidChildIndexView` | ✅ | 2^31 and an empty index: test_seed_flows.c |
| `SeedWordsBackupTestPromptView` | ✅ | verify and skip, loaded and pending seeds: test_seed_flows.c |
| `SeedWordsBackupTestView` | ✅ | every word asked once, decoys, secure RNG: test_seed_flows.c |
| `SeedWordsBackupTestMistakeView` | ✅ | try again, review words: test_seed_flows.c |
| `SeedWordsBackupTestSuccessView` | ✅ | test_seed_flows.c |
| `SeedTranscribeSeedQRFormatView` | ✅ | standard and compact, 12 and 24 words: test_seed_flows.c; qemu-flow seedqr-transcribe |
| `SeedTranscribeSeedQRWarningView` | ✅ | test_seed_flows.c; qemu-flow seedqr-transcribe |
| `SeedTranscribeSeedQRWholeQRView` | ✅ | the panel reads back as the SeedQR, every format, five profiles: test_seed_flows.c; qemu-flow seedqr-transcribe |
| `SeedTranscribeSeedQRZoomedInView` | ✅ | every zone, A-1 to F-6: test_seed_flows.c; qemu-flow seedqr-transcribe |
| `SeedTranscribeSeedQRConfirmQRPromptView` | ✅ | test_transcribe_seedqr_and_verify: test_seed_flows.c; qemu-flow seedqr-transcribe |
| `SeedTranscribeSeedQRConfirmScanView` | ✅ | test_transcribe_seedqr_and_verify: test_seed_flows.c |
| `SeedTranscribeSeedQRConfirmWrongSeedView` | ✅ | test_transcribe_seedqr_and_verify: test_seed_flows.c |
| `SeedTranscribeSeedQRConfirmInvalidQRView` | ✅ | unreadable and non-SeedQR codes: test_seed_flows.c |
| `SeedTranscribeSeedQRConfirmSuccessView` | ✅ | test_transcribe_seedqr_and_verify: test_seed_flows.c |
| `AddressVerificationStartView` | ✅ | single sig, nested, P2WSH routes: test_tools_flows.c; qemu-flow verify-address |
| `AddressVerificationSigTypeView` | ✅ | nested segwit, BACK, Multisig: test_tools_flows.c, test_multisig_flows.c |
| `SeedAddressVerificationView` | ✅ | 144 addresses at SeedSigner's index and branch, Skip 10, Cancel: test_tools_flows.c; qemu-flow verify-address; multisig, test__verify_address__legacy_multisig_p2sh__flow and P2WSH: test_multisig_flows.c |
| `SeedAddressVerificationSuccessView` | ✅ | test_tools_flows.c; qemu-flow verify-address |
| `LoadMultisigWalletDescriptorView` | ✅ | from address verification and PSBT change; Cancel to home or back to the change: test_multisig_flows.c |
| `MultisigWalletDescriptorView` | ✅ | policy and fingerprints as embit's for 17 descriptors (test_descriptor.c); each resume_main_flow's button, OK, BACK: test_multisig_flows.c; qemu-flow multisig-explorer |
| `SeedSignMessageStartView` | ✅ | setting, path and network checks: test_message_flows.c; qemu-flow sign-message |
| `SeedSignMessageConfirmMessageView` | ✅ | paged, BACK pages back: test_message_flows.c; qemu-flow sign-message |
| `SeedSignMessageConfirmAddressView` | ✅ | 192 addresses as SeedSigner's: test_message_flows.c; qemu-flow sign-message |
| `SeedSignMessageSignedMessageQRView` | ✅ | 192 signatures byte for byte (test_message.c): test_message_flows.c; qemu-flow sign-message |

## PSBT signing: 19 of 19

| View | Done | Evidence |
|---|---|---|
| `PSBTSelectSeedView` | ✅ | every flow test, "(?)" hint, autoroute: test_psbt_flows.c; qemu-flow psbt-sign |
| `PSBTOverviewView` | ✅ | test_psbt_flows.c (the sweep checks spend, change, fee per PSBT); qemu-flow psbt-sign |
| `PSBTUnsupportedScriptTypeWarningView` | ✅ | an unrecognised input script: test_psbt_flows.c (SeedSigner's own check never fires) |
| `PSBTNoChangeWarningView` | ✅ | the sweep, 65 PSBTs: test_psbt_flows.c |
| `PSBTHighFeeWarningView` | ✅ | the sweep, 21 PSBTs: test_psbt_flows.c |
| `PSBTMathView` | ✅ | the sweep; BACK skips the warnings: test_psbt_flows.c; qemu-flow psbt-sign |
| `PSBTAddressDetailsView` | ✅ | every recipient's address and amount, the sweep: test_psbt_flows.c; qemu-flow psbt-sign |
| `PSBTChangeDetailsView` | ✅ | every change output and path, the sweep: test_psbt_flows.c; multisig change verified against a scanned descriptor, every verify_multisig_output() SeedSigner's tests make: test_multisig_flows.c |
| `PSBTSeedCannotSignView` | ✅ | test_wrong_seed_routes_back_to_seed_selection_flow: test_psbt_flows.c |
| `PSBTOutputOwnershipClaimFailedView` | ✅ | test_forged_output_claim_terminates_signing_flow: test_psbt_flows.c |
| `PSBTInputOwnershipClaimFailedView` | ✅ | test_forged_input_claim_terminates_signing_flow: test_psbt_flows.c |
| `PSBTSurplusDerivationPathsView` | ✅ | test_surplus_derivation_paths_terminate_signing_flow: test_psbt_flows.c |
| `PSBTMixedDerivationPathTypesView` | ✅ | test_mixed_derivation_path_types_terminate_signing_flow: test_psbt_flows.c |
| `PSBTOutputOwnershipContradictionView` | ✅ | test_output_ownership_contradiction_terminates_signing_flow: test_psbt_flows.c |
| `PSBTAddressVerificationFailedView` | ✅ | a descriptor that does not own the change: test_multisig_flows.c |
| `PSBTOpReturnView` | ✅ | test_parse_and_display_op_return_content: test_psbt_flows.c |
| `PSBTFinalizeView` | ✅ | signs to SeedSigner's exact PSBT, 155 PSBTs: test_psbt_flows.c; qemu-flow psbt-sign |
| `PSBTSignedQRDisplayView` | ✅ | test_psbt_flows.c; qemu-flow psbt-sign |
| `PSBTSigningErrorView` | ✅ | test_scan_multisig_psbt_seed_already_signed_flow: test_psbt_flows.c |

## Tools: 12 of 15

| View | Done | Evidence |
|---|---|---|
| `ToolsMenuView` | ✅ | SeedSigner's five entries: qemu-flow home-menus |
| `ToolsImageEntropyLivePreviewView` |  |  |
| `ToolsImageEntropyFinalImageView` |  |  |
| `ToolsImageEntropyMnemonicLengthView` |  |  |
| `ToolsDiceEntropyMnemonicLengthView` | ✅ | test_tools_flows.c; qemu-flow dice-seed |
| `ToolsDiceEntropyEntryView` | ✅ | 12 roll sequences give SeedSigner's mnemonics; DEL, BACK wipes: test_tools_flows.c; qemu-flow dice-seed |
| `ToolsCalcFinalWordNumWordsView` | ✅ | test_tools_flows.c |
| `ToolsCalcFinalWordFinalizePromptView` | ✅ | all three entropy sources, BACK: test_tools_flows.c |
| `ToolsCalcFinalWordCoinFlipsView` | ✅ | test_tools_flows.c |
| `ToolsCalcFinalWordShowFinalWordView` | ✅ | 18 cases: SeedSigner's bits, checksum and final word: test_tools_flows.c |
| `ToolsCalcFinalWordDoneView` | ✅ | load and discard: test_tools_flows.c |
| `ToolsAddressExplorerSelectSourceView` | ✅ | test__address_explorer__flow, loadseed sideflow, wrong QR type: test_tools_flows.c; qemu-flow address-explorer; test__address_explorer__legacy_multisig_p2sh__flow: test_multisig_flows.c |
| `ToolsAddressExplorerAddressTypeView` | ✅ | all four back_button scenarios: test_tools_flows.c; qemu-flow address-explorer |
| `ToolsAddressExplorerAddressListView` | ✅ | 48 lists, 960 addresses as SeedSigner's, pages and BACK: test_tools_flows.c; qemu-flow address-explorer |
| `ToolsAddressExplorerAddressView` | ✅ | returns to the list at the address: test_tools_flows.c; qemu-flow address-explorer |

## Settings: 8 of 8

| View | Done | Evidence |
|---|---|---|
| `SettingsMenuView` | ✅ | each level lists SeedSigner's settings in order: test_settings_flows.c; qemu-flow settings-network |
| `LocaleSelectionView` | ✅ | English, the one language: test_settings_flows.c |
| `SettingsEntryUpdateSelectionView` | ✅ | every option of every setting, 54 in all: test_settings_flows.c; qemu-flow settings-network |
| `SettingsSelectionRequiredWarningView` | ✅ | test_multiselect: test_settings_flows.c |
| `SettingsIngestSettingsQRView` | ✅ | 135 SettingsQR parses as SeedSigner's (test_settings.c); test_settingsqr: test_settings_flows.c; qemu-flow settingsqr |
| `IOTestView` | ✅ | qemu-flow io-test; test_io_test: test_settings_flows.c |
| `DonateView` | ✅ | test_donate: test_settings_flows.c |
| `VersionView` | ✅ | qemu-flow home-menus |

## Splash: 1 of 1

| View | Done | Evidence |
|---|---|---|
| `OpeningSplashView` | ✅ | at boot, leaves by itself or on a key: test_settings_flows.c; qemu-flow home-menus |
