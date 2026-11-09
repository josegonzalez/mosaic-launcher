SEARCH := ../../plugins/search

$(BUILD)/test_search: test_search.c $(wildcard $(SEARCH)/*.c $(SEARCH)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/search-card\" -DSE_LAST_FILE=\"$(BUILD)/search-last\" test_search.c $(SEARCH)/search_model.c -o $@
