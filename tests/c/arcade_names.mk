ARCADE_NAMES := ../../plugins/arcade-names

$(BUILD)/test_arcade_names: test_arcade_names.c $(wildcard $(ARCADE_NAMES)/*.c $(ARCADE_NAMES)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_DIR=\"$(BUILD)/arcade-names\" test_arcade_names.c $(ARCADE_NAMES)/dat_model.c -o $@
