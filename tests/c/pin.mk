$(BUILD)/test_pin: test_pin.c $(LAUNCHER)/pin.c $(LAUNCHER)/pin.h $(LAUNCHER)/cfgfile.c $(LAUNCHER)/cfgfile.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -DTEST_DIR=\"$(BUILD)\" test_pin.c $(LAUNCHER)/pin.c $(LAUNCHER)/cfgfile.c -o $@
