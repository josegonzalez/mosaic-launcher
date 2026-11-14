$(BUILD)/test_lockmodel: test_lockmodel.c $(LAUNCHER)/lockmodel.c $(LAUNCHER)/lockmodel.h $(LAUNCHER)/ambientmodel.c $(LAUNCHER)/ambientmodel.h $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -I$(SDK) test_lockmodel.c $(LAUNCHER)/lockmodel.c $(LAUNCHER)/ambientmodel.c -o $@
