LAYOUT_GRID := ../../plugins/layout-grid

$(BUILD)/test_layout_grid: test_layout_grid.c $(wildcard $(LAYOUT_GRID)/*.c $(LAYOUT_GRID)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_layout_grid.c $(LAYOUT_GRID)/grid_model.c -o $@
