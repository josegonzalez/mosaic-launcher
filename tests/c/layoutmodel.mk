# SDL-free: only the SDK header's types.
$(BUILD)/test_layoutmodel: test_layoutmodel.c $(LAUNCHER)/layoutmodel.c $(LAUNCHER)/layoutmodel.h $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -I$(SDK) test_layoutmodel.c $(LAUNCHER)/layoutmodel.c -o $@
