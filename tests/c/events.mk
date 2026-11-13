$(BUILD)/test_events: test_events.c $(LAUNCHER)/events.c $(LAUNCHER)/events.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_events.c $(LAUNCHER)/events.c -o $@
