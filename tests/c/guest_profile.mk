GUEST_PROFILE := ../../plugins/guest-profile

$(BUILD)/test_guest_profile: test_guest_profile.c $(wildcard $(GUEST_PROFILE)/*.c $(GUEST_PROFILE)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -I$(GUEST_PROFILE) test_guest_profile.c -o $@
