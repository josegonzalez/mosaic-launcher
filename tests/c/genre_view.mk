GENRE := ../../plugins/genre-view

$(BUILD)/test_genre_view: test_genre_view.c $(wildcard $(GENRE)/*.c $(GENRE)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/genre-view-card\" test_genre_view.c $(GENRE)/genre_model.c -o $@
