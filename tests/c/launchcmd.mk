$(BUILD)/test_launchcmd: test_launchcmd.c $(LAUNCHER)/launchcmd.c $(LAUNCHER)/launchcmd.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -DTEST_DIR=\"$(BUILD)\" test_launchcmd.c $(LAUNCHER)/launchcmd.c -o $@
