# Roadmap

Leek BTC Signer is a minimalist, stateless, air-gapped Bitcoin signer for the
ESP32-S3. It follows SeedSigner's logic and flows on a 128x64 OLED with an EC11
rotary encoder.

**Sources:**
- **SeedSigner** ([github.com/SeedSigner/seedsigner](https://github.com/SeedSigner/seedsigner)): behaviour, flows and test vectors.
- **LeekWallet** (same author): the hardware platform.

Every task has an ID, its dependencies, a done-condition, and the cheapest
**tier** that can prove it:

- **H**: host `sim/`
- **Q**: QEMU
- **B**: the board

Tiers are described in [docs/TESTING.md](docs/TESTING.md). A task is done when
its condition passes at that tier, not when the code is written.

---

## Decisions

| Topic | Decision |
|---|---|
| Hardware | ESP32-S3-WROOM-1-N16R8 CAM + OV5640, 1.5" SH1107 128x128 OLED, four-button module, EC11 ([docs/HARDWARE.md](docs/HARDWARE.md)) |
| Display | Build-time profiles in one codebase, with no per-display branch. `oled128` (SH1107, the bench setup) is the default; `oled64` (SSD1306) shares its driver; `tft240` / `tft320` / `tft160` (ST7789 / ST7735S color modules with EC11 + K0) are for later, and `tft240` is the preferred color target |
| Input | UP, DOWN, OK, BACK, and HOLD (BACK held 3 s). The encoder turns UP/DOWN and its push is OK, wired in parallel with the OK button. Every screen works with turn + push + BACK alone. The encoder is polled every 1 ms through the LeekWallet Zcash firmware's tested quadrature decoder rather than the PCNT |
| Language / SDK | C, ESP-IDF 5.5 through PlatformIO `espressif32@6.12.0`, all components vendored, no network at build time |
| Bitcoin | libwally-core + libsecp256k1 (schnorrsig, extrakeys). No pure-C fallback secp: fail the build instead |
| UI | Custom C renderer. Screens describe content; each display profile lays it out. Colors are roles (`fg`, `bg`, `accent`, `ok`, `warning`, `danger`, `muted`), mapped to 1-bit on the OLED and to a SeedSigner-like palette on the TFTs. Menu tree and labels mirror SeedSigner |
| State | Stateless by default. Opt-in vault (LeekWallet's) for a persistent seed, behind a warning |
| Radios | Wi-Fi, Bluetooth and TinyUSB compiled out of every non-dev build |
| Testing | Host first, then QEMU for anything that must run on the real image, then the board |
| License | Apache-2.0. Ported SeedSigner code and vectors keep their MIT notice |
| Commits | [joelparkerhenderson/git-commit-message](https://github.com/joelparkerhenderson/git-commit-message), template in `.gitmessage.txt` |

## Not in scope

- **Networking of any kind.** That includes OTA over radio.
- **USB data while a seed is loaded.** USB is for flashing and dev builds only.
- **Altcoins.** LeekWallet covers EVM; this device is Bitcoin only.
- **Languages that need non-Latin glyphs.** They do not fit at 128x64.

---

## Phase 0: Platform bring-up

| ID | Task | Deps | Done when | Tier |
|---|---|---|---|---|
| T0 | PlatformIO skeleton with these environments: `s3cam` (board), `s3cam-qemu` (SPIRAM=n, UART0 console, harness), `s3cam-dev` (USB console, logs). `sdkconfig.defaults` + per-env overlays. `partitions.csv` 16 MB | — | All three environments build from a clean clone with no network once the platform is installed | Q |
| T1 | Release sdkconfig with radios out: `CONFIG_BT_ENABLED=n`, no Wi-Fi, no TinyUSB. Plus a script that fails if the release image contains radio symbols | T0 | `scripts/check-radios.sh` passes on `s3cam` and fails on a deliberately broken config | Q |
| T2 | `board.h` for the S3CAM with the screen-kit wiring (SH1107 on 47/21, buttons 1/41/14/42, EC11 on 44/43). I2C OLED driver for SH1107 and SSD1306 from LeekWallet and its Zcash firmware | T0 | QEMU boots; a missing panel runs headless and logs it | Q |
| T2a | Display interface: panel driver ops (init, flush rect, brightness), a framebuffer of palette indices (1 bpp `oled`, 4 bpp `tft*`) in internal RAM, role palette per profile. `oled.c` becomes the first panel driver behind it | T2 | `oled` profile draws through the interface; host test fills every role and checks the indices | H |
| T2b | Font set per profile: LeekWallet's 5x7 for `oled`; pre-rasterized anti-aliased fonts (body / title / mono) for `tft*`, Latin only. Source fonts and rasterizer script live in the repo | T2a | Rasterizer reproduces identical output from the sources | H |
| T3 | Carry over `sim/` (Makefile, shims, fake OLED/NVS/input), with the EVM suites removed | T2 | `make -C sim test` is green | H |
| T4 | EC11 polled at 1 ms through the Zcash firmware's quadrature state machine; push wired with OK | T2 | Host: `test_encoder` (bounce, half turns, 100-detent spin). Board: the I/O-test checklist in docs/HARDWARE.md | H, B |
| T5 | Unified input layer: encoder, buttons and K0 → `UP/DOWN/OK/BACK/HOLD`, with debounce, auto-repeat and BACK-hold as a pure state machine | T2, T3 | Host test: each event from either source has the same effect on a menu | H |
| T5a | Host screenshots for every profile: the fake panel writes PBM for `oled` and PPM at the real resolution for `tft*` | T2a, T3 | `make -C sim shots PROFILE=tft240` writes reviewable images | H |
| T5b | Desktop simulator (SDL2): the real UI and flow code in a window at the profile's exact resolution and colors. Arrow keys = encoder, Enter = push, Esc = BACK (hold 3 s for HOLD). QR payloads are fed from files or the command line as if the camera had decoded them. This is how the color profiles are developed before a module is in hand | T5a | `sim/leek-sim --profile tft240` runs every Phase 4 flow by hand; the same build saves screenshots | H |
| T6 | QEMU harness (`LBS_QEMU_HARNESS`): `K`/`Q` input and `F`/`S` frame output on UART0, for whichever profile the image was built for. `scripts/qemu.sh`, `scripts/qemu-flow.py`, and `scripts/qemu-view.py`, which shows the streamed frames live in a window | T2, T5 | A pexpect script boots the image, scrolls a menu, and reads back the expected frame | Q |
| T7 | Release check: harness symbols absent from `s3cam` | T6 | `scripts/check-release.sh` fails if `qemu_harness_` is found in the release ELF | Q |
| T8 | Carry over `entropy.c`, `rand_esp32.c` (health tests, fail closed). In the QEMU env, a marked test RNG | T2 | Host: the health tests reject stuck and biased sources. Board: `entropy_dump_for_analysis` runs | H, B |
| T9 | Carry over the camera: `camera.c`, `viewfinder.c`, quirc, esp32-camera, PSRAM octal config | T2 | Board: the preview shows on the OLED and a static QR decodes | B |
| T10 | Home screen: Scan / Seeds / Tools / Settings on the new input layer, in `oled` and `tft240` | T5, T5a | Host screenshots for both, plus a QEMU flow: navigate all four and back | H, Q |
| T11 | `scripts/check.sh`: host suites, QEMU smoke flow, radio check, release check | T1, T3, T6, T7 | One command, green | H, Q |

**Phase check:** the board boots to the home menu, the encoder and the buttons
both navigate it, the camera preview works, and `check.sh` is green.

## Phase 1: Bitcoin core, no UI

| ID | Task | Deps | Done when | Tier |
|---|---|---|---|---|
| T12 | Vendor libsecp256k1 (ecdsa, schnorrsig, extrakeys) and libwally-core as ESP-IDF components. Build for host and target | T0 | Builds in both; flash cost recorded here | H, Q |
| T13 | Decide what trezor-crypto still does (BIP39 wordlist and entry helpers?) or drop it. One secp256k1 implementation only | T12 | Decision recorded; no duplicate secp linked (checked by symbol) | Q |
| T14 | `btc/seed.c`: mnemonic and passphrase → seed and master fingerprint. Keys live only in internal RAM; `seed_clear()` wipes them | T12 | BIP39 official vectors plus SeedSigner fingerprint vectors | H, Q |
| T15 | `btc/keys.c`: xpub per script type (Native Segwit, Nested Segwit, Legacy, Taproot), single and multisig paths, custom paths; SLIP-132 handling as SeedSigner does it | T14 | Ported `test_embit_utils` vectors | H |
| T16 | `btc/address.c`: receive and change addresses per script type | T15 | Ported vectors | H |
| T17 | `btc/seedqr.c`: standard and compact encode and decode | T14 | Ported `test_seedqr`, including null bytes | H |
| T18 | `btc/dice.c`: dice → mnemonic (50/99 rolls), final word, coin-flip final word | T14 | Ported `test_mnemonic_generation`, including the Coldcard check | H |
| T19 | `btc/psbt_review.c`: parse with libwally, then compute what SeedSigner's `psbt_parser.py` computes: spend, change, fee, OP_RETURN, input ownership, and the forgery and ownership checks | T15, T16 | Ported `test_psbt_parser` (all script types, change, OP_RETURN, high fee, forgery cases) | H |
| T20 | `btc/sign.c`: sign the reviewed PSBT (ECDSA + Taproot key path) | T19 | Ported vectors; signatures verify under libsecp256k1 | H |
| T21 | Run the Bitcoin vectors on the target. The same suites are compiled into the QEMU image and report over UART0 | T14–T20, T6 | Same results on Xtensa as on the host | Q |

**Phase check:** every ported SeedSigner vector passes on the host **and**
under QEMU.

## Phase 2: QR input and output

| ID | Task | Deps | Done when | Tier |
|---|---|---|---|---|
| T22 | Remove LeekWallet's `ur:`-only filter. Raw quirc payload out, including byte mode | T9 | Board: a CompactSeedQR decodes to the right 16 or 32 bytes | B |
| T23 | `qr/detect.c`: port of SeedSigner's `detect_segment_type`. Recognizes: SeedQR standard/compact, mnemonic, 4-letter mnemonic, UR, BBQr, Specter `pXofY`, base64/base43 PSBT, address/BIP21, `settings::v1`, `signmessage` | T21 | Ported `test_decodepsbtqr` type detection | H |
| T24 | Multi-part assembly: UR crypto-psbt / crypto-account / crypto-output / bytes (LeekWallet's fountain code), BBQr (H, 2, Z), Specter. Buffers come from PSRAM with a capped internal fallback | T23 | Ported `test_decodepsbtqr` (large multi-part included) | H, Q |
| T25 | QR encoder: a raw-bytes mode (CompactSeedQR) next to text mode | — | Encode then decode round trip in host tests | H |
| T26 | Output: UR crypto-psbt and crypto-account through `qr-out`, with frame size and density modes tuned for 128x64 | T24, T25 | Ported `test_encodepsbtqr` | H |
| T26a | TFT panel drivers: ST7789 (240x240 with row offset, 320x240) and ST7735S (128x160, tab variant), SPI with DMA strip flush; camera preview straight from PSRAM to the panel | T2a | Board, once a module is in hand: every role color correct, no offset artifacts, preview at a usable frame rate | B |
| T27 | Air-gap round trip with Sparrow (desktop webcam) and one phone wallet. Measure frames needed and read rate | T22, T26, T20 | Signed PSBT accepted by Sparrow. Readability result recorded; if the webcam fails, evaluate the 2.42" SSD1309 | B |

**Phase check:** a full round trip completes on the bench, and readability is
measured.

## Phase 3: UI framework

| ID | Task | Deps | Done when | Tier |
|---|---|---|---|---|
| T28 | Screen engine: a screen table with render, input and exit hook (secrets wiped on exit), plus a back stack. This replaces LeekWallet's 7k-line `ui.c` | T5 | Host test: a back stack of 10 levels returns home; exit hooks fire on every path | H |
| T29 | Components, each with an `oled` and a `tft*` layout: list menu, text pager, address formatter (groups of 4, 2–3 rows), BTC/sats amount, warning and dire-warning screens, "working…" frame | T28 | Host screenshots reviewed | H |
| T30 | Character wheel for passphrase and custom derivation path, driven by the encoder | T28 | Every printable ASCII character is reachable; press budget measured | H |
| T31 | QR display component: static, animated, SeedQR at 2x, and a zoomed transcription view moving zone by zone | T25, T28 | Host screenshots; board scan test | H, B |
| T32 | Viewfinder screen: progress bar, decode-status mark | T9, T24 | Board: a progress bar fills while scanning a UR PSBT | B |
| T33 | Secret placement: every buffer that holds seed, passphrase or key data, **including the framebuffer**, is `MALLOC_CAP_INTERNAL` or static, never PSRAM | T28 | Host and QEMU test: the allocator wrapper refuses SPIRAM for tagged allocations | H, Q |

## Phase 4: First version (single-sig)

Each flow mirrors SeedSigner's `test_flows_*` and is tested as a QEMU flow.

| ID | Task | Deps | Done when | Tier |
|---|---|---|---|---|
| T34 | Seeds → Scan SeedQR / Enter 12 or 24 words (LeekWallet `mnemonic-entry.c`) → fingerprint confirm | T17, T29 | QEMU flow, including the wrong-checksum path | Q |
| T35 | Tools → New seed: dice 50/99, final-word calculator | T18, T29 | QEMU flow | Q |
| T36 | Passphrase: add, review, change, clear | T30 | QEMU flow; secrets wiped on exit | H, Q |
| T37 | Backup: show words (paged, with warning), backup quiz, export SeedQR + transcription | T31 | QEMU flow; board scan of the displayed SeedQR | Q, B |
| T38 | Export xpub: script type → UR crypto-account animated | T15, T26 | QEMU flow, checked against vectors | Q |
| T39 | Scan → PSBT: pick seed by fingerprint → overview → each output → change → fee → sign → UR out | T19, T20, T26, T29 | QEMU flow on the PSBT vectors, including every warning path | Q |
| T40 | Scan → address: verify against single-sig paths | T16 | QEMU flow | Q |
| T41 | Settings in RAM: network, script types, denomination, QR density, privacy warnings. SettingsQR import | T23 | QEMU flow with ported `test_settingsqr_decoder` | H, Q |
| T42 | End-to-end on hardware (`oled`, and `tft240` once a module is in hand): a new seed by dice, xpub into Sparrow, receive (testnet/signet), sign and broadcast from Sparrow | T34–T41 | A broadcast testnet/signet transaction, recorded with date | B |

**Phase check:** T42.

## Phase 5: Optional vault

| ID | Task | Deps | Done when | Tier |
|---|---|---|---|---|
| T43 | Carry over LeekWallet `pin.c`, `vault-kdf.c`, `vault-crypt.c`, `device-wipe.c`, and the attempt counter that survives power cuts | T28 | LeekWallet's vault and PIN suites pass here | H |
| T44 | Settings → Vault: off by default; enabling it shows a dire warning explaining offline attack risk without flash encryption | T43 | QEMU flow | Q |
| T45 | Release builds refuse to enable the vault unless flash encryption is active | T43 | QEMU: the plain image refuses, the encrypted image allows; `qemu-read-flash.sh` finds no mnemonic in NVS | Q |
| T46 | KDF iteration count measured on the board | T43 | Unlock time recorded | B |

## Phase 6: Advanced features

| ID | Task | Deps | Tier |
|---|---|---|---|
| T47 | Multisig: descriptor import (Specter JSON, generic descriptor, UR output, Coldcard config); multisig PSBT review, change check against the descriptor | T39 | H, Q |
| T48 | BIP-85 child seeds | T14 | H, Q |
| T49 | Message signing (`signmessage`, SeedSigner format) | T14 | H, Q |
| T50 | Electrum seeds (off by default) | T14 | H |
| T51 | Address explorer (receive and change lists) | T16 | Q |
| T52 | Camera-noise entropy, mixed in and never substituted | T9, T8 | H, B |
| T53 | BBQr output | T26 | H |
| T54 | Further Latin-script languages | T29 | H |

## Phase 7: Hardening and release

| ID | Task | Deps | Done when | Tier |
|---|---|---|---|---|
| T55 | Secure boot v2 + flash encryption, rehearsed end to end (LeekWallet `qemu-secure.sh` flow) | T45 | Tampered image refuses to boot | Q |
| T56 | JTAG disable and download-mode policy for release | T55 | Documented and verified | Q |
| T57 | Burn procedure on a dedicated test board | T55, T56 | A board boots signed and encrypted; unsigned firmware refuses | B |
| T58 | Entropy statistics on real silicon (dieharder) | T8 | Results recorded | B |
| T59 | Reproducible build: pinned toolchain, `CONFIG_APP_REPRODUCIBLE_BUILD`, two clean builds give identical hashes; signed SHA256SUMS | T11 | `scripts/repro-verify.sh` passes in CI | Q |
| T60 | Secret-lifetime audit (in the LeekWallet AUDIT style) | T42 | `AUDIT.md` written; findings fixed or tracked | H, Q |
| T61 | User guide and assembly guide, with SeedSigner credited | T42 | Someone else can build and use one | — |

---

## Critical path

T0 → T2 → T5 → T6 → T12 → T14 → T15 → T19 → T20 → T21 → T24 → T26 → T28 → T39 → T42

The hardware tasks T4, T9, T22, T26a and T27 run beside it whenever the board is on the
bench. Each one is a leaf that unblocks only its own check.

---

## Progress

Hardware results get a date when they come in. Until then, "done" means proven
on the host and in QEMU.

### 2026-10-16: Phase 0, everything that does not need the board

| Task | State | Evidence |
|---|---|---|
| T0 | Done | `s3cam`, `s3cam-dev`, `s3cam-qemu` build; `qemu.sh --smoke` passes |
| T1 | Done | `check-radios.sh` passes on all three images, and `test-checks.sh` shows it fails on an image with a radio |
| T2 | Done (QEMU), board pending | SH1107/SSD1306 driver builds into the board images; `check-release.sh` confirms it is linked |
| T2a | Done | 4-bit indexed framebuffer, colour roles, five profiles; `test_gfx` on each |
| T2b | Open | The colour profiles use the 5x7 font scaled 2x; long labels are cut on `tft240` |
| T3 | Done | New host harness (`sim/`), ASan + UBSan, every profile |
| T4 | Host done, board pending | `test_encoder` |
| T5 | Done | `test_input`: debounce, BACK on release, HOLD at 3 s, auto-repeat. It caught a repeat firing after release |
| T5a | Done | `make -C sim shots`: 31 screenshots per profile |
| T5b | Built | `make -C sim sim PROFILE=…` runs; not yet driven by hand |
| T6 | Done except QR injection (T23) and a live viewer | `qemu-flow.py`: two flows with frame checks on the real image |
| T7 | Done | `check-release.sh`, proven to fail on a harness image |
| T8 | Open | Entropy carry-over |
| T9 | Open, needs the board | Camera |
| T10 | Done | Home, Load A Seed, Tools, Settings, I/O test, Version; the rest show the task that builds them |
| T11 | Done | `./scripts/check.sh`: ALL CHECKS PASSED |

### 2026-10-16: Phase 1 begins

| Task | State | Evidence |
|---|---|---|
| T12 | Done | libwally-core 1.5.6 + secp256k1-zkp vendored by `scripts/vendor-libwally.sh`, pinned by commit; Elements off; one amalgamation for host and target. Linking `btc_init` alone grows the release image from 226 KB to 388 KB of flash; the full cost is recorded as more of the library is used |
| T13 | Done | trezor-crypto is not carried over at all: libwally's secp256k1-zkp is the only secp256k1 in the image |
| T14 | Done | `seed_t`: 12/24 words, checksum, printable-ASCII passphrase up to 100 characters, fingerprint; `seed_clear` proven to zero the struct |
| T15 | Done | Standard paths, xpubs, SLIP-132 versions (embit's `detect_version`), SeedSigner's `[fingerprint/path]xpub` export |
| T16 | Done | Single-sig addresses for legacy, nested, native segwit and BIP-86 taproot, on main, test and regtest |
| T17 | Done | Standard and compact SeedQR, encode and decode, null-byte entropy included |
| T18 | Done | Dice (50/99 and Coldcard's sample), final-word checksum, coin flips, partial final word |
| T48 (core) | Done | BIP-85 child mnemonics; the UI comes in Phase 6 |
| Vectors | Done | `scripts/vectors/gen_seedsigner_vectors.py` runs SeedSigner's own code under its pinned embit and writes 290 vectors to `sim/vectors/seedsigner_core.h`. `test_btc` passes all of them on the host and on the Xtensa build; four deliberate mutations of `btc.c` each made it fail |
| T19 | Done | `psbt-review.c` ports `psbt_parser.py` check by check. `gen_psbt_vectors.py` runs SeedSigner's own `test_psbt_parser.py` with a capture plugin and records every PSBT it parses (207 unique) with SeedSigner's verdict. All 43 rejections match SeedSigner's exception class; all 155 reviews match (policy, amounts, change and its paths, destinations, OP_RETURN, high fee). Seven deliberate mutations, including verifying inputs before outputs and dropping the cosigner comparison, each fail the suite |
| T20 | Done | Signing as SeedSigner's `sign_with` + `trim`: 135 signed PSBTs byte-identical (ECDSA, low-R grinding as embit does). Taproot: the same structure and both signatures verified against the BIP-341 sighash; byte equality is not expected, because embit's bundled libsecp256k1 predates BIP-340's handling of an absent aux_rand |
| Stricter than SeedSigner | Recorded | A PSBT with no outputs is refused (SeedSigner reviews it as everything to fees, with no high-fee warning); an input without UTXO data is refused; inputs asking for a sighash other than ALL are refused rather than silently left unsigned; more than 20 claims of this seed in one scope is refused. No per-parse derivation cache yet: large consolidations will be slow on the device |
| T21 | Mechanism done | `s3cam-qemu-selftest` compiles the host's vector suites into the firmware and runs them at boot; `test_wally` (BIP-32 vector 1, BIP-84 zpub, fingerprint, bad checksum) passes on host and Xtensa |

### 2026-10-16: Phase 2, the QR layer on the host

| Task | State | Evidence |
|---|---|---|
| T22 | Open, needs the board | quirc carried over with LeekWallet's build settings. Measured on perfect synthetic symbols: quirc misses about 1 in 1000 at versions 18-22, 1 in 50 at v23, 1 in 5 at v24 and 1 in 2 at v25, which zbar (SeedSigner's reader) reads. Animated UR frames are v10 or below; a large static QR from a wallet may need a fallback |
| T23 | Done | `qr-detect.c` ports `detect_segment_type`, in SeedSigner's order, with the Python string rules it depends on in `pytext.c`. 2756 frames detected exactly as SeedSigner detects them, Unicode whitespace and invalid UTF-8 included |
| T24 | Done except descriptors | `qr-decode.c` ports `DecodeQR`; `ur-decoder.c` is rewritten to follow SeedSigner's fountain decoder step for step. `gen_qr_vectors.py` captures SeedSigner's own `test_decodepsbtqr.py` frame by frame (165 scans) and adds 350 generated ones: UR at three densities with frames dropped, BBQr H/2/Z, Specter, base64, base43, every seed format, addresses, sign-message, settings. 4827 frames: every status, type and progress figure, and every PSBT, seed, address and message, as SeedSigner's. Wallet descriptors are detected and wait for T47 |
| T25 | Done | Byte mode and fit-to-version in `qrcode.c`; bit for bit identical to python-qrcode (SeedSigner's encoder) on 150 symbols, versions 1-25 |
| T26 | Done on the host | `qr-encode.c` ports SeedSigner's encoders: SeedQR, CompactSeedQR, static, Specter and UR crypto-account xpubs, UR crypto-psbt. 6246 parts identical to SeedSigner's, fountain parts included; codes read back through quirc and the port's own decoder |
| Different from SeedSigner | Recorded in `qr-decode.h` and `ur-decoder.h` | A stray frame of another type mid-scan is reported, not raised; a failed UR checksum is INVALID; ASCII digits only; a truncated BBQr zlib stream gives no PSBT. The fountain sums probabilities left to right, as bc-ur and Sparrow do (SeedSigner OS's Python 3.12 sums differently in the last bit; no fragment choice changes) |
| T25 vectors | Done | python-qrcode's symbols recorded by the generator; 33 symbols, versions 1-25, identical module for module, the version chosen identical too |
| Scan screen | Done in QEMU | `screens-scan.c`: SeedSigner's ScanView, ScanPSBTView and ScanSeedQRView over the decoder, fed by the `qr-source` mailbox the camera will fill. `test_scan` replays every complete recorded PSBT and seed scan through the screen (161 and 63). The harness takes `Q <hex>` frames and reports `N` notes; `qemu-flow.py` scans a CompactSeedQR (fingerprint 73c5da0a), an animated UR PSBT, a zlib BBQr, a wrong type and an invalid code on the Xtensa image |
| T21 | Extended | The self-test image runs all twelve vector suites on Xtensa, QR included, in its own 15 MB partition table |
| Next | | Camera bring-up (T9, T22) when the board is in hand; meanwhile Phase 3 |

### 2026-10-17: Phase 3, the UI framework on the host and in QEMU

| Task | State | Evidence |
|---|---|---|
| T28 | Done | Screen engine with enter/render/input/leave and now `tick` (frames, animation), `ui_replace`, a settable clock and machine-readable notes. `test_ui`: ten levels down, BACK walks home and HOLD jumps home, every `leave` runs, on a replace and on reset too |
| T29 | Done | `widgets.c` ports SeedSigner's BtcAmount and FormattedAddress; `gen_ui_vectors.py` runs SeedSigner's own components headless, as its screenshot generator does, and records what they draw: 444 amounts and 260 address layouts identical (four widths where SeedSigner's layout raises are refused here too). Deliberately different: SeedSigner prints 99 sats as "9.9E-7 btc"; here 0.00000099. `screens-common.c`: SeedSigner's Success/Warning/DireWarning/Error screens with paging bodies, and the working frame. `test_screens` on all five profiles |
| T30 | Logic done | LeekWallet's text entry carried over with its suite: every printable ASCII character reachable; a 17-character passphrase costs 205 presses flat, 115 with blocks. The passphrase screen comes with T36 |
| T31 | Done on the host | `qr-view.c` and the QR display screen: SeedSigner's two-module border and 5/30 s frames, brightness on UP/DOWN (OLED contrast). `test_qr_view` and `test_screens` read the framebuffer back with quirc on all five profiles, animated PSBTs included; 9 animated PSBTs assemble off the oled128 screen |
| T32 | Open, needs the board | Viewfinder |
| Was open | Fixed 2026-10-17 | `test_qr_view` missed static codes in the QEMU self-test. The cause was memory in the test, not quirc: it resized quirc and allocated quirc's decode result for every read, which an image without PSRAM could not always supply. Both are allocated once now, and the suite is in the self-test again |
| T33 | Done | `secmem.c`: secrets come from internal RAM only (ESP: `MALLOC_CAP_INTERNAL`, never PSRAM) and are wiped on free. A scanned seed never enters the decoder's arena, which may be PSRAM; `test_qr_decode` proves the arena untouched for every seed format, `test_scan` proves the Scan screen's decoder and seed came from `sec_alloc` and went back |
| T39 | Done, single-sig and multisig without a descriptor | `views-psbt.c`: 18 of SeedSigner's 19 PSBT views. `test_psbt_flows` ports `test_flows_psbt.py` and walks all 207 parser-test PSBTs through the screens on five profiles; 155 sign to SeedSigner's PSBT. `qemu-flow.py psbt-sign` signs on the Xtensa image. Verifying multisig change against a descriptor waits for T47 |
| T35 | Done except camera entropy | `views-tools.c`: SeedSigner's dice views (50 or 99 rolls on a 1-6 keypad with DEL) and the five final-word calculator views (coin flips, a chosen word, or zeros, then the checksum word). SeedSigner has no flow tests for these, so `gen_tools_vectors.py` runs its own Views through its FlowTest harness and records what they compute; `test_tools_flows` matches all 12 dice and 18 final-word cases on five profiles. `qemu-flow.py dice-seed` makes a seed from 50 rolls on the Xtensa image. Camera entropy waits for the board (T9) |
| T51 | Done, single sig | `views-tools.c`: SeedSigner's four address explorer views, entered from the Tools menu or a seed's options, with a seed loaded mid-flow resuming it (`resume_main_flow`). `test_tools_flows` ports `test_flows_tools.py`'s explorer cases, all four BACK scenarios included, and checks 48 address lists SeedSigner's own explorer made (960 addresses: 2 seeds, 3 networks, 4 script types, receive and change, two pages). `qemu-flow.py address-explorer` shows BIP-84's first address on the Xtensa image. Wallet descriptors (multisig) and custom derivations wait for T47 |
| T37 | Done except the camera | SeedSigner's backup test (four views: every word asked once in random order, the real word among three decoys drawn from the platform RNG) and SeedQR transcription (nine views: format, warning, the whole code, zone by zone A-1 to F-6, and a confirming scan with its three outcomes). `test_seed_flows` ports `test_transcribe_seedqr_and_verify` and reads the panel back with quirc: standard and compact, 12 and 24 words, all five profiles, each is the seed's own SeedQR. `qemu-flow.py seedqr-transcribe` on the Xtensa image. Scanning the paper copy needs the camera (T9) |
| T40 | Done, single sig | `views-address.c`: SeedSigner's six address verification views. A scanned address is sorted by type (nested segwit asks single or multi; a P2WSH address is multisig and waits for T47), the seed is chosen or loaded mid-flow, and the search runs on the screen's tick, receive and change, with Skip 10 and Cancel. `test_tools_flows` ports `test__verify_address__singlesig__flow` and finds 144 addresses from SeedSigner's own explorer lists at their index and branch; `qemu-flow.py verify-address` verifies BIP-84's first address on the Xtensa image |
| Found by QEMU | Fixed | The Scan screen sized its PSBT buffer to its whole arena, which a board short of RAM could not give; it is now bounded by the scan (`qr_decoder_psbt_max`), checked against every recorded PSBT |

### 2026-10-17: Phase 4 begins, measured in SeedSigner views

Progress is now counted in SeedSigner's views (`docs/COVERAGE.md`). This step adds 18:
the seeds menu, typing a 12- or 24-word seed (LeekWallet's word entry, carried
over with its suite), the invalid-checksum dialog, finalising with or without a
BIP-39 passphrase (add, review, edit, discard, skip), seed options, discarding,
viewing the backup words, and the whole export-xpub flow. `sim/test_seed_flows.c`
ports SeedSigner's own `tests/test_flows_seed.py` cases step for step, pressing
the keys a user would (all 21 sig/script/format combinations of the xpub
export), and `qemu-flow.py seed-lifecycle` runs scan, finalise, export (the
BIP-84 zpub on the firmware) and discard on the Xtensa image. Screens now carry
SeedSigner's View names. Settings live in RAM with SeedSigner's defaults; seeds
in `seed-storage.c`, internal RAM only. 29 of 109 views.

**Found by the firmware, fixed (2026-10-17).** Running the seed flows on the
Xtensa image in QEMU found what the host could not: the xpub details step
overflowed the UI task's 8 KB stack (now 16 KB); the harness compared screen
names by pointer, so a view reusing a freed slot looked unchanged (now by
text, and every QEMU flow fails on any panic or stack overflow); a return to
the main menu kept the old selection where SeedSigner opens a new menu; the
xpub QR encoder sat in internal RAM for the life of the device (now allocated
only while shown); and a large zlib BBQr asked for more than a board without
PSRAM has (now the largest buffer available). The QEMU self-test runs 14
suites, all passing; `qemu-flow.py seed-lifecycle` passes in 18 steps.

### 2026-10-17: PSBT signing on screen

18 of SeedSigner's 19 PSBT views (`views-psbt.c`): choose the signing seed
(with SeedSigner's "(?)" hint, or a seed loaded mid-flow by scan or typing,
which resumes the review), the overview, the unsupported-script, high-fee and
full-spend warnings, the transaction math, each recipient, each change output
with its path, OP_RETURN, approval, the signed PSBT as an animated UR, the
signing error, the wrong-seed screen, and the five refusals of a PSBT whose
account of itself does not hold up. Going home drops the PSBT, as
MainMenuView does. `sim/test_psbt_flows.c` ports SeedSigner's
`tests/test_flows_psbt.py` step for step (`gen_psbt_vectors.py --tests
tests/test_flows_psbt.py` records its PSBTs) and walks every PSBT of its
parser tests through the screens, on all five display profiles: 1741 steps,
each the View SeedSigner routes to, showing what SeedSigner's parser found,
and 155 approvals signing to SeedSigner's PSBT. `qemu-flow.py psbt-sign` scans
a PSBT and its seed and signs it on the Xtensa image. 47 of 109 views.

Two differences, recorded in the code: SeedSigner's unsupported-script
warning never fires (its parser always sets a policy once there is an input),
so here it fires on an unrecognised input script, as SeedSigner's comment
intends; and a PSBT with no outputs is refused (already recorded under T19).
PSBTAddressVerificationFailedView waits for wallet descriptors (T47). List
views whose text does not fit the screen now page it with DOWN, so a long
address never pushes the button off a small panel.

**Found by the self-test, fixed (2026-10-17).** `test_qr_encode` failed on
the Xtensa image ("did not read back") whenever less internal RAM was free
when it ran: it passed alone with 272 KB free, failed with the largest block
held to 147 KB, and had already failed once at 186 KB. The test resized
quirc's canvas for every code, about 90 KB of reallocation each time, and
without PSRAM that sometimes could not be had; decoding was never reached.
It now sizes one canvas at the start and says "out of memory" if it ever
cannot, and passes on Xtensa at the 147 KB limit. The open `test_qr_view`
failure in QEMU may have the same cause; that is the next thing to check.

### 2026-10-17: New seeds from dice, and the final-word calculator

Seven Tools views (`views-tools.c`): the dice length and entry, and the
final-word calculator from the number of words to loading or discarding
the result. The word entry gained SeedSigner's `is_calc_final_word` mode:
it stops one word short and hands over to the calculator, and a second
entry screen types the chosen last word. SeedSigner's dice and coin-flip
keyboards become one keypad for the encoder (UP/DOWN choose a face or DEL,
OK enters it); the rolls are entropy, held in internal RAM and wiped when
the keypad closes. SeedSigner's tests have no flows for these, so
`scripts/vectors/gen_tools_vectors.py` runs SeedSigner's own Views through
its FlowTest harness and records the mnemonic, fingerprint and every
figure the final-word screen shows. All 30 cases match on five profiles.
54 of 109 views.

`test_qr_view` is back in the QEMU self-test and passes there. Its long-open
failure had the cause found in `test_qr_encode`: memory, not decoding. Both
suites resized quirc for every read (quirc allocates the new canvas before
freeing the old, so each read needed twice the memory) and allocated
quirc's 9 KB decode result per code. With both suites in one image, the
Xtensa run showed quirc finding every code and the decode buffer's malloc
returning NULL. Both buffers are now allocated once, when each suite starts;
on the board, quirc's buffers will live in PSRAM.

### 2026-10-17: The address explorer

Four Tools views: choose the source (a loaded seed, or one scanned or typed
mid-flow, which resumes the explorer as SeedSigner's `resume_main_flow`
does), the script type (skipped when only one is enabled), receive or
change, then pages of ten addresses, each shown as a QR that returns to the
list at the same address. `gen_tools_vectors.py` now also runs SeedSigner's
explorer through its FlowTest harness and records every list it makes;
`test_tools_flows` ports the explorer cases of `test_flows_tools.py` and
matches all 960 addresses on five profiles. Stack views now take up to 12
buttons and open on a given selection. 58 of 109 views.

### 2026-10-17: The backup test and SeedQR transcription

Thirteen seed views. After the backup words, SeedSigner's optional test asks
for every word once, in random order, among three decoys; here the order and
decoys come from the platform's RNG (`btc_random`), and the next word is
drawn from those left, so it never retries. SeedQR export draws the whole
code with a one-module quiet zone, then zone by zone as on SeedSigner's
printed templates (UP/DOWN step through the zones on the encoder), and
offers a confirming scan that reports a different seed, an unreadable or
non-SeedQR code, or success. The SeedQR is the seed: its modules live in
internal RAM and are wiped, with the panel, when the screens close.

Two deliberate differences: SeedSigner lays light grey zone lines over the
whole code, which on a one-bit panel change modules, so here the whole code
is drawn plain and must scan as the SeedQR (the suite reads it back with
quirc on all five profiles); and with compact SeedQR disabled SeedSigner
labels a 24-word code 25x25, where this port shows 29x29. 71 of 109 views.


**Self-test headroom (2026-10-17).** libwally is built with `BUILD_MINIMAL`,
which drops its non-English BIP-39 wordlists. Each is 2048 pointers in
internal RAM; the release image never linked them, but the self-test image
does (its suites call `bip39_get_wordlist`), and they took 49 KB of the
heap the suites run in: free heap before the QR suites rose from 208 KB
(largest block 139 KB) to 257 KB (188 KB). Note for the build: PlatformIO
does not rebuild a component when only its compile definitions change, so
such a change needs `pio run -t clean` on every image before `check.sh`.

### 2026-10-17: Address verification

Six views (`views-address.c`): scan an address from the Tools menu (or the
main Scan), let its type decide single sig or multisig as SeedSigner does
(asking only for nested segwit, which cannot tell), choose or load the
seed, and search its receive and change addresses until one matches.
SeedSigner searches in a background thread; here the search runs a couple of
indexes per UI tick, so Skip 10 and Cancel stay live, and like SeedSigner's
it has no upper bound. A seed loaded mid-flow resumes the search
(`resume_main_flow`, checked first, as SeedSigner does). 77 of 109 views.

### 2026-10-17: BIP-85 child seeds

Three views: the child's length, its index on a digit keypad, and the
invalid-index warning. The child is shown with SeedSigner's words screen
("Child #N") and can be backup-tested like a seed; its words are a seed in
their own right, so they live in internal RAM and are wiped when the words
screen closes. The keypad that dice and coin flips used is now its own
module (`keypad.c`), with an OK key for entries of no fixed length.
`test_seed_flows` derives all 18 children SeedSigner's own code derived
and checks them word for word on screen, on five profiles. One deliberate
difference: SeedSigner raises on an empty index; here it is an invalid
index like any other. 80 of 109 views.

