ROW_BADGES := ../../plugins/row-badges

$(BUILD)/test_row_badges: test_row_badges.c $(wildcard $(ROW_BADGES)/*.c $(ROW_BADGES)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/row-badges-card\" test_row_badges.c $(ROW_BADGES)/badges_model.c -o $@
