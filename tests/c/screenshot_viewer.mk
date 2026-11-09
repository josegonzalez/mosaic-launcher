SCREENSHOT_VIEWER := ../../plugins/screenshot-viewer

$(BUILD)/test_screenshot_viewer: test_screenshot_viewer.c $(wildcard $(SCREENSHOT_VIEWER)/*.c $(SCREENSHOT_VIEWER)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/screenshot-viewer-card\" test_screenshot_viewer.c $(SCREENSHOT_VIEWER)/shot_model.c -o $@
