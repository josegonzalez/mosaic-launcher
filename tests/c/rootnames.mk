# SDL-free: the root list's names as the strings slot's owner translates them.
$(BUILD)/test_rootnames: test_rootnames.c $(LAUNCHER)/rootnames.c $(LAUNCHER)/rootnames.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_rootnames.c $(LAUNCHER)/rootnames.c -o $@
