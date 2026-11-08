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

TESTS = test_art test_library test_actions test_pluginset test_favorites test_pakhooks test_kbmodel test_cfgfile test_smart_collections test_hidden test_numeric_prefix test_focus_mode test_title_disambiguation test_launchcmd test_all_games test_random_game test_clear_recents test_power test_game_actions test_path_repair test_simple_mode test_stock_toggles test_platform_jump test_button_remap test_nav test_quick_menu_plus test_emulator_override test_netplay test_power_plus test_battery_logger test_leds test_genre_view test_core_prefetch test_collate_subfolders test_led_status test_bluetooth test_bt_reconnect test_drawmodel test_layoutmodel test_artcache test_selmodel test_statusmodel test_infomodel test_transmodel test_art_boxart test_layout_grid test_art_placeholder test_art_backdrop test_header_indicators test_row_badges test_layout_carousel test_playmodel test_playhistory test_layout_hero_rail test_layout_shelf test_layout_fancy test_playtime_captions test_metadata_band test_playtime_limit test_collections_plus

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

# nav.c's folder stack over the listing code, on a card of its own.
NAV_SOURCES = test_nav.c stubs.c $(LAUNCHER)/nav.c $(LAUNCHER)/library.c $(LAUNCHER)/listsort.c $(LAUNCHER)/containers.c \
	$(LAUNCHER)/state.c $(LAUNCHER)/quicktiles.c $(LAUNCHER)/actions.c $(LAUNCHER)/tr.c $(NEXTUI)/all/common/utils.c

$(BUILD)/test_nav: $(NAV_SOURCES) $(wildcard $(LAUNCHER)/*.h) check.h platform/platform.h
	mkdir -p $(BUILD)
	$(CC) $(filter-out -DTEST_SDCARD_PATH=%,$(CARD_CFLAGS)) -DTEST_SDCARD_PATH=\"$(BUILD)/nav-card\" $(NAV_SOURCES) -lm -o $@

POWER_SOURCES =test_power.c $(LAUNCHER)/power.c $(LAUNCHER)/powerwatch.c

$(BUILD)/test_power: $(POWER_SOURCES) $(wildcard $(LAUNCHER)/*.h) check.h
	mkdir -p $(BUILD)
	$(CC) $(CARD_CFLAGS) -Werror $(POWER_SOURCES) -o $@

ACTIONS_SOURCES = test_actions.c $(LAUNCHER)/actions.c $(LAUNCHER)/tr.c

$(BUILD)/test_actions: $(ACTIONS_SOURCES) $(wildcard $(LAUNCHER)/*.h) check.h
	mkdir -p $(BUILD)
	$(CC) $(CARD_CFLAGS) $(ACTIONS_SOURCES) -o $@

# Against stock's config.c, on a card of its own since it writes minuisettings.txt.
# Unfortified: reading a saved font style, stock copies the font name onto
# itself, which macOS's checked strncpy stops.
STOCK_TOGGLES_SOURCES = test_stock_toggles.c $(LAUNCHER)/stocktoggles.c $(NEXTUI)/all/common/config.c $(NEXTUI)/all/common/utils.c

$(BUILD)/test_stock_toggles: $(STOCK_TOGGLES_SOURCES) $(LAUNCHER)/stocktoggles.h $(SDK)/mosaic_plugin.h check.h platform/platform.h
	mkdir -p $(BUILD)
	$(CC) $(filter-out -DTEST_SDCARD_PATH=%,$(CARD_CFLAGS)) -DTEST_SDCARD_PATH=\"$(BUILD)/stock-toggles-card\" -D_FORTIFY_SOURCE=0 \
		$(STOCK_TOGGLES_SOURCES) -lm -o $@

# Against a fake of stock's LED profiles and override stack.
$(BUILD)/test_leds: test_leds.c $(LAUNCHER)/leds.c $(LAUNCHER)/leds.h check.h
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) test_leds.c $(LAUNCHER)/leds.c -o $@

# Against a fake hcitool and bluetoothctl it writes into its own folder.
$(BUILD)/test_bluetooth: test_bluetooth.c $(LAUNCHER)/bluetooth.c $(LAUNCHER)/bluetooth.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -DTEST_DIR=\"$(BUILD)/bluetooth\" test_bluetooth.c $(LAUNCHER)/bluetooth.c -lpthread -o $@

# SDL-free: only the SDK header's types.
$(BUILD)/test_drawmodel: test_drawmodel.c $(LAUNCHER)/drawmodel.c $(LAUNCHER)/drawmodel.h $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -I$(SDK) test_drawmodel.c $(LAUNCHER)/drawmodel.c -o $@

# SDL-free: only the SDK header's types.
$(BUILD)/test_layoutmodel: test_layoutmodel.c $(LAUNCHER)/layoutmodel.c $(LAUNCHER)/layoutmodel.h $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -I$(SDK) test_layoutmodel.c $(LAUNCHER)/layoutmodel.c -o $@

# SDL-free: only the SDK header's types.
$(BUILD)/test_transmodel: test_transmodel.c $(LAUNCHER)/transmodel.c $(LAUNCHER)/transmodel.h $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -I$(SDK) test_transmodel.c $(LAUNCHER)/transmodel.c -o $@

# SDL-free, and with no launcher headers but its own.
$(BUILD)/test_artcache: test_artcache.c $(LAUNCHER)/artcache.c $(LAUNCHER)/artcache.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_artcache.c $(LAUNCHER)/artcache.c -o $@

# SDL-free, and with no launcher headers but its own.
$(BUILD)/test_selmodel: test_selmodel.c $(LAUNCHER)/selmodel.c $(LAUNCHER)/selmodel.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_selmodel.c $(LAUNCHER)/selmodel.c -o $@

# SDL-free, and with no launcher headers but its own.
$(BUILD)/test_statusmodel: test_statusmodel.c $(LAUNCHER)/statusmodel.c $(LAUNCHER)/statusmodel.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_statusmodel.c $(LAUNCHER)/statusmodel.c -o $@

# SDL-free, and with no launcher headers but its own.
$(BUILD)/test_infomodel: test_infomodel.c $(LAUNCHER)/infomodel.c $(LAUNCHER)/infomodel.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_infomodel.c $(LAUNCHER)/infomodel.c -o $@

# SDL-free, and with no launcher headers but its own.
$(BUILD)/test_playmodel: test_playmodel.c $(LAUNCHER)/playmodel.c $(LAUNCHER)/playmodel.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_playmodel.c $(LAUNCHER)/playmodel.c -o $@

# Against a fixture game_logs.sqlite it makes in its own folder, through the
# system's sqlite library, which both it and the reader load with dlopen.
$(BUILD)/test_playhistory: test_playhistory.c $(LAUNCHER)/playhistory.c $(LAUNCHER)/playmodel.c \
		$(LAUNCHER)/playhistory.h $(LAUNCHER)/playmodel.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -DTEST_DIR=\"$(BUILD)/play-history\" \
		test_playhistory.c $(LAUNCHER)/playhistory.c $(LAUNCHER)/playmodel.c -ldl -o $@

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

$(BUILD)/test_path_repair: test_path_repair.c $(wildcard $(PATH_REPAIR)/*.c $(PATH_REPAIR)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/path-repair-card\" test_path_repair.c $(PATH_REPAIR)/repair_model.c -o $@

SIMPLE := ../../plugins/simple-mode

$(BUILD)/test_simple_mode: test_simple_mode.c $(wildcard $(SIMPLE)/*.c $(SIMPLE)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_simple_mode.c $(SIMPLE)/simple_model.c -o $@

PLATFORM_JUMP := ../../plugins/platform-jump

$(BUILD)/test_platform_jump: test_platform_jump.c $(wildcard $(PLATFORM_JUMP)/*.c $(PLATFORM_JUMP)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_platform_jump.c $(PLATFORM_JUMP)/jump_model.c -o $@

REMAP := ../../plugins/button-remap

$(BUILD)/test_button_remap: test_button_remap.c $(wildcard $(REMAP)/*.c $(REMAP)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_button_remap.c $(REMAP)/remap_model.c -o $@

QUICK_MENU_PLUS := ../../plugins/quick-menu-plus

$(BUILD)/test_quick_menu_plus: test_quick_menu_plus.c $(wildcard $(QUICK_MENU_PLUS)/*.c $(QUICK_MENU_PLUS)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_DIR=\"$(BUILD)\" -DQMP_AWAKE_FILE=\"$(BUILD)/qmp-awake\" test_quick_menu_plus.c $(QUICK_MENU_PLUS)/quick_model.c -o $@

# With the launcher's launchcmd.c behind its fake replace_launch, to check
# the command line the replaced launch gets.
OVERRIDE := ../../plugins/emulator-override

$(BUILD)/test_emulator_override: test_emulator_override.c $(wildcard $(OVERRIDE)/*.c $(OVERRIDE)/*.h) $(LAUNCHER)/launchcmd.c $(LAUNCHER)/launchcmd.h $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -I$(LAUNCHER) -DTEST_SDCARD_PATH=\"$(BUILD)/emulator-override-card\" test_emulator_override.c $(OVERRIDE)/override_model.c $(LAUNCHER)/launchcmd.c -o $@

NETPLAY := ../../plugins/netplay

$(BUILD)/test_netplay: test_netplay.c $(wildcard $(NETPLAY)/*.c $(NETPLAY)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/netplay-card\" test_netplay.c $(NETPLAY)/netplay_model.c -o $@

# With the wake alarm and stay-awake files repointed into the build folder.
POWER_PLUS := ../../plugins/power-plus

$(BUILD)/test_power_plus: test_power_plus.c $(wildcard $(POWER_PLUS)/*.c $(POWER_PLUS)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DPP_WAKEALARM_PATH=\"$(BUILD)/pp-wakealarm\" \
		-DPP_STAY_AWAKE_PATH=\"$(BUILD)/pp-stay-awake\" -DPP_STAY_ALIVE_PATH=\"$(BUILD)/pp-stay-alive\" \
		test_power_plus.c $(POWER_PLUS)/power_model.c -o $@

BATTERY := ../../plugins/battery-logger

$(BUILD)/test_battery_logger: test_battery_logger.c $(wildcard $(BATTERY)/*.c $(BATTERY)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_DIR=\"$(BUILD)/battery-logger\" test_battery_logger.c $(BATTERY)/battery_model.c -o $@

GENRE := ../../plugins/genre-view

$(BUILD)/test_genre_view: test_genre_view.c $(wildcard $(GENRE)/*.c $(GENRE)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/genre-view-card\" test_genre_view.c $(GENRE)/genre_model.c -o $@

# On a fixture card of its own, with a fake posix_fadvise and clock.
PREFETCH := ../../plugins/core-prefetch

$(BUILD)/test_core_prefetch: test_core_prefetch.c $(wildcard $(PREFETCH)/*.c $(PREFETCH)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_DIR=\"$(BUILD)/core-prefetch\" test_core_prefetch.c $(PREFETCH)/prefetch_model.c -o $@

# With genre-view's model, which the plugin builds in too.
COLLATE := ../../plugins/collate-subfolders

$(BUILD)/test_collate_subfolders: test_collate_subfolders.c $(wildcard $(COLLATE)/*.c $(COLLATE)/*.h $(GENRE)/genre_model.*) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/collate-subfolders-card\" test_collate_subfolders.c $(COLLATE)/collate_model.c $(GENRE)/genre_model.c -o $@

LED_STATUS := ../../plugins/led-status

$(BUILD)/test_led_status: test_led_status.c $(wildcard $(LED_STATUS)/*.c $(LED_STATUS)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_led_status.c $(LED_STATUS)/led_model.c -o $@

BT_RECONNECT := ../../plugins/bt-reconnect

$(BUILD)/test_bt_reconnect: test_bt_reconnect.c $(wildcard $(BT_RECONNECT)/*.c $(BT_RECONNECT)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_bt_reconnect.c $(BT_RECONNECT)/bt_model.c -o $@

BOXART := ../../plugins/art-boxart

$(BUILD)/test_art_boxart: test_art_boxart.c $(wildcard $(BOXART)/*.c $(BOXART)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/art-boxart-card\" test_art_boxart.c $(BOXART)/boxart_model.c -o $@

LAYOUT_GRID := ../../plugins/layout-grid

$(BUILD)/test_layout_grid: test_layout_grid.c $(wildcard $(LAYOUT_GRID)/*.c $(LAYOUT_GRID)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_layout_grid.c $(LAYOUT_GRID)/grid_model.c -o $@

PLACEHOLDER := ../../plugins/art-placeholder

$(BUILD)/test_art_placeholder: test_art_placeholder.c $(wildcard $(PLACEHOLDER)/*.c $(PLACEHOLDER)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_art_placeholder.c $(PLACEHOLDER)/placeholder_model.c -o $@

BACKDROP := ../../plugins/art-backdrop

$(BUILD)/test_art_backdrop: test_art_backdrop.c $(wildcard $(BACKDROP)/*.c $(BACKDROP)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_art_backdrop.c $(BACKDROP)/backdrop_model.c -o $@

# statvfs on the build folder stands in for the card.
HEADER_INDICATORS := ../../plugins/header-indicators

$(BUILD)/test_header_indicators: test_header_indicators.c $(wildcard $(HEADER_INDICATORS)/*.c $(HEADER_INDICATORS)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_DIR=\"$(BUILD)\" test_header_indicators.c $(HEADER_INDICATORS)/indicators_model.c -o $@

ROW_BADGES := ../../plugins/row-badges

$(BUILD)/test_row_badges: test_row_badges.c $(wildcard $(ROW_BADGES)/*.c $(ROW_BADGES)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/row-badges-card\" test_row_badges.c $(ROW_BADGES)/badges_model.c -o $@

LAYOUT_CAROUSEL := ../../plugins/layout-carousel

$(BUILD)/test_layout_carousel: test_layout_carousel.c $(wildcard $(LAYOUT_CAROUSEL)/*.c $(LAYOUT_CAROUSEL)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_layout_carousel.c $(LAYOUT_CAROUSEL)/carousel_model.c -o $@

HERO_RAIL := ../../plugins/layout-hero-rail

$(BUILD)/test_layout_hero_rail: test_layout_hero_rail.c $(wildcard $(HERO_RAIL)/*.c $(HERO_RAIL)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_layout_hero_rail.c $(HERO_RAIL)/hero_model.c -o $@

LAYOUT_SHELF := ../../plugins/layout-shelf

$(BUILD)/test_layout_shelf: test_layout_shelf.c $(wildcard $(LAYOUT_SHELF)/*.c $(LAYOUT_SHELF)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_layout_shelf.c $(LAYOUT_SHELF)/shelf_model.c -o $@

LAYOUT_FANCY := ../../plugins/layout-fancy

$(BUILD)/test_layout_fancy: test_layout_fancy.c $(wildcard $(LAYOUT_FANCY)/*.c $(LAYOUT_FANCY)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/layout-fancy-card\" test_layout_fancy.c $(LAYOUT_FANCY)/fancy_model.c -o $@

PLAYTIME_CAPTIONS := ../../plugins/playtime-captions

$(BUILD)/test_playtime_captions: test_playtime_captions.c $(wildcard $(PLAYTIME_CAPTIONS)/*.c $(PLAYTIME_CAPTIONS)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_playtime_captions.c $(PLAYTIME_CAPTIONS)/captions_model.c -o $@

METADATA_BAND := ../../plugins/metadata-band

$(BUILD)/test_metadata_band: test_metadata_band.c $(wildcard $(METADATA_BAND)/*.c $(METADATA_BAND)/*.h) check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. test_metadata_band.c $(METADATA_BAND)/band_model.c -o $@

PLAYTIME_LIMIT := ../../plugins/playtime-limit

$(BUILD)/test_playtime_limit: test_playtime_limit.c $(wildcard $(PLAYTIME_LIMIT)/*.c $(PLAYTIME_LIMIT)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_playtime_limit.c $(PLAYTIME_LIMIT)/limit_model.c -o $@

COLLECTIONS_PLUS := ../../plugins/collections-plus

$(BUILD)/test_collections_plus: test_collections_plus.c $(wildcard $(COLLECTIONS_PLUS)/*.c $(COLLECTIONS_PLUS)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/collections-plus-card\" test_collections_plus.c $(COLLECTIONS_PLUS)/collections_model.c -o $@

clean:
	rm -rf $(BUILD)
