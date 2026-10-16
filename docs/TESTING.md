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
  input sequences against the screen graph.

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
host → device   Q <payload>        a QR payload, as if quirc had decoded it
                                   (binary payloads are hex-encoded: QH <hex>)
device → host   F <base64 frame>   the framebuffer (palette indices) whenever it
                                   changes; 1 KB for oled, up to 37.5 KB for tft320
device → host   S <screen-id>      the current screen, for assertions
device → host   A                  the event was handled and its frame sent
```

`scripts/qemu-flow.py` drives this through `scripts/qemu_harness.py`, so a test reads like a
SeedSigner flow test: scan a SeedQR, confirm the fingerprint, scan a PSBT, and
check the signed UR that comes out.

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
