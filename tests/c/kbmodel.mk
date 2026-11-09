$(BUILD)/test_kbmodel: test_kbmodel.c $(LAUNCHER)/kbmodel.c $(LAUNCHER)/kbmodel.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_kbmodel.c $(LAUNCHER)/kbmodel.c -o $@
