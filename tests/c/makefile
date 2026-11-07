###########################################################
# Host-compiled unit tests for launcher code that doesn't need a display.
# Built against the desktop platform headers; SDL headers are needed for
# defines.h, but nothing links SDL.
###########################################################

NEXTUI ?= $(abspath ../../vendor/NextUI/workspace)
BUILD ?= $(abspath ../../build/tests)
LAUNCHER := $(abspath ../../launcher)

CC ?= cc
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
CFLAGS += -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -I$(NEXTUI)/all/common \
	-I$(NEXTUI)/desktop/platform $(SDL_CFLAGS) -DUSE_SDL2 -DPLATFORM=\"desktop\"

TESTS = test_art

.PHONY: test clean

test: $(addprefix $(BUILD)/,$(TESTS))
	@for t in $^; do $$t || exit 1; done

$(BUILD)/test_art: test_art.c $(LAUNCHER)/art.c check.h
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) test_art.c $(LAUNCHER)/art.c -o $@

clean:
	rm -rf $(BUILD)
