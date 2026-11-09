REMAP := ../../plugins/button-remap

$(BUILD)/test_button_remap: test_button_remap.c $(wildcard $(REMAP)/*.c $(REMAP)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_button_remap.c $(REMAP)/remap_model.c -o $@
