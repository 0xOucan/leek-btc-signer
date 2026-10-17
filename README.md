# Leek BTC Signer

**A stateless, air-gapped Bitcoin signer for the ESP32-S3**, inspired by
[SeedSigner](https://github.com/SeedSigner/seedsigner) and rebuilt in C for a
small board: a 1.5" monochrome OLED, a rotary encoder, four buttons, and a camera
for QR codes. The seed lives in RAM only. Unplug it and the seed is gone.

> **Status: in development. Not for real funds.** The Bitcoin core, PSBT
> signing and the QR layer are done and checked against SeedSigner itself. The
> screens and the camera are next. Nothing has run on the physical board yet.

![The UI on the 1.5" 128x128 OLED, rendered by the host build](docs/img/oled128.png)

## Why

SeedSigner showed that a signing device can be stateless, cheap, built from
parts, and fully air-gapped, with QR codes as the only channel to the wallet.
Leek BTC Signer brings that design to an ESP32-S3 board that costs a few dollars.
It boots in a moment, has no operating system, and has no radio stack in the
firmware at all.

It follows SeedSigner closely on purpose:

- **The same flows and menus:** Scan, Seeds, Tools, Settings.
- **The same SeedQR format.**
- **The same PSBT checks.** The Bitcoin logic is ported from SeedSigner and
  tested against SeedSigner's own code, so the device accepts, rejects, reviews
  and signs a PSBT the way SeedSigner does.

## Progress

```
SeedSigner  ██████████████░░░░░░  77 of 109 views (71%)
```

![SeedSigner views working on Leek BTC Signer, by area](docs/img/progress.svg)

**Progress is measured against SeedSigner itself.** The denominator is every
View class in SeedSigner's source, the screen-steps a SeedSigner user can
reach (109 of them). A view counts only when the device does what
SeedSigner's view does and a test proves it, in QEMU or on the host. Each
view weighs the same. The per-view list, with the test behind every tick, is
[docs/COVERAGE.md](docs/COVERAGE.md); `scripts/coverage.py` draws the chart.

The figure starts low on purpose: so far the work went into what sits under
the screens, the parts that are hard to get right and that the views reuse:
keys and addresses, the PSBT rules and signing, every QR format, and the
screen engine. All of it is already proven against SeedSigner's own code
(see [How it is verified](#how-it-is-verified)), so from here most views
are screens on top of it.

## SeedSigner and Leek BTC Signer, side by side

**The hardware**

| | SeedSigner | Leek BTC Signer |
|---|---|---|
| Computer | Raspberry Pi Zero (v1.3, no wireless) | ESP32-S3 N16R8 CAM board: 16 MB flash, 8 MB PSRAM |
| Software | Python on SeedSigner OS, a minimal Linux booted from a microSD card | C firmware on ESP-IDF, in the chip's flash; no SD card needed |
| Screen | 1.3" 240x240 colour LCD | 1.5" 128x128 monochrome OLED today; 240x240 colour TFT supported for later |
| Controls | Joystick and three keys | Rotary encoder (EC11) and four keys; encoder plus one key on the TFT modules |
| Camera | Raspberry Pi camera (OV5647) | OV5640 on the board |
| Radios | None on the Pi Zero v1.3 | The chip has Wi-Fi and Bluetooth hardware; **no radio stack is linked**, and every build is checked for that |
| QR reader | zbar | quirc |
| Seed storage | Stateless | Stateless by default; an optional vault, opt-in and at the user's own risk (Phase 5) |

**The features**, with Leek BTC Signer's state:

| Feature | SeedSigner | Leek BTC Signer |
|---|---|---|
| SeedQR, CompactSeedQR: scan and show | ✅ | ✅ scan to a loaded seed; transcription zone by zone, and a confirming scan |
| Backup words and the backup test | ✅ | ✅ words page by page, then every word verified in random order with decoys |
| Seeds from words, dice, coin flips | ✅ | ✅ typing 12 or 24 words; dice (50 or 99 rolls); the final-word calculator from coin flips, a chosen word or zeros; camera entropy needs the board |
| BIP-39 passphrase | ✅ | ✅ add, review, edit, discard, as SeedSigner's flow |
| xpub export: static, Specter, animated UR | ✅ | ✅ the whole flow, all 21 of SeedSigner's combinations |
| PSBT scan: UR, BBQr, Specter, base64, base43 | ✅ | 🟡 scan to seed selection works end to end in QEMU, matching SeedSigner frame by frame; camera needs the board |
| PSBT review and the anti-forgery checks | ✅ | ✅ the whole flow on screen: seed choice, overview, warnings, math, each recipient and change output, OP_RETURN, every refusal; plus three stricter refusals (below) |
| PSBT signing: segwit, nested, legacy, taproot | ✅ | ✅ byte-identical signed PSBTs (taproot verified) |
| Animated UR output of the signed PSBT | ✅ | ✅ shown after approval; parts identical to SeedSigner's |
| Address explorer | ✅ | ✅ receive and change addresses page by page, each as a QR; identical to SeedSigner's for every script type and network; multisig descriptors in Phase 6 |
| Address verification | ✅ | ✅ single sig: scan an address, pick or load the seed, search receive and change until it matches; multisig with descriptors in Phase 6 |
| Multisig: descriptors, cosigner checks | ✅ | 🟡 multisig PSBTs reviewed and signed; change verification against a descriptor in Phase 6 |
| BIP-85 child seeds | ✅ | 🟡 logic done; screens in Phase 6 |
| Message signing | ✅ | 🟡 request decoding done; signing in Phase 6 |
| SettingsQR | ✅ | 🟡 decoding done; settings screens next |
| Languages other than English | ✅ | ⬜ not planned yet |

✅ done · 🟡 partly done · ⬜ not started

## What it does

| | Status |
|---|---|
| Seed from SeedQR (standard and compact), word entry, dice, coin flips | Done, on screen |
| BIP-39 passphrase, fingerprint, BIP-85 child seeds | Logic done |
| xpub export: native and nested segwit, taproot, legacy, multisig paths, SLIP-132 versions | Logic done |
| PSBT review: change detection, fee, OP_RETURN, the anti-forgery checks | Done |
| PSBT signing: ECDSA and taproot key path | Done |
| Address verification | Done, single sig |
| QR decoding: UR, BBQr, Specter, base64/base43 PSBTs, every SeedQR form, addresses, sign-message | Done on the host, matches SeedSigner frame by frame |
| QR output: SeedQR, xpubs (static, Specter, UR), animated UR PSBTs | Done on the host, matches SeedSigner part by part |
| The Scan screen: progress, SeedSigner's wrong-type and invalid errors | Done in QEMU, fed SeedSigner's recorded scans |
| QR display: static and animated codes, brightness keys, read back off the framebuffer | Done on the host and QEMU |
| Status screens, amounts and addresses as SeedSigner draws them | Done |
| Camera capture and viewfinder | Needs the board |
| Optional vault for a stored seed, opt-in and at the user's own risk | Phase 5 |

Each status has evidence behind it in [ROADMAP.md](ROADMAP.md).

## How it is verified

There's no board on the bench yet, so everything is proven in three tiers
([docs/TESTING.md](docs/TESTING.md)):

1. **Host:** the firmware's own C, compiled with gcc under AddressSanitizer and
   UBSan, for every display profile.
2. **QEMU:** the real ESP32-S3 firmware image, on Espressif's emulator.
   - The same test suites run inside it at boot, so the code is proven on the
     Xtensa build, not only on x86.
   - A serial harness lets scripts drive the menus and read back every frame.
3. **Board:** camera, OLED, encoder, timing, real entropy. When it arrives.

**The test vectors come from SeedSigner itself.** The scripts in
[`scripts/vectors/`](scripts/vectors/) run SeedSigner's Python code under the
embit version it pins and record what it does:

| What | Vectors | Result |
|---|---|---|
| Paths, xpubs, addresses, xpub exports, SeedQR, dice, BIP-85 | 290 | all match |
| Every PSBT SeedSigner's own test suite parses | 207 | see below |
| QR scans (SeedSigner's own decode tests and generated ones), frame by frame | 515 scans, 4827 frames | all match; descriptors wait for multisig |
| QR type detection | 2756 frames | all match |
| QR output parts | 6246 | all identical |
| QR symbols, module by module (python-qrcode) | 33, versions 1-25 | all identical |
| Amounts and address layouts SeedSigner's components draw | 720 | 704 identical; 12 tiny btc amounts deliberately not in "9.9E-7" form; 4 widths both refuse |

The PSBT cases break down like this:

| PSBT cases | Result |
|---|---|
| 43 | Rejected with SeedSigner's exact error |
| 155 | Reviewed identically |
| 135 | Signed byte-for-byte as SeedSigner signs them |
| 20 | Taproot: our signature and SeedSigner's both verify as valid BIP-340 |
| 9 | Refused on purpose (see below) |

**Deliberate mutations prove the tests catch real bugs.** Eleven times, the
security logic was broken on purpose: for example, skipping the forged-output
check, or checking inputs before outputs. The suites failed every time.

### Where it is stricter than SeedSigner

- **A PSBT with no outputs is refused.** SeedSigner reviews it as everything
  going to fees, with no high-fee warning.
- **An input without UTXO data is refused.**
- **An input asking for a sighash other than ALL is refused,** rather than left
  silently unsigned.

## Hardware

The bench setup is an ESP32-S3-WROOM-1-N16R8 CAM board with an OV5640 camera,
plus:

- a 1.5" SH1107 128x128 OLED,
- a four-button module (UP, DOWN, BACK, OK),
- an EC11 rotary encoder whose push is wired to OK.

Wiring, pin map, and a first-boot checklist are in
[docs/HARDWARE.md](docs/HARDWARE.md).

**The display is a build-time profile.** The same code drives a 128x64 SSD1306,
and later the colour ST7789/ST7735 modules, using SeedSigner's own palette:

![The same screens on the 240x240 colour profile](docs/img/tft240.png)

## Build and test

ESP-IDF 5.5 is pinned through PlatformIO (`espressif32@6.12.0`). Every
dependency is vendored, so builds fetch nothing.

```sh
./scripts/check.sh                 # everything below, as CI runs it

make -C sim test                   # host suites, all display profiles
make -C sim shots                  # screenshots of every screen
make -C sim sim PROFILE=OLED128    # desktop simulator (SDL2): arrows, Enter, Esc
./sim/build/OLED128/leek-sim

./scripts/qemu.sh --smoke                                  # boot the firmware in QEMU
./scripts/qemu.sh --env s3cam-qemu-selftest --selftest     # vector suites on Xtensa
./scripts/qemu-flow.py                                     # drive the menus in QEMU

pio run -e s3cam-dev -t upload     # the board, with a USB console
```

ESP-IDF refuses to build in a path that contains a space, so clone into a
plain path.

| Environment | Image |
|---|---|
| `s3cam` | Release: no console, no radios, no harness (each checked from the ELF) |
| `s3cam-dev` | The board with a USB-Serial-JTAG console |
| `s3cam-qemu` | QEMU, with the UI harness on UART0 |
| `s3cam-qemu-selftest` | QEMU, running the vector suites at boot |

## Layout

```
src/core/     pure C, host and device: gfx, input, screens, btc, psbt-review
src/hal/      ESP-IDF only: OLED driver, input polling, QEMU harness
components/   libwally-core + secp256k1-zkp, vendored and pinned
sim/          host tests, generated vectors, screenshots, SDL simulator
scripts/      QEMU, image checks, vector generators
docs/         hardware, testing
```

## Acknowledgements

- **[SeedSigner](https://github.com/SeedSigner/seedsigner)** (MIT) is the model
  for this project's design, flows, SeedQR format and PSBT checks, and the
  source of its test vectors. Thank you to its contributors. Leek BTC Signer is
  independent and not affiliated with or endorsed by SeedSigner.
- **LeekWallet**, an ESP32-S3 EVM hardware wallet by the same author, provides
  the board support, OLED and button handling. Its Zcash firmware provides the
  encoder decoder.
- **[libwally-core](https://github.com/ElementsProject/libwally-core)** and
  **secp256k1-zkp** do the cryptography, as they do in Blockstream's Jade.

The full inventory is in [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md).

## License

Apache-2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
