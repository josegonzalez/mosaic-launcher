HERO_RAIL := ../../plugins/layout-hero-rail

$(BUILD)/test_layout_hero_rail: test_layout_hero_rail.c $(wildcard $(HERO_RAIL)/*.c $(HERO_RAIL)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_layout_hero_rail.c $(HERO_RAIL)/hero_model.c -o $@
