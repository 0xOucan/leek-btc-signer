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
CC       ?= gcc
CFLAGS   := -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -O1 -g \
            -I../src/core -I. -DLBS_HOST=1
SAN      := -fsanitize=address,undefined -fno-omit-frame-pointer
VERSION  := $(shell git describe --always --dirty 2>/dev/null || echo dev)

# Suites that depend on the display profile run once per profile; the rest
# once.
PER_PROFILE := test_gfx test_ui
ONCE        := test_input test_encoder

B := build

.PHONY: test shots sim clean

test:
	@mkdir -p $(B)
	@set -e; for t in $(ONCE); do \
	  $(CC) $(CFLAGS) $(SAN) -DLBS_DISPLAY_OLED128 -o $(B)/$$t $$t.c $(CORE) && ./$(B)/$$t; \
	done
	@set -e; for p in $(PROFILES); do for t in $(PER_PROFILE); do \
	  mkdir -p $(B)/$$p; \
	  $(CC) $(CFLAGS) $(SAN) -DLBS_DISPLAY_$$p -o $(B)/$$p/$$t $$t.c $(CORE) && ./$(B)/$$p/$$t; \
	done; done

shots:
	@set -e; for p in $(PROFILES); do \
	  mkdir -p $(B)/$$p $(B)/shots; \
	  $(CC) $(CFLAGS) -DLBS_DISPLAY_$$p -o $(B)/$$p/shots shots.c shot.c $(CORE) && ./$(B)/$$p/shots; \
	done

sim:
	@mkdir -p $(B)/$(PROFILE)
	$(CC) $(CFLAGS) -DLBS_DISPLAY_$(PROFILE) -DLBS_SIM_VERSION='"$(VERSION)"' \
	  $(shell sdl2-config --cflags) -o $(B)/$(PROFILE)/leek-sim \
	  leek-sim.c shot.c $(CORE) $(shell sdl2-config --libs)
	@echo "run: sim/$(B)/$(PROFILE)/leek-sim"

clean:
	rm -rf $(B)
