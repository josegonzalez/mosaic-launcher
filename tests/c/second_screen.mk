SECOND_SCREEN := ../../plugins/second-screen

$(BUILD)/test_second_screen: test_second_screen.c $(wildcard $(SECOND_SCREEN)/*.c $(SECOND_SCREEN)/*.h) check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_second_screen.c $(SECOND_SCREEN)/ss_model.c -o $@
