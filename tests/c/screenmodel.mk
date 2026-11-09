$(BUILD)/test_screenmodel: test_screenmodel.c $(LAUNCHER)/screenmodel.c $(LAUNCHER)/screenmodel.h $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -I$(SDK) test_screenmodel.c $(LAUNCHER)/screenmodel.c -o $@
