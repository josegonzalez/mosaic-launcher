GAME_ACTIONS := ../../plugins/game-actions

$(BUILD)/test_game_actions: test_game_actions.c $(wildcard $(GAME_ACTIONS)/*.c $(GAME_ACTIONS)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/game-actions-card\" test_game_actions.c $(GAME_ACTIONS)/actions_model.c -o $@
