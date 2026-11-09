# SDL-free, and with no launcher headers but its own.
$(BUILD)/test_artcache: test_artcache.c $(LAUNCHER)/artcache.c $(LAUNCHER)/artcache.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_artcache.c $(LAUNCHER)/artcache.c -o $@
