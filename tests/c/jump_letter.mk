JUMP_LETTER := ../../plugins/jump-letter

$(BUILD)/test_jump_letter: test_jump_letter.c $(wildcard $(JUMP_LETTER)/*.c $(JUMP_LETTER)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(JUMP_LETTER) test_jump_letter.c $(JUMP_LETTER)/jump_model.c -o $@
