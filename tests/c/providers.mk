$(BUILD)/test_providers: test_providers.c $(LAUNCHER)/providers.c $(LAUNCHER)/providers.h $(LAUNCHER)/events.c $(LAUNCHER)/events.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_providers.c $(LAUNCHER)/providers.c $(LAUNCHER)/events.c -o $@
