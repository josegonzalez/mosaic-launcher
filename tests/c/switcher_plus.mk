PLAYTIME_CAPTIONS := ../../plugins/playtime-captions
SWITCHER_PLUS := ../../plugins/switcher-plus

# with playtime-captions' model, to check the captions' copy reads the same
$(BUILD)/test_switcher_plus: test_switcher_plus.c $(wildcard $(SWITCHER_PLUS)/*.c $(SWITCHER_PLUS)/*.h) \
	$(PLAYTIME_CAPTIONS)/captions_model.c $(PLAYTIME_CAPTIONS)/captions_model.h $(SDK)/mosaic_plugin.h check.h words.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_switcher_plus.c $(SWITCHER_PLUS)/switcher_model.c \
		$(PLAYTIME_CAPTIONS)/captions_model.c -o $@
