# SDL-free: smooth scaling of the sprite sheet's pieces.
$(BUILD)/test_scalemodel: test_scalemodel.c $(LAUNCHER)/scalemodel.c $(LAUNCHER)/scalemodel.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_scalemodel.c $(LAUNCHER)/scalemodel.c -lm -o $@
