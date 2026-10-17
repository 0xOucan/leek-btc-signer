# Host-side tests, screenshots and the desktop simulator.
#
#   make -C sim test                 every suite, on every display profile
#   make -C sim shots                screenshots for every profile
#   make -C sim sim PROFILE=OLED128  the SDL simulator (needs SDL2)
#
# Profiles: OLED128 (the bench screen), OLED64, TFT240, TFT320, TFT160.

PROFILES := OLED128 OLED64 TFT240 TFT320 TFT160
PROFILE  ?= OLED128

CORE     := $(wildcard ../src/core/*.c)

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
            $(WALLY_INC) -DWALLY_ABI_NO_ELEMENTS=1
SAN      := -fsanitize=address,undefined -fno-omit-frame-pointer
VERSION  := $(shell git describe --always --dirty 2>/dev/null || echo dev)

# Suites that depend on the display profile run once per profile; the rest
# once.
PER_PROFILE := test_gfx test_ui
ONCE        := test_input test_encoder test_wally test_btc test_psbt

B := build

.PHONY: test shots sim clean

# Upstream code: its own warnings are not errors here either.
$(WALLY_OBJ): $(W)/upstream/src/amalgamation/combined.c $(W)/config/config.h $(W)/config/wally-defs.mk
	@mkdir -p $(B)
	$(CC) -std=c11 -D_DEFAULT_SOURCE -O2 -g -c $(WALLY_DEFS) $(WALLY_PRIV) $< -o $@

test: $(WALLY_OBJ)
	@mkdir -p $(B)
	@set -e; for t in $(ONCE); do \
	  $(CC) $(CFLAGS) $(SAN) -DLBS_DISPLAY_OLED128 -o $(B)/$$t $$t.c $(CORE) $(WALLY_OBJ) && ./$(B)/$$t; \
	done
	@set -e; for p in $(PROFILES); do for t in $(PER_PROFILE); do \
	  mkdir -p $(B)/$$p; \
	  $(CC) $(CFLAGS) $(SAN) -DLBS_DISPLAY_$$p -o $(B)/$$p/$$t $$t.c $(CORE) $(WALLY_OBJ) && ./$(B)/$$p/$$t; \
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
