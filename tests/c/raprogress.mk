# The worker, on a fixture card with minarch's .ra files.
$(BUILD)/test_raprogress: test_raprogress.c $(LAUNCHER)/raprogress.c $(RA_HASH_SOURCES) $(wildcard $(LAUNCHER)/ra*.h) \
		$(VENDOR_OBJECTS) check.h
	mkdir -p $(BUILD)
	$(CC) $(RA_CFLAGS) -DTEST_DIR=\"$(BUILD)/ra-progress\" test_raprogress.c $(LAUNCHER)/raprogress.c \
		$(RA_HASH_SOURCES) $(VENDOR_OBJECTS) -lz -lpthread -o $@
