SYSTEM_INFO := ../../plugins/system-info

$(BUILD)/test_system_info: test_system_info.c $(wildcard $(SYSTEM_INFO)/*.c $(SYSTEM_INFO)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/system-info-card\" test_system_info.c $(SYSTEM_INFO)/info_model.c -o $@
