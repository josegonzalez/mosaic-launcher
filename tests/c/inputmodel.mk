# SDL-free: only the SDK header's types, with layoutmodel.c's button maps.
$(BUILD)/test_inputmodel: test_inputmodel.c $(LAUNCHER)/inputmodel.c $(LAUNCHER)/inputmodel.h $(LAUNCHER)/layoutmodel.c \
		$(LAUNCHER)/layoutmodel.h $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -I$(SDK) test_inputmodel.c $(LAUNCHER)/inputmodel.c $(LAUNCHER)/layoutmodel.c -o $@
