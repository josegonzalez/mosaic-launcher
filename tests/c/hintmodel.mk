$(BUILD)/test_hintmodel: test_hintmodel.c $(LAUNCHER)/hintmodel.c $(LAUNCHER)/hintmodel.h $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -I$(SDK) test_hintmodel.c $(LAUNCHER)/hintmodel.c -o $@
