# SDL-free: the parsers, on bytes made here.
$(BUILD)/test_ramodel: test_ramodel.c $(LAUNCHER)/ramodel.c $(LAUNCHER)/ramodel.h check.h
	mkdir -p $(BUILD)
	$(CC) $(RA_CFLAGS) test_ramodel.c $(LAUNCHER)/ramodel.c -o $@
