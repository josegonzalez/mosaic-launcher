# SDL-free, and with no launcher headers but its own.
$(BUILD)/test_statusmodel: test_statusmodel.c $(LAUNCHER)/statusmodel.c $(LAUNCHER)/statusmodel.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_statusmodel.c $(LAUNCHER)/statusmodel.c -o $@
