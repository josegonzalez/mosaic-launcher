$(BUILD)/test_profiles: test_profiles.c $(LAUNCHER)/profiles.c $(LAUNCHER)/profiles.h check.h
	mkdir -p $(BUILD)/profiles
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(LAUNCHER) -DTEST_DIR=\"$(BUILD)/profiles\" test_profiles.c $(LAUNCHER)/profiles.c -o $@
