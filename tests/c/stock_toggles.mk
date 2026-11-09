# Against stock's config.c, on a card of its own since it writes minuisettings.txt.
# Unfortified: reading a saved font style, stock copies the font name onto
# itself, which macOS's checked strncpy stops.
STOCK_TOGGLES_SOURCES = test_stock_toggles.c $(LAUNCHER)/stocktoggles.c $(NEXTUI)/all/common/config.c $(NEXTUI)/all/common/utils.c

$(BUILD)/test_stock_toggles: $(STOCK_TOGGLES_SOURCES) $(LAUNCHER)/stocktoggles.h $(SDK)/mosaic_plugin.h check.h platform/platform.h
	mkdir -p $(BUILD)
	$(CC) $(filter-out -DTEST_SDCARD_PATH=%,$(CARD_CFLAGS)) -DTEST_SDCARD_PATH=\"$(BUILD)/stock-toggles-card\" -D_FORTIFY_SOURCE=0 \
		$(STOCK_TOGGLES_SOURCES) -lm -o $@
