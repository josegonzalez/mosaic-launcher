# Plugins are tested against a fake host, by including their source.
FAVORITES := ../../plugins/favorites

$(BUILD)/test_favorites: test_favorites.c $(wildcard $(FAVORITES)/*.c $(FAVORITES)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/favorites-card\" test_favorites.c $(FAVORITES)/pin_model.c -o $@
