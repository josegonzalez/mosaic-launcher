LAYOUT_CAROUSEL := ../../plugins/layout-carousel

$(BUILD)/test_layout_carousel: test_layout_carousel.c $(wildcard $(LAYOUT_CAROUSEL)/*.c $(LAYOUT_CAROUSEL)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_layout_carousel.c $(LAYOUT_CAROUSEL)/carousel_model.c -o $@
