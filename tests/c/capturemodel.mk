$(BUILD)/test_capturemodel: test_capturemodel.c $(LAUNCHER)/capturemodel.c $(LAUNCHER)/capturemodel.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_capturemodel.c $(LAUNCHER)/capturemodel.c -o $@
