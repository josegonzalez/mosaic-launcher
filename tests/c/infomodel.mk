# SDL-free, and with no launcher headers but its own.
$(BUILD)/test_infomodel: test_infomodel.c $(LAUNCHER)/infomodel.c $(LAUNCHER)/infomodel.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_infomodel.c $(LAUNCHER)/infomodel.c -o $@
