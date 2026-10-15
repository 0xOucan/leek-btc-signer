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

## Display profiles

There is one codebase and **no long-lived branch per display**. The display is a
build-time profile, chosen together with the board:

| Profile | Panel | Resolution | Input | Status |
|---|---|---|---|---|
| `oled` | SSD1306 0.96", I2C | 128x64, 1-bit | EC11 + K1–K4 | The board on the bench |
| `tft240` | ST7789, SPI | 240x240 RGB | EC11 + K0 (module) | **Preferred color target**, same resolution as SeedSigner |
| `tft320` | ST7789, SPI | 320x240 RGB | EC11 + K0 (module) | Also supported by SeedSigner |
| `tft160` | ST7735S, SPI | 128x160 RGB | EC11 + K0 (module) | Lowest priority |

Screens describe content (a title, a list, a warning, a QR code). Each profile
has its own layout that decides how to draw it. Colors are **roles**: `fg`,
`bg`, `accent`, `ok`, `warning`, `danger`, `muted`. On the OLED they map to
black and white, with warnings drawn inverted. On the TFTs they map to a palette
close to SeedSigner's.

### Framebuffer and secrets

Seed words are drawn into the framebuffer, so the framebuffer is secret and
stays in internal RAM. It stores palette indices, not RGB565:

| Profile | Format | Size |
|---|---|---|
| `oled` | 1 bpp | 1 KB |
| `tft240` | 4 bpp indexed | 28.1 KB |
| `tft320` | 4 bpp indexed | 37.5 KB |
| `tft160` | 4 bpp indexed | 10 KB |

The flush converts to RGB565 one strip at a time through a small DMA buffer.
Camera preview frames are not secret, so they go straight from PSRAM to the
panel and bypass the indexed framebuffer.

### Color TFT modules

The three modules found share one 12-pin header with the encoder built in:

```
GND VCC SCL SDA RES DC CS BLK K0 PUSH A B
```

`SCL`/`SDA` here are SPI clock and data (MOSI), not I2C. The 240x240 module is
sold for ESP8266 projects, but only the panel is used; no ESP8266 is involved.

### TFT pin map (second S3 CAM board, without K1–K4)

The camera owns GPIO 4–18 except 14. The TFT profile reuses the pins that K1–K4
and the OLED use on the `oled` board. A board with the four buttons wired can't
also carry a TFT.

| Module pin | GPIO | Note |
|---|---|---|
| SCL (SPI SCK) | 47 | SPI2 through the GPIO matrix; writes at 40 MHz are fine |
| SDA (SPI MOSI) | 21 | |
| DC | 14 | |
| CS | 1 | Can be tied to GND if it's the only SPI device |
| RES | 41 | Or tie it high and use the panel's SWRESET command |
| BLK | 2 | Shares the on-board LED_ON, which then lights with the backlight; or tie to 3V3 |
| K0 | 42 | BACK |
| PUSH | 38 | Same as the `oled` profile |
| A | 39 | Same as the `oled` profile |
| B | 40 | Same as the `oled` profile |

**Unverified until the module is in hand:**
- the header order;
- whether K0/PUSH/A/B have pull-ups on the module;
- whether the panel needs a column/row offset. ST7789 240x240 modules usually
  need a row offset of 80; 128x160 ST7735S modules come in "tab" variants with
  different offsets.

### Controls on the TFT profile

| Input | Role |
|---|---|
| Encoder turn | Up / down |
| Encoder push | Select / confirm |
| K0 | Back |
| K0 held 3 s | Clear the seed and lock |

**Every screen must be fully usable with turn, push and BACK alone.** K1/K2 page
keys on the `oled` board are shortcuts, never the only way to do something.

## Known limit: QR export on 0.96"

At 0.96", one QR module at 2x scale is about 0.34 mm. Phones read it reliably;
laptop webcams (e.g. Sparrow on desktop) may not. Phase 2 measures this. The
fallback is a 2.42" SSD1309, which has the same 128x64 resolution and the same
driver family, so no UI changes are needed.
