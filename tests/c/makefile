###########################################################
# Host-compiled unit tests for launcher code that doesn't need a display.
# Built against the desktop platform headers; SDL headers are needed for
# defines.h, but nothing links SDL.
###########################################################

NEXTUI ?= $(abspath ../../vendor/NextUI/workspace)
BUILD ?= $(abspath ../../build/tests)
LAUNCHER := $(abspath ../../launcher)
SDK := $(abspath ../../sdk)

CC ?= cc
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
CFLAGS += -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -I$(SDK) -I$(NEXTUI)/all/common \
	-I$(NEXTUI)/desktop/platform $(SDL_CFLAGS) -DUSE_SDL2 -DPLATFORM=\"desktop\"

# Every test_<name>.c is a test. One that needs more than its own source and
# the SDK header gets a <name>.mk with its rule; the rest use the rule below.
TESTS = $(sort $(basename $(wildcard test_*.c)))

# Tests that touch the card get a fixture SD card under build/tests, via a
# platform.h shim that repoints SDCARD_PATH. The shim's directory comes first
# so its #include_next finds the desktop platform.h. No -Werror: these link
# stock code (utils.c, library.c) that has warnings of its own.
CARD_CFLAGS = -std=gnu99 -Wall -Iplatform -I. -I$(LAUNCHER) -I$(SDK) -I$(NEXTUI)/all/common \
	-I$(NEXTUI)/desktop/platform -I$(NEXTUI)/desktop/libmsettings $(SDL_CFLAGS) \
	-DUSE_SDL2 -DPLATFORM=\"desktop\" -DTEST_SDCARD_PATH=\"$(BUILD)/sdcard\"

.PHONY: test clean

# Runs them one at a time (some share the fixture card), each one's
# milliseconds going to $(BUILD)/unit.tsv for scripts/test-times.sh slowest.
test: $(addprefix $(BUILD)/,$(TESTS))
	@. ../../scripts/timing.sh; : >$(BUILD)/unit.tsv; \
	for t in $^; do \
		started=$$(now_ms); $$t || exit 1; \
		printf '%s\t%d\n' "$${t##*/}" $$(($$(now_ms) - started)) >>$(BUILD)/unit.tsv; \
	done

# rcheevos' hashing and libchdr, built as the launcher builds them.
RCHEEVOS := $(abspath ../../vendor/rcheevos)
LIBCHDR := $(abspath ../../vendor/libchdr)
VENDOR_OBJ := $(BUILD)/vendor
include $(LAUNCHER)/vendor.mk
RA_CFLAGS = -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -I$(RCHEEVOS)/include -I$(LIBCHDR)/include

# rahash.c and what it hashes with, for test_rahash and test_raprogress.
RA_HASH_SOURCES = $(LAUNCHER)/rahash.c $(LAUNCHER)/rachd.c $(LAUNCHER)/ramodel.c

# A test that includes what it tests.
$(BUILD)/test_%: test_%.c $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) $< -o $@

include $(sort $(wildcard *.mk))

clean:
	rm -rf $(BUILD)
