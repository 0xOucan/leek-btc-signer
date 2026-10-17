# SeedSigner coverage

How much of SeedSigner a user can do on this device. The list is every
View class in SeedSigner's `src/seedsigner/views/` (SeedSigner's View classes at b2199c68b475eab0035b863b156f149757a1b4a6, extracted by scripts/coverage.py).

A view counts as done only when the device does what SeedSigner's view
does and a test proves it. Every view weighs the same. `scripts/coverage.py`
writes this page and the README chart.

**80 of 109 views: 73.4%.**

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

## Scan: 4 of 6

| View | Done | Evidence |
|---|---|---|
| `ScanView` | ✅ | sim/test_scan.c (every recorded scan); qemu-flow scan-*, seed-lifecycle |
| `ScanPSBTView` |  |  |
| `ScanSeedQRView` | ✅ | sim/test_scan.c; qemu-flow scan-seedqr-wrong-type |
| `ScanWalletDescriptorView` |  |  |
| `ScanAddressView` | ✅ | refuses other codes: test_tools_flows.c; qemu-flow verify-address |
| `ScanInvalidQRTypeView` | ✅ | sim/test_scan.c; qemu-flow scan-invalid |

## Seeds: 40 of 48

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
| `SeedExportXpubCustomDerivationView` |  |  |
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
| `AddressVerificationSigTypeView` | ✅ | nested segwit, BACK: test_tools_flows.c |
| `SeedAddressVerificationView` | ✅ | 144 addresses at SeedSigner's index and branch, Skip 10, Cancel: test_tools_flows.c; qemu-flow verify-address |
| `SeedAddressVerificationSuccessView` | ✅ | test_tools_flows.c; qemu-flow verify-address |
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
| `ToolsAddressExplorerSelectSourceView` | ✅ | test__address_explorer__flow, loadseed sideflow, wrong QR type: test_tools_flows.c; qemu-flow address-explorer |
| `ToolsAddressExplorerAddressTypeView` | ✅ | all four back_button scenarios: test_tools_flows.c; qemu-flow address-explorer |
| `ToolsAddressExplorerAddressListView` | ✅ | 48 lists, 960 addresses as SeedSigner's, pages and BACK: test_tools_flows.c; qemu-flow address-explorer |
| `ToolsAddressExplorerAddressView` | ✅ | returns to the list at the address: test_tools_flows.c; qemu-flow address-explorer |

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
