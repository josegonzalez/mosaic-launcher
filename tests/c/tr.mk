# SDL-free: the strings slot's checks and cache, and TR() over them.
$(BUILD)/test_tr: test_tr.c $(LAUNCHER)/tr.c $(LAUNCHER)/trmodel.c $(LAUNCHER)/caller.c $(LAUNCHER)/tr.h $(LAUNCHER)/trmodel.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_tr.c $(LAUNCHER)/tr.c $(LAUNCHER)/trmodel.c $(LAUNCHER)/caller.c -o $@
