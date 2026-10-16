# Hardware

## The bench setup: S3 CAM board + screen kit

The board is the ESP32-S3-WROOM-1-N16R8 CAM with an OV5640. It is the same
board as LeekWallet's `esp32s3cam` profile, whose `docs/BOARD-S3CAM-PINOUT.md`
is the authoritative pin reference, spot-checked with a multimeter. The camera,
the microSD slot and the octal PSRAM are on the board and need no wiring.
`src/board.h` is the firmware's copy of this page, and the two must agree.

**Power every module from 3V3, never 5V.** Each module has its own pull-up
resistors to its supply pin, and those pull the ESP32-S3's pins up to that
voltage. The S3 is not 5 V tolerant.

| Part | Spec |
|---|---|
| MCU | ESP32-S3, 16 MB flash, 8 MB octal PSRAM |
| Display | 1.5" OLED GME128128-01, SH1107, 128x128 monochrome, I2C 0x3C (0x3D also probed) |
| Input | Four-button module (UP, DOWN, BACK, OK) + EC11 encoder module |
| Camera | OV5640 over DVP, in the board's camera connector |
| Radios | **Not used.** No Wi-Fi, Bluetooth or USB stack is linked (`scripts/check-radios.sh`) |

### Screen: SH1107 128x128, I2C

| Module | Board |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SCL | GPIO21 |
| SDA | GPIO47 |

The UI uses the whole 128x128 panel (display profile `oled128`).

### Four-button module: V G 1 2 3 4

| Module | Board | Function |
|---|---|---|
| V | 3V3 | |
| G | GND | |
| 1 | GPIO1 | UP |
| 2 | GPIO41 | DOWN |
| 3 | GPIO14 | BACK; hold 3 s to clear the seed and go home |
| 4 | GPIO42 | OK |

A pressed key must read **low**: pulled up, switch to GND.

### Rotary encoder: EC11 module, GND + SW DT CLK

| Module | Board | Function |
|---|---|---|
| GND | GND | |
| + | 3V3 | supply for the module's pull-ups |
| CLK | GPIO44, the header pin labelled **RX** | encoder A |
| DT | GPIO43, the header pin labelled **TX**, **through a 1 kΩ resistor** | encoder B |
| SW | GPIO42, joined to button 4 | press = OK |

- **Turning and pressing.** Turning the knob is UP and DOWN, one step per
  detent; clockwise is DOWN. Pressing it is OK. SW works in parallel with
  button 4 because both pull the line to GND.
- **Why GPIO43/44 are free.** They are UART0, free because the console is USB
  Serial/JTAG (dev image) or absent (release image).
- **Why DT has a 1 kΩ resistor.** The ROM still prints its boot banner on
  GPIO43 for a few milliseconds. If the encoder contact happened to be closed
  then, the pin would drive into a short, and the resistor takes that instead.
  Anything from 220 Ω to 2.2 kΩ works. Do not use 4.7 kΩ or more: against the
  module's 10 kΩ pull-up the pin would no longer read a clean low.
- **How the encoder is read.** A 1 ms poll feeds a quadrature state machine.
  It is the decoder from the LeekWallet Zcash firmware, carried over with its
  tests (`src/core/encoder.c`, `sim/test_encoder.c`). Bounce, and a half turn
  that comes back, produce no step.

### Pins that look free but are not

| Pins | Taken by |
|---|---|
| 4–13, 15–18 | Camera |
| 38–40 | microSD |
| 35–37 | PSRAM |
| 19–20 | USB |
| 0, 3, 45, 46 | Strapping pins: a module's pull-up there can stop the board booting or flashing |
| 2 | Power LED |
| 48 | RGB LED |

### Summary

```
GPIO47 SDA ── OLED SDA          GPIO1  ── BTN 1 (UP)
GPIO21 SCL ── OLED SCL          GPIO41 ── BTN 2 (DOWN)
RX(GPIO44) ── ENC CLK           GPIO14 ── BTN 3 (BACK)
TX(GPIO43) ─1k─ ENC DT          GPIO42 ── BTN 4 (OK) + ENC SW
3V3 ── OLED VCC, BTN V, ENC +   GND ── OLED GND, BTN G, ENC GND
GPIO38/39/40 microSD (on board)  GPIO4-18 camera (on board)
```

## First boot: the bring-up checklist

None of this has run on hardware yet. Flash the dev image, which has the USB
console:

```
pio run -e s3cam-dev -t upload && pio device monitor -e s3cam-dev
```

Then open **Settings → I/O test**. It counts every input event and highlights
the last one; hold BACK for 3 s to leave. Check, in order:

| Check | Expected | If not |
|---|---|---|
| Screen lights; log says `SH1107 128x128 at 0x3C` | Menu visible | Log lists every I2C device found: check power, GND, SDA/SCL swapped, 0x3D strap |
| Picture the right way up | Title at the top | `-DBOARD_SH1107_FLIP=1` |
| Each button counts once per press | UP, DOWN, BACK, OK | A key that counts on release, or constantly, is active-high: `-DBOARD_BUTTONS_ACTIVE_HIGH=1` |
| Knob clockwise | DOWN counts | `-DBOARD_ENC_REVERSE=1` |
| One count per click | 1 per detent | Two clicks per count: `-DENCODER_HALF_STEP` |
| Knob push | OK counts | Check the SW wire joined to GPIO42 |
| GPIO43/44 at rest, multimeter to GND | about 3.3 V | The on-board TX/RX LEDs may load the line; report the reading |

Add any flag needed to `build_flags` for the `s3cam` and `s3cam-dev`
environments in `platformio.ini`, and record the result here with the date.

## Display profiles

There is one codebase and no branch per display. The profile is chosen at
build time:

| Profile | Panel | Resolution | Input | Status |
|---|---|---|---|---|
| `oled128` | SH1107 1.5", I2C | 128x128, 1-bit | 4 buttons + EC11 | **The bench setup, default** |
| `oled64` | SSD1306 0.96", I2C | 128x64, 1-bit | 4 buttons + EC11 | Supported, same driver file |
| `tft240` | ST7789, SPI | 240x240 RGB | EC11 + K0 (module) | Future; SeedSigner's resolution |
| `tft320` | ST7789, SPI | 320x240 RGB | EC11 + K0 (module) | Future |
| `tft160` | ST7735S, SPI | 128x160 RGB | EC11 + K0 (module) | Future, lowest priority |

**How a profile is applied.** Screens describe content (a title, a list, a
message), and each profile's theme (`src/core/theme.c`) decides where it goes.
Colors are **roles**: `fg`, `bg`, `accent`, `warning` and so on. On the OLEDs
they map to lit or dark. On the TFTs they map to SeedSigner's own palette
(`src/core/palette.c`). Every profile is tested and screenshotted on the host
(`make -C sim test shots`) and can be run in the desktop simulator
(`make -C sim sim PROFILE=TFT240`).

### Framebuffer and secrets

Seed words are drawn into the framebuffer, so the framebuffer is secret. It is
static, in internal RAM, and holds 4-bit palette indices rather than RGB:

| Profile | Size |
|---|---|
| `oled128` | 8 KB |
| `tft240` | 28.1 KB |
| `tft320` | 37.5 KB |

Panels convert the indices when they flush: page bits for the OLEDs, RGB565
strips for the TFTs.

### Color TFT modules (future)

The modules found share one 12-pin header with the encoder built in:

```
GND VCC SCL SDA RES DC CS BLK K0 PUSH A B
```

`SCL` and `SDA` are SPI clock and MOSI, not I2C. A TFT replaces the OLED and
the four-button module, so its pins come from those. The pin map will be
fixed when a module is in hand (T26a). The control model doesn't change:

| Input | Role |
|---|---|
| Encoder turn | UP / DOWN |
| Encoder push | OK |
| K0 | BACK |

**Every screen must be fully usable with turn, push and BACK alone.**

## Known limit: QR codes on a small OLED

The SH1107 is 128 px tall, which gives the QR codes room:

| QR | Scale | Size |
|---|---|---|
| Version 3 (29 modules) | 4x | 116 px |
| Version 6 (41 modules) | 3x | 123 px |

Webcam readability is still measured on the bench in T27.
