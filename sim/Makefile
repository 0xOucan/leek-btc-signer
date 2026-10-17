# Host-side tests, screenshots and the desktop simulator.
#
#   make -C sim test                 every suite, on every display profile
#   make -C sim shots                screenshots for every profile
#   make -C sim sim PROFILE=OLED128  the SDL simulator (needs SDL2)
#
# Profiles: OLED128 (the bench screen), OLED64, TFT240, TFT320, TFT160.

PROFILES := OLED128 OLED64 TFT240 TFT320 TFT160
PROFILE  ?= OLED128

CORE     := $(wildcard ../src/core/*.c) ../components/tinf/tinflate.c
QUIRC_SRC := $(wildcard ../components/quirc/lib/*.c)
QUIRC    := build/quirc.a

# libwally-core + secp256k1, compiled once per build directory with the
# same definitions as the ESP-IDF component.
W        := ../components/libwally-core
include $(W)/config/wally-defs.mk
WALLY_INC := -I$(W)/upstream/include
WALLY_PRIV := -I$(W)/config -I$(W)/upstream -I$(W)/upstream/src \
              -I$(W)/upstream/src/ccan -I$(W)/upstream/src/secp256k1 \
              -I$(W)/upstream/src/secp256k1/include
WALLY_OBJ := build/wally.o
CC       ?= gcc
CFLAGS   := -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O1 -g \
            -I../src/core -I. -DLBS_HOST=1 \
            -I../components/tinf -I../components/quirc/lib \
            $(WALLY_INC) -DWALLY_ABI_NO_ELEMENTS=1
SAN      := -fsanitize=address,undefined -fno-omit-frame-pointer
VERSION  := $(shell git describe --always --dirty 2>/dev/null || echo dev)

# Suites that depend on the display profile run once per profile; the rest
# once.
PER_PROFILE := test_gfx test_ui test_qr_view test_screens test_psbt_flows test_tools_flows \
               test_seed_flows test_settings_flows
ONCE        := test_input test_encoder test_wally test_btc test_psbt \
               test_cbor test_ur test_ur_fountain test_ur_decoder test_ur_encoder \
               test_qrcode test_qr_decode test_qr_encode test_scan test_widgets \
               test_text_entry test_mnemonic_entry test_settings

B := build

.PHONY: test shots sim clean

# Upstream code: its own warnings are not errors here either.
$(WALLY_OBJ): $(W)/upstream/src/amalgamation/combined.c $(W)/config/config.h $(W)/config/wally-defs.mk
	@mkdir -p $(B)
	$(CC) -std=c11 -D_DEFAULT_SOURCE -O2 -g -c $(WALLY_DEFS) $(WALLY_PRIV) $< -o $@

# quirc, the QR reader the host tests read our QR codes back with.
$(QUIRC): $(QUIRC_SRC)
	@mkdir -p $(B)/quirc
	@set -e; for f in $(QUIRC_SRC); do \
	  $(CC) -std=c11 -O2 -g -c -I../components/quirc/lib $$f -o $(B)/quirc/$$(basename $$f .c).o; \
	done
	ar rcs $@ $(B)/quirc/*.o

test: $(WALLY_OBJ) $(QUIRC)
	@mkdir -p $(B)
	@set -e; for t in $(ONCE); do \
	  $(CC) $(CFLAGS) $(SAN) -DLBS_DISPLAY_OLED128 -o $(B)/$$t $$t.c $(CORE) $(QUIRC) $(WALLY_OBJ) -lm && ./$(B)/$$t; \
	done
	@set -e; for p in $(PROFILES); do for t in $(PER_PROFILE); do \
	  mkdir -p $(B)/$$p; \
	  $(CC) $(CFLAGS) $(SAN) -DLBS_DISPLAY_$$p -o $(B)/$$p/$$t $$t.c $(CORE) $(QUIRC) $(WALLY_OBJ) -lm && ./$(B)/$$p/$$t; \
	done; done

shots: $(WALLY_OBJ)
	@set -e; for p in $(PROFILES); do \
	  mkdir -p $(B)/$$p $(B)/shots; \
	  $(CC) $(CFLAGS) -DLBS_DISPLAY_$$p -o $(B)/$$p/shots shots.c shot.c $(CORE) $(WALLY_OBJ) && ./$(B)/$$p/shots; \
	done

sim: $(WALLY_OBJ)
	@mkdir -p $(B)/$(PROFILE)
	$(CC) $(CFLAGS) -DLBS_DISPLAY_$(PROFILE) -DLBS_SIM_VERSION='"$(VERSION)"' \
	  $(shell sdl2-config --cflags) -o $(B)/$(PROFILE)/leek-sim \
	  leek-sim.c shot.c $(CORE) $(WALLY_OBJ) $(shell sdl2-config --libs)
	@echo "run: sim/$(B)/$(PROFILE)/leek-sim"

clean:
	rm -rf $(B)
