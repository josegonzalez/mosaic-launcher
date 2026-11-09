THEME_PLUS := ../../plugins/theme-plus

$(BUILD)/test_theme_plus: test_theme_plus.c $(wildcard $(THEME_PLUS)/*.c $(THEME_PLUS)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/theme-plus-card\" test_theme_plus.c $(THEME_PLUS)/theme_plus_model.c -o $@
