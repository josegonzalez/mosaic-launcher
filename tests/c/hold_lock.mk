HOLD_LOCK := ../../plugins/hold-lock

$(BUILD)/test_hold_lock: test_hold_lock.c $(wildcard $(HOLD_LOCK)/*.c $(HOLD_LOCK)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_hold_lock.c $(HOLD_LOCK)/hold_model.c -o $@
