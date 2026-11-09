# SDL-free, and with no launcher headers but its own.
$(BUILD)/test_selmodel: test_selmodel.c $(LAUNCHER)/selmodel.c $(LAUNCHER)/selmodel.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_selmodel.c $(LAUNCHER)/selmodel.c -o $@
