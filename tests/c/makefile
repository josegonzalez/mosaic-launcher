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

TESTS = test_art test_library

# Tests that touch the card get a fixture SD card under build/tests, via a
# platform.h shim that repoints SDCARD_PATH. The shim's directory comes first
# so its #include_next finds the desktop platform.h. No -Werror: these link
# stock code (utils.c, library.c) that has warnings of its own.
CARD_CFLAGS = -std=gnu99 -Wall -Iplatform -I. -I$(LAUNCHER) -I$(NEXTUI)/all/common \
	-I$(NEXTUI)/desktop/platform -I$(NEXTUI)/desktop/libmsettings $(SDL_CFLAGS) \
	-DUSE_SDL2 -DPLATFORM=\"desktop\" -DTEST_SDCARD_PATH=\"$(BUILD)/sdcard\"

.PHONY: test clean

test: $(addprefix $(BUILD)/,$(TESTS))
	@for t in $^; do $$t || exit 1; done

$(BUILD)/test_art: test_art.c $(LAUNCHER)/art.c check.h
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) test_art.c $(LAUNCHER)/art.c -o $@

LIBRARY_SOURCES = test_library.c stubs.c $(LAUNCHER)/library.c $(LAUNCHER)/containers.c \
	$(LAUNCHER)/state.c $(NEXTUI)/all/common/utils.c

$(BUILD)/test_library: $(LIBRARY_SOURCES) $(wildcard $(LAUNCHER)/*.h) check.h platform/platform.h
	mkdir -p $(BUILD)
	$(CC) $(CARD_CFLAGS) $(LIBRARY_SOURCES) -lm -o $@

clean:
	rm -rf $(BUILD)
