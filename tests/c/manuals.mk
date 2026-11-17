MANUALS := ../../plugins/manuals

$(BUILD)/test_manuals: test_manuals.c $(wildcard $(MANUALS)/*.c $(MANUALS)/*.h) check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(MANUALS) -DTEST_SDCARD_PATH=\"$(BUILD)/manuals-card\" test_manuals.c $(MANUALS)/manual_model.c -o $@
