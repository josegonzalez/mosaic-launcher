# nav.c's folder stack over the listing code, on a card of its own.
NAV_SOURCES = test_nav.c stubs.c $(LAUNCHER)/nav.c $(LAUNCHER)/library.c $(LAUNCHER)/listsort.c $(LAUNCHER)/containers.c \
	$(LAUNCHER)/state.c $(LAUNCHER)/quicktiles.c $(LAUNCHER)/actions.c $(LAUNCHER)/tr.c $(NEXTUI)/all/common/utils.c

$(BUILD)/test_nav: $(NAV_SOURCES) $(wildcard $(LAUNCHER)/*.h) check.h platform/platform.h
	mkdir -p $(BUILD)
	$(CC) $(filter-out -DTEST_SDCARD_PATH=%,$(CARD_CFLAGS)) -DTEST_SDCARD_PATH=\"$(BUILD)/nav-card\" $(NAV_SOURCES) -lm -o $@
