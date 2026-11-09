$(BUILD)/test_cfgfile: test_cfgfile.c $(LAUNCHER)/cfgfile.c $(LAUNCHER)/cfgfile.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -DTEST_DIR=\"$(BUILD)\" test_cfgfile.c $(LAUNCHER)/cfgfile.c -o $@
