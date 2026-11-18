SMART := ../../plugins/smart-collections
SMART_SEARCH := ../../plugins/search

$(BUILD)/test_smart_collections: test_smart_collections.c $(wildcard $(SMART)/*.c $(SMART)/*.h) $(SDK)/mosaic_plugin.h check.h fakesets.h \
		fakeindex.h fakeproviders.h $(LAUNCHER)/indexmodel.c $(LAUNCHER)/indexmodel.h $(SMART_SEARCH)/search_model.c $(SMART_SEARCH)/search_model.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -I$(LAUNCHER) -I$(SMART_SEARCH) -DTEST_SDCARD_PATH=\"$(BUILD)/smart-collections-card\" test_smart_collections.c \
		$(SMART)/smart_model.c $(SMART_SEARCH)/search_model.c $(LAUNCHER)/indexmodel.c $(LAUNCHER)/infomodel.c -o $@
