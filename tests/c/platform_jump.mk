PLATFORM_JUMP := ../../plugins/platform-jump

$(BUILD)/test_platform_jump: test_platform_jump.c $(wildcard $(PLATFORM_JUMP)/*.c $(PLATFORM_JUMP)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_platform_jump.c $(PLATFORM_JUMP)/jump_model.c -o $@
