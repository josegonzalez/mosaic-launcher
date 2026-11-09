FOCUS := ../../plugins/focus-mode

$(BUILD)/test_focus_mode: test_focus_mode.c $(wildcard $(FOCUS)/*.c $(FOCUS)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/focus-mode-card\" test_focus_mode.c $(FOCUS)/focus_model.c -o $@
