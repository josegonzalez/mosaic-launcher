COLLECTIONS_PLUS := ../../plugins/collections-plus

$(BUILD)/test_collections_plus: test_collections_plus.c $(wildcard $(COLLECTIONS_PLUS)/*.c $(COLLECTIONS_PLUS)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/collections-plus-card\" test_collections_plus.c $(COLLECTIONS_PLUS)/collections_model.c -o $@
