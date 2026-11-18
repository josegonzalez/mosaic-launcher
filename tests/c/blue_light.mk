BLUE_LIGHT := ../../plugins/blue-light

$(BUILD)/test_blue_light: test_blue_light.c $(wildcard $(BLUE_LIGHT)/*.c $(BLUE_LIGHT)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -I$(BLUE_LIGHT) test_blue_light.c -o $@
