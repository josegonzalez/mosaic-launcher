CHARGING_SCREEN := ../../plugins/charging-screen

$(BUILD)/test_charging_screen: test_charging_screen.c $(wildcard $(CHARGING_SCREEN)/*.c $(CHARGING_SCREEN)/*.h) $(SDK)/mosaic_plugin.h check.h words.h fakeproviders.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_charging_screen.c $(CHARGING_SCREEN)/charging_model.c -o $@
