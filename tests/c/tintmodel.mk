$(BUILD)/test_tintmodel: test_tintmodel.c $(LAUNCHER)/tintmodel.c $(LAUNCHER)/tintmodel.h $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -I$(SDK) test_tintmodel.c $(LAUNCHER)/tintmodel.c -o $@
