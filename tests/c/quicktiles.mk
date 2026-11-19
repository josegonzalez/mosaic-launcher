# quicktiles.c's plugin tiles, over the listing code its entries come from.
QUICKTILES_SOURCES = test_quicktiles.c stubs.c $(LAUNCHER)/nav.c $(LAUNCHER)/library.c $(LAUNCHER)/listsort.c $(LAUNCHER)/containers.c \
	$(LAUNCHER)/state.c $(LAUNCHER)/quicktiles.c $(LAUNCHER)/actions.c $(LAUNCHER)/tr.c $(LAUNCHER)/trmodel.c $(LAUNCHER)/caller.c $(LAUNCHER)/rootnames.c $(NEXTUI)/all/common/utils.c

$(BUILD)/test_quicktiles: $(QUICKTILES_SOURCES) $(wildcard $(LAUNCHER)/*.h) check.h platform/platform.h
	mkdir -p $(BUILD)
	$(CC) $(filter-out -DTEST_SDCARD_PATH=%,$(CARD_CFLAGS)) -DTEST_SDCARD_PATH=\"$(BUILD)/quicktiles-card\" $(QUICKTILES_SOURCES) -lm -o $@
