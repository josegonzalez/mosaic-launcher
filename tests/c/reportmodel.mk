$(BUILD)/test_reportmodel: test_reportmodel.c $(LAUNCHER)/reportmodel.c $(LAUNCHER)/reportmodel.h $(LAUNCHER)/pluginset.c $(LAUNCHER)/pluginset.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_reportmodel.c $(LAUNCHER)/reportmodel.c $(LAUNCHER)/pluginset.c -o $@
