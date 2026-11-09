PLACEHOLDER := ../../plugins/art-placeholder

$(BUILD)/test_art_placeholder: test_art_placeholder.c $(wildcard $(PLACEHOLDER)/*.c $(PLACEHOLDER)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_art_placeholder.c $(PLACEHOLDER)/placeholder_model.c -o $@
