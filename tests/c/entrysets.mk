$(BUILD)/test_entrysets: test_entrysets.c $(LAUNCHER)/entrysets.c $(LAUNCHER)/entrysets.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_entrysets.c $(LAUNCHER)/entrysets.c -o $@
