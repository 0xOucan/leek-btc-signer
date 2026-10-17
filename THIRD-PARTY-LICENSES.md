# Third-party software

Everything Leek BTC Signer bundles or derives from, with its license. All are
permissive and compatible with this project's Apache-2.0 license. The full
license texts are kept next to each component's source.

| Component | Where | License | Copyright / source |
|---|---|---|---|
| libwally-core 1.5.6 | `components/libwally-core/upstream/` | MIT (BSD-MIT) | Jon Griffiths and contributors; `upstream/LICENSE`. Pinned commit in `upstream/VENDORED` |
| secp256k1-zkp | `components/libwally-core/upstream/src/secp256k1/` | MIT | Pieter Wuille and contributors; `COPYING` |
| ccan modules (sha256, sha512, ripemd160, base64) | `components/libwally-core/upstream/src/ccan/` | BSD-MIT | Rusty Russell and contributors; `ccan/licenses/BSD-MIT` |
| ccan modules (build_assert, compiler, endian, str/hex) | same | CC0 | public domain dedication; `ccan/licenses/CC0` |
| ctaes | `components/libwally-core/upstream/src/ctaes/` | MIT | Copyright (c) 2016 Pieter Wuille; `COPYING` |
| scrypt | `components/libwally-core/upstream/src/scrypt/` | BSD-2-Clause | Copyright 2009 Colin Percival; header of `crypto_scrypt.c` |
| BIP-39 English wordlist | `src/core/bip39-words.c` | from libwally-core (MIT); the list itself is in BIP-39 | |
| SeedSigner | behaviour, flows, menu labels, colour palette; generated test vectors in `sim/vectors/` | MIT | Copyright (c) 2021 SeedSigner; https://github.com/SeedSigner/seedsigner |
| LeekWallet | 5x7 font, I2C OLED driver, button handling, QEMU and harness practices; CBOR, UR, fountain and QR code with their tests | Apache-2.0 | Copyright 2026 0xoucan, the same author |
| LeekWallet Zcash firmware | EC11 decoder and its test, SH1107 init | Apache-2.0 | Copyright 2026 0xoucan, the same author |
| quirc | `components/quirc/` | ISC | Copyright (c) 2010-2012 Daniel Beer; `LICENSE`. Upstream https://github.com/dlbeer/quirc at `927d680904dc95fdff4cd9d022eb374b438ff8f2`, via LeekWallet |
| tinf 1.2.1 | `components/tinf/` | Zlib | Copyright (c) 2003-2019 Joergen Ibsen; `LICENSE`. Upstream https://github.com/jibsen/tinf at `9929246b87f0946b124cfa2a89894b8943b0b072`; `tinflate.c` and `tinf.h` only |
| QRCode | `src/core/qrcode.c` | MIT | Copyright (c) 2017 Richard Moore, Project Nayuki; via LeekWallet |
| BC-UR Bytewords list and fountain algorithm | `src/core/ur.c`, `src/core/ur-fountain.c` | BSD-2-Clause Plus Patent | Blockchain Commons; implementation via LeekWallet |
| SeedSigner's ur2 decoder | algorithm of `src/core/ur-decoder.c` | BSD-2-Clause Plus Patent | Foundation Devices (foundation-ur-py), as bundled in SeedSigner |
| ESP-IDF | build framework, drivers (not vendored; fetched by PlatformIO, pinned) | Apache-2.0 | Espressif Systems |
