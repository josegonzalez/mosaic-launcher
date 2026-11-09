ALL_GAMES := ../../plugins/all-games

$(BUILD)/test_all_games: test_all_games.c $(wildcard $(ALL_GAMES)/*.c $(ALL_GAMES)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/all-games-card\" test_all_games.c $(ALL_GAMES)/games_model.c -o $@
