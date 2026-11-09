# Against a fake of stock's LED profiles and override stack.
$(BUILD)/test_leds: test_leds.c $(LAUNCHER)/leds.c $(LAUNCHER)/leds.h check.h
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) test_leds.c $(LAUNCHER)/leds.c -o $@
