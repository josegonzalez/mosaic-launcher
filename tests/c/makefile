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

TESTS = test_art test_library test_actions test_pluginset test_favorites test_pakhooks test_kbmodel

# Tests that touch the card get a fixture SD card under build/tests, via a
# platform.h shim that repoints SDCARD_PATH. The shim's directory comes first
# so its #include_next finds the desktop platform.h. No -Werror: these link
# stock code (utils.c, library.c) that has warnings of its own.
CARD_CFLAGS = -std=gnu99 -Wall -Iplatform -I. -I$(LAUNCHER) -I$(SDK) -I$(NEXTUI)/all/common \
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

ACTIONS_SOURCES = test_actions.c $(LAUNCHER)/actions.c $(LAUNCHER)/tr.c

$(BUILD)/test_actions: $(ACTIONS_SOURCES) $(wildcard $(LAUNCHER)/*.h) check.h
	mkdir -p $(BUILD)
	$(CC) $(CARD_CFLAGS) $(ACTIONS_SOURCES) -o $@

$(BUILD)/test_pluginset: test_pluginset.c $(LAUNCHER)/pluginset.c $(LAUNCHER)/pluginset.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_pluginset.c $(LAUNCHER)/pluginset.c -o $@

PAKHOOKS_SOURCES = test_pakhooks.c stubs.c $(LAUNCHER)/pakhooks.c $(LAUNCHER)/containers.c \
	$(LAUNCHER)/state.c $(NEXTUI)/all/common/utils.c

$(BUILD)/test_pakhooks: $(PAKHOOKS_SOURCES) $(wildcard $(LAUNCHER)/*.h) check.h platform/platform.h
	mkdir -p $(BUILD)
	$(CC) $(CARD_CFLAGS) $(PAKHOOKS_SOURCES) -lm -o $@

$(BUILD)/test_kbmodel: test_kbmodel.c $(LAUNCHER)/kbmodel.c $(LAUNCHER)/kbmodel.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_kbmodel.c $(LAUNCHER)/kbmodel.c -o $@

# Plugins are tested against a fake host, by including their source.
$(BUILD)/test_favorites: test_favorites.c ../../plugins/favorites/favorites.c $(SDK)/nextui_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/favorites-card\" test_favorites.c -o $@

clean:
	rm -rf $(BUILD)
