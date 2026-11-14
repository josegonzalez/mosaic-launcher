# SDL-free, on a fixture card of its own, with cardindex.c's worker.
$(BUILD)/test_indexmodel: test_indexmodel.c $(LAUNCHER)/indexmodel.c $(LAUNCHER)/indexmodel.h $(LAUNCHER)/cardindex.c \
		$(LAUNCHER)/cardindex.h $(LAUNCHER)/infomodel.c $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -pthread -I. -I$(LAUNCHER) -I$(SDK) -DTEST_DIR=\"$(BUILD)/indexmodel\" test_indexmodel.c \
		$(LAUNCHER)/indexmodel.c $(LAUNCHER)/cardindex.c $(LAUNCHER)/infomodel.c -o $@
