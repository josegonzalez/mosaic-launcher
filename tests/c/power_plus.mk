# With the wake alarm, stay-awake files and power supplies repointed into the build folder.
POWER_PLUS := ../../plugins/power-plus

$(BUILD)/test_power_plus: test_power_plus.c $(wildcard $(POWER_PLUS)/*.c $(POWER_PLUS)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DPP_WAKEALARM_PATH=\"$(BUILD)/pp-wakealarm\" \
		-DPP_STAY_AWAKE_PATH=\"$(BUILD)/pp-stay-awake\" -DPP_STAY_ALIVE_PATH=\"$(BUILD)/pp-stay-alive\" \
		-DPP_VOLTAGE_PATH=\"$(BUILD)/pp-axp-voltage\" -DPP_POWER_SUPPLY_DIR=\"$(BUILD)/pp-supply\" -DBUILD_DIR=\"$(BUILD)\" \
		test_power_plus.c $(POWER_PLUS)/power_model.c -o $@
