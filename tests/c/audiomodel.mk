# SDL-free, and with no launcher headers but its own.
$(BUILD)/test_audiomodel: test_audiomodel.c $(LAUNCHER)/audiomodel.c $(LAUNCHER)/audiomodel.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_audiomodel.c $(LAUNCHER)/audiomodel.c -o $@
