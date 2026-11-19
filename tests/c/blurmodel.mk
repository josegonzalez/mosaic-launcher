# SDL-free: only stdint and the blur model.
$(BUILD)/test_blurmodel: test_blurmodel.c $(LAUNCHER)/blurmodel.c $(LAUNCHER)/blurmodel.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -O2 -Wall -Werror -I. -I$(LAUNCHER) test_blurmodel.c $(LAUNCHER)/blurmodel.c -o $@
