$(BUILD)/test_art: test_art.c $(LAUNCHER)/art.c check.h
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) test_art.c $(LAUNCHER)/art.c -o $@
