# Against stock's palette.c and config.c, on a card of its own with
# built-in and card palettes and a minuisettings.txt. Unfortified, as
# stock_toggles.mk is, for stock's config.c.
PALETTES_SOURCES = test_palettes.c $(LAUNCHER)/palettes.c $(NEXTUI)/all/common/palette.c $(NEXTUI)/all/common/config.c $(NEXTUI)/all/common/utils.c

$(BUILD)/test_palettes: $(PALETTES_SOURCES) $(LAUNCHER)/palettes.h $(SDK)/mosaic_plugin.h check.h platform/platform.h
	mkdir -p $(BUILD)
	$(CC) $(filter-out -DTEST_SDCARD_PATH=%,$(CARD_CFLAGS)) -DTEST_SDCARD_PATH=\"$(BUILD)/palettes-card\" -D_FORTIFY_SOURCE=0 \
		$(PALETTES_SOURCES) -lm -o $@
