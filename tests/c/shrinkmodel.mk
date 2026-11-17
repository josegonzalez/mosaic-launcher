# SDL-free: image_load_fit's header sizes, fitting and shrinking.
$(BUILD)/test_shrinkmodel: test_shrinkmodel.c $(LAUNCHER)/shrinkmodel.c $(LAUNCHER)/shrinkmodel.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_shrinkmodel.c $(LAUNCHER)/shrinkmodel.c -o $@
