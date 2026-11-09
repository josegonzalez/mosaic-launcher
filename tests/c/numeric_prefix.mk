NUMERIC := ../../plugins/numeric-prefix

$(BUILD)/test_numeric_prefix: test_numeric_prefix.c $(wildcard $(NUMERIC)/*.c $(NUMERIC)/*.h) ../../plugins/all-games/games_model.c ../../plugins/all-games/games_model.h $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_numeric_prefix.c $(NUMERIC)/numprefix.c ../../plugins/all-games/games_model.c -o $@
