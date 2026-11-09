TITLES := ../../plugins/title-disambiguation

$(BUILD)/test_title_disambiguation: test_title_disambiguation.c $(wildcard $(TITLES)/*.c $(TITLES)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_title_disambiguation.c $(TITLES)/titles.c -o $@
