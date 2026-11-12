GAMIFICATION := ../../plugins/gamification

$(BUILD)/test_gamification: test_gamification.c $(wildcard $(GAMIFICATION)/*.c $(GAMIFICATION)/*.h) $(SDK)/mosaic_plugin.h check.h words.h
	mkdir -p $(BUILD) $(BUILD)/gamification
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_DIR=\"$(BUILD)/gamification\" \
		-DGM_LAUNCH_FILE=\"$(BUILD)/gamification/launch\" test_gamification.c $(GAMIFICATION)/gamification_model.c -o $@
