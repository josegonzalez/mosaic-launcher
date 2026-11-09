LED_STATUS := ../../plugins/led-status

$(BUILD)/test_led_status: test_led_status.c $(wildcard $(LED_STATUS)/*.c $(LED_STATUS)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_led_status.c $(LED_STATUS)/led_model.c -o $@
