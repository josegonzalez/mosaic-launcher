# Against a fake hcitool and bluetoothctl it writes into its own folder.
$(BUILD)/test_bluetooth: test_bluetooth.c $(LAUNCHER)/bluetooth.c $(LAUNCHER)/bluetooth.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -DTEST_DIR=\"$(BUILD)/bluetooth\" test_bluetooth.c $(LAUNCHER)/bluetooth.c -lpthread -o $@
