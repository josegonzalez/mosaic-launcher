$(BUILD)/test_pluginorder: test_pluginorder.c $(LAUNCHER)/pluginorder.c $(LAUNCHER)/pluginorder.h $(LAUNCHER)/pluginset.c $(LAUNCHER)/pluginset.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_pluginorder.c $(LAUNCHER)/pluginorder.c $(LAUNCHER)/pluginset.c -o $@
