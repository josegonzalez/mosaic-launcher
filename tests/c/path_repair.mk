PATH_REPAIR := ../../plugins/path-repair

$(BUILD)/test_path_repair: test_path_repair.c $(wildcard $(PATH_REPAIR)/*.c $(PATH_REPAIR)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/path-repair-card\" test_path_repair.c $(PATH_REPAIR)/repair_model.c -o $@
