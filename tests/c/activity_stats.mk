ACTIVITY_STATS := ../../plugins/activity-stats

$(BUILD)/test_activity_stats: test_activity_stats.c $(wildcard $(ACTIVITY_STATS)/*.c $(ACTIVITY_STATS)/*.h) $(SDK)/mosaic_plugin.h check.h words.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/activity-stats-card\" test_activity_stats.c $(ACTIVITY_STATS)/activity_model.c -o $@
