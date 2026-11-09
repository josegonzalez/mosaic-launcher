ACTIONS_SOURCES = test_actions.c $(LAUNCHER)/actions.c $(LAUNCHER)/tr.c $(LAUNCHER)/trmodel.c

$(BUILD)/test_actions: $(ACTIONS_SOURCES) $(wildcard $(LAUNCHER)/*.h) check.h
	mkdir -p $(BUILD)
	$(CC) $(CARD_CFLAGS) $(ACTIONS_SOURCES) -o $@
