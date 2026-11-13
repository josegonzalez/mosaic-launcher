# On a fixture card of its own, with a fake posix_fadvise and clock.
PREFETCH := ../../plugins/core-prefetch

$(BUILD)/test_core_prefetch: test_core_prefetch.c $(wildcard $(PREFETCH)/*.c $(PREFETCH)/*.h) $(SDK)/mosaic_plugin.h check.h fakeloaded.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_DIR=\"$(BUILD)/core-prefetch\" test_core_prefetch.c $(PREFETCH)/prefetch_model.c -o $@
