# With the launcher's launchcmd.c behind its fake replace_launch, to check
# the command line the replaced launch gets.
OVERRIDE := ../../plugins/emulator-override

$(BUILD)/test_emulator_override: test_emulator_override.c $(wildcard $(OVERRIDE)/*.c $(OVERRIDE)/*.h) $(LAUNCHER)/launchcmd.c $(LAUNCHER)/launchcmd.h $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -I$(LAUNCHER) -DTEST_SDCARD_PATH=\"$(BUILD)/emulator-override-card\" test_emulator_override.c $(OVERRIDE)/override_model.c $(LAUNCHER)/launchcmd.c -o $@
