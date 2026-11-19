HOME_DASHBOARD := ../../plugins/home-dashboard

$(BUILD)/test_home_dashboard: test_home_dashboard.c $(wildcard $(HOME_DASHBOARD)/*.c $(HOME_DASHBOARD)/*.h) $(SDK)/mosaic_plugin.h check.h words.h fakeloaded.h fakeproviders.h \
		fakeindex.h $(LAUNCHER)/indexmodel.c $(LAUNCHER)/indexmodel.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -I$(LAUNCHER) -DTEST_SDCARD_PATH=\"$(BUILD)/home-dashboard-card\" test_home_dashboard.c \
		$(HOME_DASHBOARD)/home_model.c $(LAUNCHER)/indexmodel.c $(LAUNCHER)/infomodel.c -o $@
