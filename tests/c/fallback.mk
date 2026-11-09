# SDL-free: the fallback font's run splitting, widths and line breaks.
$(BUILD)/test_fallback: test_fallback.c $(LAUNCHER)/fallbackmodel.c $(LAUNCHER)/fallbackmodel.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_fallback.c $(LAUNCHER)/fallbackmodel.c -o $@
