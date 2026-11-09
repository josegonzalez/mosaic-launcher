# Against a fixture game_logs.sqlite it makes in its own folder, through the
# system's sqlite library, which both it and the reader load with dlopen.
$(BUILD)/test_playhistory: test_playhistory.c $(LAUNCHER)/playhistory.c $(LAUNCHER)/playmodel.c \
		$(LAUNCHER)/playhistory.h $(LAUNCHER)/playmodel.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -DTEST_DIR=\"$(BUILD)/play-history\" \
		test_playhistory.c $(LAUNCHER)/playhistory.c $(LAUNCHER)/playmodel.c -ldl -o $@
