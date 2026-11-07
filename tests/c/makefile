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

TESTS = test_art test_library test_actions test_pluginset test_favorites test_pakhooks test_kbmodel test_cfgfile test_smart_collections test_hidden test_numeric_prefix test_focus_mode test_title_disambiguation test_launchcmd test_all_games test_random_game test_clear_recents test_power test_game_actions test_path_repair

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

LIBRARY_SOURCES = test_library.c stubs.c $(LAUNCHER)/library.c $(LAUNCHER)/listsort.c $(LAUNCHER)/containers.c \
	$(LAUNCHER)/state.c $(LAUNCHER)/quicktiles.c $(LAUNCHER)/actions.c $(LAUNCHER)/tr.c $(NEXTUI)/all/common/utils.c

$(BUILD)/test_library: $(LIBRARY_SOURCES) $(wildcard $(LAUNCHER)/*.h) check.h platform/platform.h
	mkdir -p $(BUILD)
	$(CC) $(CARD_CFLAGS) $(LIBRARY_SOURCES) -lm -o $@

POWER_SOURCES = test_power.c $(LAUNCHER)/power.c $(LAUNCHER)/powerwatch.c

$(BUILD)/test_power: $(POWER_SOURCES) $(wildcard $(LAUNCHER)/*.h) check.h
	mkdir -p $(BUILD)
	$(CC) $(CARD_CFLAGS) -Werror $(POWER_SOURCES) -o $@

ACTIONS_SOURCES = test_actions.c $(LAUNCHER)/actions.c $(LAUNCHER)/tr.c

$(BUILD)/test_actions: $(ACTIONS_SOURCES) $(wildcard $(LAUNCHER)/*.h) check.h
	mkdir -p $(BUILD)
	$(CC) $(CARD_CFLAGS) $(ACTIONS_SOURCES) -o $@

$(BUILD)/test_pluginset: test_pluginset.c $(LAUNCHER)/pluginset.c $(LAUNCHER)/pluginset.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_pluginset.c $(LAUNCHER)/pluginset.c -o $@

PAKHOOKS_SOURCES = test_pakhooks.c stubs.c $(LAUNCHER)/pakhooks.c $(LAUNCHER)/listsort.c $(LAUNCHER)/containers.c \
	$(LAUNCHER)/state.c $(NEXTUI)/all/common/utils.c

$(BUILD)/test_pakhooks: $(PAKHOOKS_SOURCES) $(wildcard $(LAUNCHER)/*.h) check.h platform/platform.h
	mkdir -p $(BUILD)
	$(CC) $(CARD_CFLAGS) $(PAKHOOKS_SOURCES) -lm -o $@

$(BUILD)/test_kbmodel: test_kbmodel.c $(LAUNCHER)/kbmodel.c $(LAUNCHER)/kbmodel.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_kbmodel.c $(LAUNCHER)/kbmodel.c -o $@

$(BUILD)/test_cfgfile: test_cfgfile.c $(LAUNCHER)/cfgfile.c $(LAUNCHER)/cfgfile.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -DTEST_DIR=\"$(BUILD)\" test_cfgfile.c $(LAUNCHER)/cfgfile.c -o $@

$(BUILD)/test_launchcmd: test_launchcmd.c $(LAUNCHER)/launchcmd.c $(LAUNCHER)/launchcmd.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -DTEST_DIR=\"$(BUILD)\" test_launchcmd.c $(LAUNCHER)/launchcmd.c -o $@

# Plugins are tested against a fake host, by including their source.
FAVORITES := ../../plugins/favorites

$(BUILD)/test_favorites: test_favorites.c $(wildcard $(FAVORITES)/*.c $(FAVORITES)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/favorites-card\" test_favorites.c $(FAVORITES)/pin_model.c -o $@

SMART := ../../plugins/smart-collections

$(BUILD)/test_smart_collections: test_smart_collections.c $(wildcard $(SMART)/*.c $(SMART)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/smart-collections-card\" test_smart_collections.c $(SMART)/smart_model.c -o $@

HIDDEN := ../../plugins/hidden

$(BUILD)/test_hidden: test_hidden.c $(HIDDEN)/hidden.c $(HIDDEN)/hiddenset.c $(HIDDEN)/hiddenset.h $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/hidden-card\" test_hidden.c $(HIDDEN)/hiddenset.c -o $@

NUMERIC := ../../plugins/numeric-prefix

$(BUILD)/test_numeric_prefix: test_numeric_prefix.c $(wildcard $(NUMERIC)/*.c $(NUMERIC)/*.h) ../../plugins/all-games/games_model.c ../../plugins/all-games/games_model.h $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_numeric_prefix.c $(NUMERIC)/numprefix.c ../../plugins/all-games/games_model.c -o $@

FOCUS := ../../plugins/focus-mode

$(BUILD)/test_focus_mode: test_focus_mode.c $(wildcard $(FOCUS)/*.c $(FOCUS)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/focus-mode-card\" test_focus_mode.c $(FOCUS)/focus_model.c -o $@

TITLES := ../../plugins/title-disambiguation

$(BUILD)/test_title_disambiguation: test_title_disambiguation.c $(wildcard $(TITLES)/*.c $(TITLES)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_title_disambiguation.c $(TITLES)/titles.c -o $@

ALL_GAMES := ../../plugins/all-games

$(BUILD)/test_all_games: test_all_games.c $(wildcard $(ALL_GAMES)/*.c $(ALL_GAMES)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/all-games-card\" test_all_games.c $(ALL_GAMES)/games_model.c -o $@

RANDOM := ../../plugins/random-game

$(BUILD)/test_random_game: test_random_game.c $(wildcard $(RANDOM)/*.c $(RANDOM)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/random-game-card\" test_random_game.c $(RANDOM)/random_model.c -o $@

CLEAR_RECENTS := ../../plugins/clear-recents

$(BUILD)/test_clear_recents: test_clear_recents.c $(wildcard $(CLEAR_RECENTS)/*.c) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_clear_recents.c -o $@

GAME_ACTIONS := ../../plugins/game-actions

$(BUILD)/test_game_actions: test_game_actions.c $(wildcard $(GAME_ACTIONS)/*.c $(GAME_ACTIONS)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/game-actions-card\" test_game_actions.c $(GAME_ACTIONS)/actions_model.c -o $@

PATH_REPAIR := ../../plugins/path-repair

$(BUILD)/test_path_repair: test_path_repair.c $(wildcard $(PATH_REPAIR)/*.c $(PATH_REPAIR)/*.h) $(SDK)/nextui_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/path-repair-card\" test_path_repair.c $(PATH_REPAIR)/repair_model.c -o $@

clean:
	rm -rf $(BUILD)
