# Leek BTC Signer

A minimalist, stateless, air-gapped Bitcoin signing device for the ESP32-S3.
It uses a 128x64 OLED, an EC11 rotary encoder, and a camera for QR codes.

> **Status:** early development. Do not use with real funds.

## Inspiration

Leek BTC Signer is inspired by **[SeedSigner](https://github.com/SeedSigner/seedsigner)**.
SeedSigner's stateless design, menu structure, SeedQR format and PSBT review
flows are the model this project follows, rebuilt in C for a much smaller
device. Thank you to the SeedSigner contributors. This project is independent
and is not affiliated with or endorsed by SeedSigner.

The hardware platform comes from **LeekWallet**, an ESP32-S3 EVM hardware wallet
by the same author. LeekWallet's stateless "temporary seed" mode was itself
inspired by SeedSigner.

## What it does (target)

- Holds a seed only in RAM. Power off and it's gone.
- Loads a seed from a SeedQR, by word entry, or from dice rolls.
- Exports xpubs and signs PSBTs, both over animated QR (UR2). No USB data,
  Wi-Fi or Bluetooth.
- Optional, opt-in vault that keeps a seed on the device, at the user's own risk.

## Hardware

ESP32-S3-WROOM-1-N16R8 CAM board, OV5640 camera, 0.96" SSD1306 OLED, 4 buttons,
and an EC11 rotary encoder. See [docs/HARDWARE.md](docs/HARDWARE.md).

## Roadmap and testing

See [ROADMAP.md](ROADMAP.md) for the plan and [docs/TESTING.md](docs/TESTING.md)
for how each step is proven: on the host, under QEMU, then on the board.

## License

Apache-2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
