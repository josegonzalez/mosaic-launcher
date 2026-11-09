TRANSITIONS := ../../plugins/transitions

$(BUILD)/test_transitions: test_transitions.c $(wildcard $(TRANSITIONS)/*.c $(TRANSITIONS)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_transitions.c $(TRANSITIONS)/transitions_model.c -o $@
