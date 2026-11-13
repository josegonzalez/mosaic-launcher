PLAYTIME_CAPTIONS := ../../plugins/playtime-captions
SWITCHER_PLUS := ../../plugins/switcher-plus

# with playtime-captions' model, whose captions.text the fake host answers with
$(BUILD)/test_switcher_plus: test_switcher_plus.c $(wildcard $(SWITCHER_PLUS)/*.c $(SWITCHER_PLUS)/*.h) \
	$(PLAYTIME_CAPTIONS)/captions_model.c $(PLAYTIME_CAPTIONS)/captions_model.h $(SDK)/mosaic_plugin.h check.h fakeproviders.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_switcher_plus.c $(SWITCHER_PLUS)/switcher_model.c \
		$(PLAYTIME_CAPTIONS)/captions_model.c -o $@
