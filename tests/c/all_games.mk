ALL_GAMES := ../../plugins/all-games

$(BUILD)/test_all_games: test_all_games.c $(wildcard $(ALL_GAMES)/*.c $(ALL_GAMES)/*.h) $(SDK)/mosaic_plugin.h check.h fakeindex.h \
		$(LAUNCHER)/indexmodel.c $(LAUNCHER)/indexmodel.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -I$(LAUNCHER) -DTEST_SDCARD_PATH=\"$(BUILD)/all-games-card\" test_all_games.c \
		$(ALL_GAMES)/games_model.c $(LAUNCHER)/indexmodel.c $(LAUNCHER)/infomodel.c -o $@
