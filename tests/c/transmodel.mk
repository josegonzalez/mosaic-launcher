# SDL-free: only the SDK header's types.
$(BUILD)/test_transmodel: test_transmodel.c $(LAUNCHER)/transmodel.c $(LAUNCHER)/transmodel.h $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -I$(SDK) test_transmodel.c $(LAUNCHER)/transmodel.c -o $@
