SAVE_STATE_MANAGER := ../../plugins/save-state-manager

$(BUILD)/test_save_state_manager: test_save_state_manager.c $(wildcard $(SAVE_STATE_MANAGER)/*.c $(SAVE_STATE_MANAGER)/*.h) $(SDK)/mosaic_plugin.h check.h words.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/save-state-manager-card\" test_save_state_manager.c $(SAVE_STATE_MANAGER)/state_model.c -o $@
