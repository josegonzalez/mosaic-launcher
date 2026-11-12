$(BUILD)/test_fnbinds: test_fnbinds.c $(LAUNCHER)/fnbinds.c $(LAUNCHER)/fnbinds.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_fnbinds.c $(LAUNCHER)/fnbinds.c -o $@
