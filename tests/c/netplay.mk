NETPLAY := ../../plugins/netplay

$(BUILD)/test_netplay: test_netplay.c $(wildcard $(NETPLAY)/*.c $(NETPLAY)/*.h) $(SDK)/mosaic_plugin.h check.h fakeindex.h \
		$(LAUNCHER)/indexmodel.c $(LAUNCHER)/indexmodel.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -I$(LAUNCHER) -DTEST_SDCARD_PATH=\"$(BUILD)/netplay-card\" test_netplay.c \
		$(NETPLAY)/netplay_model.c $(LAUNCHER)/indexmodel.c $(LAUNCHER)/infomodel.c -o $@
