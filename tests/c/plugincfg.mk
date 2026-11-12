PLUGINCFG_SOURCES = test_plugincfg.c $(LAUNCHER)/plugincfg.c $(LAUNCHER)/pluginset.c $(LAUNCHER)/caller.c

$(BUILD)/test_plugincfg: $(PLUGINCFG_SOURCES) $(LAUNCHER)/plugincfg.h $(LAUNCHER)/pluginset.h $(LAUNCHER)/caller.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -DTEST_DIR=\"$(BUILD)\" $(PLUGINCFG_SOURCES) -pthread -o $@
