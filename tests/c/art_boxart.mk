BOXART := ../../plugins/art-boxart

$(BUILD)/test_art_boxart: test_art_boxart.c $(wildcard $(BOXART)/*.c $(BOXART)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/art-boxart-card\" test_art_boxart.c $(BOXART)/boxart_model.c -o $@
