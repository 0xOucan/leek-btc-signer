# Testing

Everything is tested without the board first. There are three tiers, from
fastest to most faithful. Use the cheapest tier that can express the test.

| Tier | What runs | Speed | What it proves |
|---|---|---|---|
| **1. Host** (`sim/`) | Firmware `.c` files compiled with gcc against shims; fake OLED, fake encoder and buttons, fake NVS | ms | Logic, Bitcoin vectors, screen graph, secret lifetimes. Runs under ASan/UBSan. |
| **2. QEMU** (`scripts/qemu.sh`) | The real Xtensa firmware image on Espressif's `esp32s3` machine | seconds | The same binary boots. Crypto is correct on the target compiler. NVS/vault. Secure boot + flash encryption against emulated eFuses. Full UI flows through the QEMU harness. |
| **3. Board** | ESP32-S3-WROOM-1-N16R8 CAM | minutes | Camera, OLED, encoder, PSRAM, real RNG, timing, heat, QR readability |

`scripts/check.sh` runs tiers 1 and 2 and is what CI runs.

## Tier 1: host

LeekWallet's `sim/` harness is carried over, with these additions:

- `fake_encoder`: scripted detents and pushes, which emit the same input events
  as the buttons.
- **Golden vectors ported from SeedSigner's pytest suite.** These include
  `test_seedqr`, `test_mnemonic_generation`, `test_bip85`, `test_embit_utils`,
  `test_psbt_parser` and `test_decodepsbtqr`. Each ported file names the
  SeedSigner test it came from.
- **Screenshots for every display profile.** The fake panel writes each
  distinct frame as a 1-bit PBM for `oled` and as a PPM at the real resolution
  for `tft240`/`tft320`/`tft160`. Every screen can be reviewed on every profile
  without hardware, including the color ones not yet in hand. This plays the role of
  SeedSigner's screenshot generator.
- **Flow tests.** SeedSigner's `tests/test_flows_*.py` translated into scripted
  input sequences against the screen graph: `test_seed_flows.c`,
  `test_psbt_flows.c` and `test_tools_flows.c`, sharing the key-pressing
  helpers in `sim/flow.h`. Where SeedSigner has no flow test (dice, the
  final-word calculator), its own Views are run through its FlowTest harness
  to record what they compute, and the device must match.
  `test_psbt_flows` also walks every PSBT of SeedSigner's parser tests through
  the whole review, checking each View against SeedSigner's routing and each
  signed PSBT against SeedSigner's, on every display profile.
  `test_multisig_flows.c` ports SeedSigner's two multisig flow tests and
  reaches every `verify_multisig_output()` its parser tests make through the
  PSBT review's "Verify multisig change"; where a wrong descriptor fails an
  earlier change output, the flow ends there, as SeedSigner's would.
  `test_electrum_flows.c` ports SeedSigner's Electrum seed flow tests (entry,
  xpub export, the explorer sideflow) and reaches its overrides as a user
  would: no BIP-85 or SeedQR, the custom extension, address verification and
  message signing with an Electrum seed loaded mid-flow.
- **Electrum seeds** (`test_electrum.c`, also in the Xtensa self-test):
  143 ElectrumSeed constructions (seed, fingerprint, the extension as
  normalised, and which mnemonics it refuses), 96 xpub exports at m/0h and
  m/1h, 1512 addresses below m/0h and 36 message signatures.
- **Multisig descriptors** (`test_descriptor.c`, also in the Xtensa
  self-test): every wallet descriptor scan SeedSigner's DecodeQR made (its
  tests' and generated ones, 46 of them at the corners of its JSON and
  setup-file parsing), ScanView's rewrite, embit's policy and fingerprints,
  360 multisig addresses and 21 change verifications.

### Desktop simulator

`sim/leek-sim` (SDL2) runs the same UI and flow code as the firmware in a window
at the selected profile's exact resolution and colors. This is how the color
profiles are built and reviewed before a module is in hand.

| Key | Input |
|---|---|
| ↑ / ↓ | Encoder turn |
| Enter | Encoder push |
| Esc, Backspace | BACK; hold 3 s for HOLD |
| S | Save a screenshot |
| Q | Quit |

Feeding QR payloads from files arrives with T23.

## Tier 2: QEMU

Espressif's QEMU fork (`qemu-system-xtensa`, `esp_develop_9.2.2` or later)
emulates the ESP32-S3 core, flash, NVS, eFuses, SHA/AES/RSA and the boot ROM.

### What it does not emulate

LeekWallet's `docs/QEMU.md` lists these, and the firmware has to work around
each one:

| Missing | Consequence | Workaround |
|---|---|---|
| USB-Serial-JTAG | Console is silent | The `-qemu` env routes the console to UART0 |
| PSRAM (espressif/qemu#129) | `CONFIG_SPIRAM=n` | Large buffers use a capped internal-RAM fallback; the PSBT size cap is lower under QEMU |
| I2C SSD1306 | No display | The firmware runs headless; frames go out through the harness |
| GPIO / PCNT | No buttons or encoder | Input comes in through the harness |
| Camera | No frames | QR payloads are injected through the harness, after the decoder |
| Real RNG noise | Entropy health tests fail by design | The QEMU env runs a test RNG that is clearly marked and refuses to sign mainnet. It never reaches a release. |
| Absolute timing | About 16x too fast (LeekWallet KDF benchmark) | Read timings only as ratios. Measure on the board. |

### The QEMU harness

The harness is a `-qemu`-only driver on UART0. It replaces the three things
QEMU lacks, so whole user flows run on the real firmware image:

```
host → device   K <event>          UP DOWN OK BACK HOLD
host → device   Q <hex>            the bytes of one decoded QR frame, as the camera's
                                   reader would return them (hex, so CompactSeedQR's
                                   binary crosses intact)
host → device   R                  resend the current frame
device → host   F <w> <h> <base64> the framebuffer (palette indices) whenever it
                                   changes; 8 KB for oled128, up to 37.5 KB for tft320
device → host   S <screen-id>      the current screen, for assertions
device → host   N <note>           what a screen concluded, e.g. "seed 12 73c5da0a"
                                   or "psbt 1234 9f86d081"; never a secret
device → host   A                  the input or frame was handled and its frame sent
device → host   E <message>        a harness error
```

A `Q` frame goes into the same mailbox (`src/core/qr-source.c`) the camera
fills on the board, so everything from the Scan screen down is the code that
runs on the device. `scripts/qemu-flow.py` drives this through
`scripts/qemu_harness.py`, feeding scans SeedSigner itself recorded
(`scripts/qr_vectors.py` reads them out of `sim/vectors/seedsigner_qr.h`): a
CompactSeedQR must show fingerprint `73c5da0a`, an animated UR PSBT and a
zlib BBQr must arrive with SeedSigner's bytes, and the Scan a SeedQR screen
must refuse an address as SeedSigner does.

A flow fails at once if the firmware prints a panic, a stack overflow or a
software reset, rather than carrying on against a rebooted device.

**The harness must never ship.** It is compiled only when `LBS_QEMU_HARNESS=1`.
That flag is set only in `-qemu` environments, and a release check fails if the
harness symbols appear in a release image.

## Tier 3: board

Some things only the board can tell us:

- Camera decode rate.
- OLED QR readability, by phone and by webcam.
- Encoder feel and bounce.
- PSRAM behaviour.
- RNG statistics.
- KDF iteration timing.
- Secure boot and flash encryption with real eFuses. Burning them can't be
  undone, so this needs a dedicated test board.

Record each hardware result in the roadmap entry it closes, with the date.
