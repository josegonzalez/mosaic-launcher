PAKHOOKS_SOURCES = test_pakhooks.c stubs.c $(LAUNCHER)/pakhooks.c $(LAUNCHER)/listsort.c $(LAUNCHER)/containers.c \
	$(LAUNCHER)/state.c $(NEXTUI)/all/common/utils.c

$(BUILD)/test_pakhooks: $(PAKHOOKS_SOURCES) $(wildcard $(LAUNCHER)/*.h) check.h platform/platform.h
	mkdir -p $(BUILD)
	$(CC) $(CARD_CFLAGS) $(PAKHOOKS_SOURCES) -lm -o $@
