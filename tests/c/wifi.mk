# Against a fake source standing in for stock's connection call.
$(BUILD)/test_wifi: test_wifi.c $(LAUNCHER)/wifi.c $(LAUNCHER)/wifi.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) test_wifi.c $(LAUNCHER)/wifi.c -lpthread -o $@
