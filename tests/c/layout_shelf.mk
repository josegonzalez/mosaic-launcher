LAYOUT_SHELF := ../../plugins/layout-shelf

$(BUILD)/test_layout_shelf: test_layout_shelf.c $(wildcard $(LAYOUT_SHELF)/*.c $(LAYOUT_SHELF)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_layout_shelf.c $(LAYOUT_SHELF)/shelf_model.c -o $@
