FILTER_PRESETS := ../../plugins/filter-presets
FILTER_PRESETS_SEARCH := ../../plugins/search

$(BUILD)/test_filter_presets: test_filter_presets.c $(wildcard $(FILTER_PRESETS)/*.c $(FILTER_PRESETS)/*.h) \
		$(FILTER_PRESETS_SEARCH)/search_model.c $(FILTER_PRESETS_SEARCH)/search_model.h $(SDK)/mosaic_plugin.h check.h fakesets.h fakeproviders.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -I$(FILTER_PRESETS_SEARCH) test_filter_presets.c $(FILTER_PRESETS)/presets_model.c \
		$(FILTER_PRESETS_SEARCH)/search_model.c -o $@
