# Through rcheevos, on carts, zips and discs it writes in its own folder, and
# CHD fixtures made from those discs.
$(BUILD)/test_rahash: test_rahash.c $(RA_HASH_SOURCES) $(wildcard $(LAUNCHER)/ra*.h) $(VENDOR_OBJECTS) check.h
	mkdir -p $(BUILD)
	$(CC) $(RA_CFLAGS) -DTEST_DIR=\"$(BUILD)/ra-hash\" -DFIXTURES=\"$(abspath fixtures/ra)\" \
		test_rahash.c $(RA_HASH_SOURCES) $(VENDOR_OBJECTS) -lz -o $@
