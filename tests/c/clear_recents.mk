CLEAR_RECENTS := ../../plugins/clear-recents

$(BUILD)/test_clear_recents: test_clear_recents.c $(wildcard $(CLEAR_RECENTS)/*.c) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_clear_recents.c -o $@
