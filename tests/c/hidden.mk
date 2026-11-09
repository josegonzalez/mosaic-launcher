HIDDEN := ../../plugins/hidden

$(BUILD)/test_hidden: test_hidden.c $(HIDDEN)/hidden.c $(HIDDEN)/hiddenset.c $(HIDDEN)/hiddenset.h $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/hidden-card\" test_hidden.c $(HIDDEN)/hiddenset.c -o $@
