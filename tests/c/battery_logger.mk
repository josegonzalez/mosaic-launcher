BATTERY := ../../plugins/battery-logger

$(BUILD)/test_battery_logger: test_battery_logger.c $(wildcard $(BATTERY)/*.c $(BATTERY)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_DIR=\"$(BUILD)/battery-logger\" test_battery_logger.c $(BATTERY)/battery_model.c -o $@
