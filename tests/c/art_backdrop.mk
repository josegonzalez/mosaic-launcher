BACKDROP := ../../plugins/art-backdrop

$(BUILD)/test_art_backdrop: test_art_backdrop.c $(wildcard $(BACKDROP)/*.c $(BACKDROP)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_art_backdrop.c $(BACKDROP)/backdrop_model.c -o $@
