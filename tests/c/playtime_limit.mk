PLAYTIME_LIMIT := ../../plugins/playtime-limit

$(BUILD)/test_playtime_limit: test_playtime_limit.c $(wildcard $(PLAYTIME_LIMIT)/*.c $(PLAYTIME_LIMIT)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_playtime_limit.c $(PLAYTIME_LIMIT)/limit_model.c -o $@
