$(BUILD)/test_resetmodel: test_resetmodel.c $(LAUNCHER)/resetmodel.c $(LAUNCHER)/resetmodel.h $(LAUNCHER)/pluginset.c $(LAUNCHER)/pluginset.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -DTEST_DIR=\"$(BUILD)\" test_resetmodel.c $(LAUNCHER)/resetmodel.c $(LAUNCHER)/pluginset.c -o $@
