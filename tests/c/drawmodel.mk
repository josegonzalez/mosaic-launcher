# SDL-free: only the SDK header's types.
$(BUILD)/test_drawmodel: test_drawmodel.c $(LAUNCHER)/drawmodel.c $(LAUNCHER)/drawmodel.h $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -I$(SDK) test_drawmodel.c $(LAUNCHER)/drawmodel.c -o $@
