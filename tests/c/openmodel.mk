# SDL-free, and with no launcher headers but its own and the SDK's.
$(BUILD)/test_openmodel: test_openmodel.c $(LAUNCHER)/openmodel.c $(LAUNCHER)/openmodel.h $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -I$(SDK) test_openmodel.c $(LAUNCHER)/openmodel.c -o $@
