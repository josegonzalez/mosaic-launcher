SCREENSAVER := ../../plugins/screensaver

$(BUILD)/test_screensaver: test_screensaver.c $(wildcard $(SCREENSAVER)/*.c $(SCREENSAVER)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_screensaver.c $(SCREENSAVER)/screensaver_model.c -o $@
