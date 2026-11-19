$(BUILD)/test_sinkmodel: test_sinkmodel.c $(LAUNCHER)/sinkmodel.c $(LAUNCHER)/sinkmodel.h $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -I$(SDK) test_sinkmodel.c $(LAUNCHER)/sinkmodel.c -o $@
