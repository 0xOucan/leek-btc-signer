# SeedSigner coverage

How much of SeedSigner a user can do on this device. The list is every
View class in SeedSigner's `src/seedsigner/views/` (SeedSigner's View classes at b2199c68b475eab0035b863b156f149757a1b4a6, extracted by scripts/coverage.py).

A view counts as done only when the device does what SeedSigner's view
does and a test proves it. Every view weighs the same. `scripts/coverage.py`
writes this page and the README chart.

**11 of 109 views: 10.1%.**

## System: 4 of 12

| View | Done | Evidence |
|---|---|---|
| `BackStackView` | ✅ | the screen stack: sim/test_ui.c (ten levels, BACK and HOLD) |
| `MainMenuView` | ✅ | HOME: sim/test_ui.c; qemu-flow home-menus |
| `PowerOptionsView` |  |  |
| `RestartView` |  |  |
| `PowerOffView` |  |  |
| `NotYetImplementedView` | ✅ | the placeholder screens: sim/test_ui.c walks every one |
| `ErrorView` | ✅ | status screens: sim/test_screens.c; Wrong QR Type in qemu-flow |
| `NetworkMismatchErrorView` |  |  |
| `UnhandledExceptionView` |  |  |
| `CameraConnectionErrorView` |  |  |
| `OptionDisabledView` |  |  |
| `RemoveMicroSDWarningView` |  |  |

## Scan: 3 of 6

| View | Done | Evidence |
|---|---|---|
| `ScanView` | ✅ | SCAN: sim/test_scan.c (every recorded scan); qemu-flow scan-* |
| `ScanPSBTView` |  |  |
| `ScanSeedQRView` | ✅ | SCAN_SEED: sim/test_scan.c; qemu-flow scan-seedqr-wrong-type |
| `ScanWalletDescriptorView` |  |  |
| `ScanAddressView` |  |  |
| `ScanInvalidQRTypeView` | ✅ | invalid code: sim/test_scan.c; qemu-flow scan-invalid |

## Seeds: 1 of 48

| View | Done | Evidence |
|---|---|---|
| `SeedsMenuView` |  |  |
| `SeedSelectSeedView` |  |  |
| `LoadSeedView` | ✅ | LOAD_SEED, SeedSigner's menu with Electrum off (its default): qemu-flow home-menus |
| `SeedMnemonicEntryView` |  |  |
| `SeedMnemonicInvalidView` |  |  |
| `SeedFinalizeView` |  |  |
| `SeedAddPassphraseView` |  |  |
| `SeedAddPassphraseExitDialogView` |  |  |
| `SeedReviewPassphraseView` |  |  |
| `SeedDiscardView` |  |  |
| `SeedElectrumMnemonicStartView` |  |  |
| `SeedOptionsView` |  |  |
| `SeedBackupView` |  |  |
| `SeedExportXpubSigTypeView` |  |  |
| `SeedExportXpubScriptTypeView` |  |  |
| `SeedExportXpubCustomDerivationView` |  |  |
| `SeedExportXpubQRFormatView` |  |  |
| `SeedExportXpubWarningView` |  |  |
| `SeedExportXpubDetailsView` |  |  |
| `SeedExportXpubQRDisplayView` |  |  |
| `SeedWordsWarningView` |  |  |
| `SeedWordsView` |  |  |
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

## PSBT signing: 0 of 19

| View | Done | Evidence |
|---|---|---|
| `PSBTSelectSeedView` |  |  |
| `PSBTOverviewView` |  |  |
| `PSBTUnsupportedScriptTypeWarningView` |  |  |
| `PSBTNoChangeWarningView` |  |  |
| `PSBTHighFeeWarningView` |  |  |
| `PSBTMathView` |  |  |
| `PSBTAddressDetailsView` |  |  |
| `PSBTChangeDetailsView` |  |  |
| `PSBTSeedCannotSignView` |  |  |
| `PSBTOutputOwnershipClaimFailedView` |  |  |
| `PSBTInputOwnershipClaimFailedView` |  |  |
| `PSBTSurplusDerivationPathsView` |  |  |
| `PSBTMixedDerivationPathTypesView` |  |  |
| `PSBTOutputOwnershipContradictionView` |  |  |
| `PSBTAddressVerificationFailedView` |  |  |
| `PSBTOpReturnView` |  |  |
| `PSBTFinalizeView` |  |  |
| `PSBTSignedQRDisplayView` |  |  |
| `PSBTSigningErrorView` |  |  |

## Tools: 1 of 15

| View | Done | Evidence |
|---|---|---|
| `ToolsMenuView` | ✅ | TOOLS, SeedSigner's five entries: qemu-flow home-menus |
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
| `IOTestView` | ✅ | IO_TEST: qemu-flow io-test |
| `DonateView` |  |  |
| `VersionView` | ✅ | VERSION: qemu-flow home-menus |

## Splash: 0 of 1

| View | Done | Evidence |
|---|---|---|
| `OpeningSplashView` |  |  |
