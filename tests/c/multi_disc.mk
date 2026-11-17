MULTI_DISC := ../../plugins/multi-disc

$(BUILD)/test_multi_disc: test_multi_disc.c $(wildcard $(MULTI_DISC)/*.c $(MULTI_DISC)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_multi_disc.c $(MULTI_DISC)/disc_model.c -o $@
