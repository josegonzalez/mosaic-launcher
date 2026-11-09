# SDL-free, and with no launcher headers but its own.
$(BUILD)/test_playmodel: test_playmodel.c $(LAUNCHER)/playmodel.c $(LAUNCHER)/playmodel.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_playmodel.c $(LAUNCHER)/playmodel.c -o $@
