LAUNCH_ANIMATION := ../../plugins/launch-animation

$(BUILD)/test_launch_animation: test_launch_animation.c $(wildcard $(LAUNCH_ANIMATION)/*.c $(LAUNCH_ANIMATION)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_launch_animation.c $(LAUNCH_ANIMATION)/launch_model.c -o $@
