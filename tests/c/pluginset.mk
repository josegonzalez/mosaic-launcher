$(BUILD)/test_pluginset: test_pluginset.c $(LAUNCHER)/pluginset.c $(LAUNCHER)/pluginset.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_pluginset.c $(LAUNCHER)/pluginset.c -o $@
