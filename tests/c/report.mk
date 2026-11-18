REPORT_SOURCES = test_report.c $(LAUNCHER)/report.c $(LAUNCHER)/reportmodel.c $(LAUNCHER)/pluginset.c $(LAUNCHER)/plugincfg.c

$(BUILD)/test_report: $(REPORT_SOURCES) $(LAUNCHER)/report.h $(LAUNCHER)/reportmodel.h $(LAUNCHER)/pluginset.h $(LAUNCHER)/plugincfg.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -I$(SDK) -DTEST_DIR=\"$(BUILD)\" $(REPORT_SOURCES) -o $@
