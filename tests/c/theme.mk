# SDL-free: the theme slot's checks, sizes, contrast and card paths.
$(BUILD)/test_theme: test_theme.c $(LAUNCHER)/thememodel.c $(LAUNCHER)/thememodel.h $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -I$(SDK) test_theme.c $(LAUNCHER)/thememodel.c -lm -o $@
