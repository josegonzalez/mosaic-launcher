PLAYTIME_CAPTIONS := ../../plugins/playtime-captions

$(BUILD)/test_playtime_captions: test_playtime_captions.c $(wildcard $(PLAYTIME_CAPTIONS)/*.c $(PLAYTIME_CAPTIONS)/*.h) $(SDK)/mosaic_plugin.h check.h words.h fakeproviders.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_playtime_captions.c $(PLAYTIME_CAPTIONS)/captions_model.c -o $@
