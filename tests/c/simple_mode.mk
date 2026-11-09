SIMPLE := ../../plugins/simple-mode

$(BUILD)/test_simple_mode: test_simple_mode.c $(wildcard $(SIMPLE)/*.c $(SIMPLE)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_simple_mode.c $(SIMPLE)/simple_model.c -o $@
