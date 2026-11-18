BUG_REPORT := ../../plugins/bug-report

$(BUILD)/test_bug_report: test_bug_report.c $(wildcard $(BUG_REPORT)/*.c $(BUG_REPORT)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -I$(BUG_REPORT) test_bug_report.c -o $@
