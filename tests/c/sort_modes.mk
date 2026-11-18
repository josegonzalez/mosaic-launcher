SORT_MODES := ../../plugins/sort-modes

$(BUILD)/test_sort_modes: test_sort_modes.c $(wildcard $(SORT_MODES)/*.c $(SORT_MODES)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SORT_MODES) test_sort_modes.c $(SORT_MODES)/sort_model.c -o $@
