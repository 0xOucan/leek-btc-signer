# Hardware: ESP32-S3-WROOM-1-N16R8 CAM + OV5640

The board LeekWallet's `esp32s3cam` profile already runs on. The authoritative
pin reference is LeekWallet's `docs/BOARD-S3CAM-PINOUT.md`, spot-checked with a
multimeter on 2026-09-17. This file only records what Leek BTC Signer adds or
changes.

| Part | Spec |
|---|---|
| MCU | ESP32-S3, 16 MB flash, 8 MB octal PSRAM |
| Display | SSD1306 0.96" 128x64 monochrome OLED, I2C 0x3C |
| Camera | OV5640 over DVP, grayscale, autofocus off |
| Input | 4 fixed buttons + EC11 rotary encoder with push switch |
| Radios | **Not used.** Wi-Fi and Bluetooth are compiled out. |

## Pin map

| Function | GPIO | Origin |
|---|---|---|
| Camera (DVP + SCCB) | 4–13, 15–18 | LeekWallet, unchanged |
| OLED SDA / SCL | 47 / 21 | LeekWallet, unchanged |
| K1 | 1 | LeekWallet, unchanged |
| K2 | 41 (MTDI) | LeekWallet, unchanged |
| K3 | 14 | LeekWallet, unchanged |
| K4 | 42 (MTMS) | LeekWallet, unchanged |
| **EC11 A** | **39** (was SD_CLK / MTCK) | new |
| **EC11 B** | **40** (was SD_DATA / MTDO) | new |
| **EC11 push** | **38** (was SD_CMD) | new |
| Status LED | 48 (WS2812) | LeekWallet, unchanged |
| PSRAM | 35–37 | reserved |
| USB-Serial-JTAG | 19 / 20 | dev builds only |

The encoder uses the on-board microSD pins, which nothing used. That means
**no microSD**: settings come from SettingsQR, or from NVS if the user enables
the vault. External JTAG on 39/40 is also given up; native USB JTAG on 19/20 is
unaffected.

GPIO 43/44 are free but avoided: the on-board TX/RX LEDs sit on them and can
load an input line.

**To verify on the bench:** that 38/39/40 have no pull resistors or SD-slot
parts that fight the encoder. Remove the card from the slot.

## EC11 wiring

- The common pin goes to GND. A and B use internal pull-ups, active low.
- The push switch goes to GND, with an internal pull-up.
- Optional but recommended: 10 nF from A and from B to GND, to tame contact
  bounce.
- Decoded by the S3's PCNT (pulse counter) in quadrature mode, with its glitch
  filter. No polling and no ISR per edge.

## Controls

The four buttons can't be removed, so each gets a role. Every screen can be
driven from the encoder alone **or** from the buttons alone. That gives
redundancy if one fails, and it lets LeekWallet's scripted button tests keep
working.

| Input | Role | SeedSigner equivalent |
|---|---|---|
| Encoder turn | Move up / down, change value | Joystick up / down |
| Encoder push | Select / confirm | Joystick press |
| K1 (UP) | Previous page / previous letter group | Joystick up (fast) |
| K2 (DOWN) | Next page / next letter group | Joystick down (fast) |
| K3 (CANCEL) | Back. Hold 3 s to clear the seed and lock | Joystick left / back |
| K4 (ACCEPT) | Confirm (same as encoder push) | KEY / joystick press |

## Known limit: QR export on 0.96"

At 0.96", one QR module at 2x scale is about 0.34 mm. Phones read it reliably;
laptop webcams (e.g. Sparrow on desktop) may not. Phase 2 measures this. The
fallback is a 2.42" SSD1309, which has the same 128x64 resolution and the same
driver family, so no UI changes are needed.
