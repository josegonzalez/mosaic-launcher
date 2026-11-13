$(BUILD)/test_actionlist: test_actionlist.c $(LAUNCHER)/actionlist.c $(LAUNCHER)/actionlist.h $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -I$(SDK) test_actionlist.c $(LAUNCHER)/actionlist.c -o $@
