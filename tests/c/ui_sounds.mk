UI_SOUNDS := ../../plugins/ui-sounds

$(BUILD)/test_ui_sounds: test_ui_sounds.c $(wildcard $(UI_SOUNDS)/*.c $(UI_SOUNDS)/*.h $(UI_SOUNDS)/res/sounds/*.wav) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -I$(UI_SOUNDS) -DUIS_TEST_RES=\"$(abspath $(UI_SOUNDS)/res)\" \
		-DUIS_TEST_TMP=\"$(BUILD)/ui-sounds\" test_ui_sounds.c $(UI_SOUNDS)/ui_sounds_model.c -o $@
