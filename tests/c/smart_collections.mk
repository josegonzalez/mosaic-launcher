SMART := ../../plugins/smart-collections

$(BUILD)/test_smart_collections: test_smart_collections.c $(wildcard $(SMART)/*.c $(SMART)/*.h) $(SDK)/mosaic_plugin.h check.h fakesets.h \
		fakeindex.h $(LAUNCHER)/indexmodel.c $(LAUNCHER)/indexmodel.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -I$(LAUNCHER) -DTEST_SDCARD_PATH=\"$(BUILD)/smart-collections-card\" test_smart_collections.c \
		$(SMART)/smart_model.c $(LAUNCHER)/indexmodel.c $(LAUNCHER)/infomodel.c -o $@
