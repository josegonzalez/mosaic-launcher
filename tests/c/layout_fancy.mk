LAYOUT_FANCY := ../../plugins/layout-fancy

$(BUILD)/test_layout_fancy: test_layout_fancy.c $(wildcard $(LAYOUT_FANCY)/*.c $(LAYOUT_FANCY)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/layout-fancy-card\" test_layout_fancy.c $(LAYOUT_FANCY)/fancy_model.c -o $@
