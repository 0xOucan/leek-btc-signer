# SeedSigner coverage

How much of SeedSigner a user can do on this device. The list is every
View class in SeedSigner's `src/seedsigner/views/` (SeedSigner's View classes at b2199c68b475eab0035b863b156f149757a1b4a6, extracted by scripts/coverage.py).

A view counts as done only when the device does what SeedSigner's view
does and a test proves it. Every view weighs the same. `scripts/coverage.py`
writes this page and the README chart.

**47 of 109 views: 43.1%.**

## System: 4 of 12

| View | Done | Evidence |
|---|---|---|
| `BackStackView` | ✅ | the screen stack: sim/test_ui.c (ten levels, BACK and HOLD) |
| `MainMenuView` | ✅ | sim/test_ui.c; qemu-flow home-menus |
| `PowerOptionsView` |  |  |
| `RestartView` |  |  |
| `PowerOffView` |  |  |
| `NotYetImplementedView` | ✅ | placeholders naming the View not ported yet: sim/test_ui.c, test_seed_flows.c |
| `ErrorView` | ✅ | status screens: sim/test_screens.c; Wrong QR Type in qemu-flow |
| `NetworkMismatchErrorView` |  |  |
| `UnhandledExceptionView` |  |  |
| `CameraConnectionErrorView` |  |  |
| `OptionDisabledView` |  |  |
| `RemoveMicroSDWarningView` |  |  |

## Scan: 3 of 6

| View | Done | Evidence |
|---|---|---|
| `ScanView` | ✅ | sim/test_scan.c (every recorded scan); qemu-flow scan-*, seed-lifecycle |
| `ScanPSBTView` |  |  |
| `ScanSeedQRView` | ✅ | sim/test_scan.c; qemu-flow scan-seedqr-wrong-type |
| `ScanWalletDescriptorView` |  |  |
| `ScanAddressView` |  |  |
| `ScanInvalidQRTypeView` | ✅ | sim/test_scan.c; qemu-flow scan-invalid |

## Seeds: 19 of 48

| View | Done | Evidence |
|---|---|---|
| `SeedsMenuView` | ✅ | test_seed_flows.c (redirect with no seeds, list with one); qemu-flow seed-lifecycle |
| `SeedSelectSeedView` |  |  |
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
| `SeedExportXpubCustomDerivationView` |  |  |
| `SeedExportXpubQRFormatView` | ✅ | test_export_xpub_standard_flow: test_seed_flows.c; qemu-flow seed-lifecycle |
| `SeedExportXpubWarningView` | ✅ | test_export_xpub_standard_flow: test_seed_flows.c; qemu-flow seed-lifecycle |
| `SeedExportXpubDetailsView` | ✅ | BIP-84 zpub on the firmware: qemu-flow seed-lifecycle; test_seed_flows.c |
| `SeedExportXpubQRDisplayView` | ✅ | parts identical to SeedSigner's (test_qr_encode.c); qemu-flow seed-lifecycle |
| `SeedWordsWarningView` | ✅ | test_seed_flows.c |
| `SeedWordsView` | ✅ | pages of four words, BACK pages back: test_seed_flows.c |
| `SeedBIP85SelectNumWordsView` |  |  |
| `SeedBIP85SelectChildIndexView` |  |  |
| `SeedBIP85InvalidChildIndexView` |  |  |
| `SeedWordsBackupTestPromptView` |  |  |
| `SeedWordsBackupTestView` |  |  |
| `SeedWordsBackupTestMistakeView` |  |  |
| `SeedWordsBackupTestSuccessView` |  |  |
| `SeedTranscribeSeedQRFormatView` |  |  |
| `SeedTranscribeSeedQRWarningView` |  |  |
| `SeedTranscribeSeedQRWholeQRView` |  |  |
| `SeedTranscribeSeedQRZoomedInView` |  |  |
| `SeedTranscribeSeedQRConfirmQRPromptView` |  |  |
| `SeedTranscribeSeedQRConfirmScanView` |  |  |
| `SeedTranscribeSeedQRConfirmWrongSeedView` |  |  |
| `SeedTranscribeSeedQRConfirmInvalidQRView` |  |  |
| `SeedTranscribeSeedQRConfirmSuccessView` |  |  |
| `AddressVerificationStartView` |  |  |
| `AddressVerificationSigTypeView` |  |  |
| `SeedAddressVerificationView` |  |  |
| `SeedAddressVerificationSuccessView` |  |  |
| `LoadMultisigWalletDescriptorView` |  |  |
| `MultisigWalletDescriptorView` |  |  |
| `SeedSignMessageStartView` |  |  |
| `SeedSignMessageConfirmMessageView` |  |  |
| `SeedSignMessageConfirmAddressView` |  |  |
| `SeedSignMessageSignedMessageQRView` |  |  |

## PSBT signing: 18 of 19

| View | Done | Evidence |
|---|---|---|
| `PSBTSelectSeedView` | ✅ | every flow test, "(?)" hint, autoroute: test_psbt_flows.c; qemu-flow psbt-sign |
| `PSBTOverviewView` | ✅ | test_psbt_flows.c (the sweep checks spend, change, fee per PSBT); qemu-flow psbt-sign |
| `PSBTUnsupportedScriptTypeWarningView` | ✅ | an unrecognised input script: test_psbt_flows.c (SeedSigner's own check never fires) |
| `PSBTNoChangeWarningView` | ✅ | the sweep, 65 PSBTs: test_psbt_flows.c |
| `PSBTHighFeeWarningView` | ✅ | the sweep, 21 PSBTs: test_psbt_flows.c |
| `PSBTMathView` | ✅ | the sweep; BACK skips the warnings: test_psbt_flows.c; qemu-flow psbt-sign |
| `PSBTAddressDetailsView` | ✅ | every recipient's address and amount, the sweep: test_psbt_flows.c; qemu-flow psbt-sign |
| `PSBTChangeDetailsView` | ✅ | every change output and path, the sweep; multisig skips verification until descriptors (T47): test_psbt_flows.c |
| `PSBTSeedCannotSignView` | ✅ | test_wrong_seed_routes_back_to_seed_selection_flow: test_psbt_flows.c |
| `PSBTOutputOwnershipClaimFailedView` | ✅ | test_forged_output_claim_terminates_signing_flow: test_psbt_flows.c |
| `PSBTInputOwnershipClaimFailedView` | ✅ | test_forged_input_claim_terminates_signing_flow: test_psbt_flows.c |
| `PSBTSurplusDerivationPathsView` | ✅ | test_surplus_derivation_paths_terminate_signing_flow: test_psbt_flows.c |
| `PSBTMixedDerivationPathTypesView` | ✅ | test_mixed_derivation_path_types_terminate_signing_flow: test_psbt_flows.c |
| `PSBTOutputOwnershipContradictionView` | ✅ | test_output_ownership_contradiction_terminates_signing_flow: test_psbt_flows.c |
| `PSBTAddressVerificationFailedView` |  |  |
| `PSBTOpReturnView` | ✅ | test_parse_and_display_op_return_content: test_psbt_flows.c |
| `PSBTFinalizeView` | ✅ | signs to SeedSigner's exact PSBT, 155 PSBTs: test_psbt_flows.c; qemu-flow psbt-sign |
| `PSBTSignedQRDisplayView` | ✅ | test_psbt_flows.c; qemu-flow psbt-sign |
| `PSBTSigningErrorView` | ✅ | test_scan_multisig_psbt_seed_already_signed_flow: test_psbt_flows.c |

## Tools: 1 of 15

| View | Done | Evidence |
|---|---|---|
| `ToolsMenuView` | ✅ | SeedSigner's five entries: qemu-flow home-menus |
| `ToolsImageEntropyLivePreviewView` |  |  |
| `ToolsImageEntropyFinalImageView` |  |  |
| `ToolsImageEntropyMnemonicLengthView` |  |  |
| `ToolsDiceEntropyMnemonicLengthView` |  |  |
| `ToolsDiceEntropyEntryView` |  |  |
| `ToolsCalcFinalWordNumWordsView` |  |  |
| `ToolsCalcFinalWordFinalizePromptView` |  |  |
| `ToolsCalcFinalWordCoinFlipsView` |  |  |
| `ToolsCalcFinalWordShowFinalWordView` |  |  |
| `ToolsCalcFinalWordDoneView` |  |  |
| `ToolsAddressExplorerSelectSourceView` |  |  |
| `ToolsAddressExplorerAddressTypeView` |  |  |
| `ToolsAddressExplorerAddressListView` |  |  |
| `ToolsAddressExplorerAddressView` |  |  |

## Settings: 2 of 8

| View | Done | Evidence |
|---|---|---|
| `SettingsMenuView` |  |  |
| `LocaleSelectionView` |  |  |
| `SettingsEntryUpdateSelectionView` |  |  |
| `SettingsSelectionRequiredWarningView` |  |  |
| `SettingsIngestSettingsQRView` |  |  |
| `IOTestView` | ✅ | qemu-flow io-test |
| `DonateView` |  |  |
| `VersionView` | ✅ | qemu-flow home-menus |

## Splash: 0 of 1

| View | Done | Evidence |
|---|---|---|
| `OpeningSplashView` |  |  |
