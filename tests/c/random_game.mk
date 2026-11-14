RANDOM := ../../plugins/random-game

$(BUILD)/test_random_game: test_random_game.c $(wildcard $(RANDOM)/*.c $(RANDOM)/*.h) $(SDK)/mosaic_plugin.h check.h fakeproviders.h fakesets.h \
		fakeindex.h $(LAUNCHER)/indexmodel.c $(LAUNCHER)/indexmodel.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -I$(LAUNCHER) -DTEST_SDCARD_PATH=\"$(BUILD)/random-game-card\" test_random_game.c \
		$(RANDOM)/random_model.c $(LAUNCHER)/indexmodel.c $(LAUNCHER)/infomodel.c -o $@
