# With genre-view's model, which the plugin builds in too.
COLLATE := ../../plugins/collate-subfolders
GENRE := ../../plugins/genre-view

$(BUILD)/test_collate_subfolders: test_collate_subfolders.c $(wildcard $(COLLATE)/*.c $(COLLATE)/*.h $(GENRE)/genre_model.*) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/collate-subfolders-card\" test_collate_subfolders.c $(COLLATE)/collate_model.c $(GENRE)/genre_model.c -o $@
