SYNC := ../../plugins/sync

$(BUILD)/test_sync: test_sync.c $(wildcard $(SYNC)/*.c $(SYNC)/*.h) check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SYNC) test_sync.c $(SYNC)/sync_model.c -o $@
