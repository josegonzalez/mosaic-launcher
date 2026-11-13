TAGS := ../../plugins/tags

$(BUILD)/test_tags: test_tags.c $(wildcard $(TAGS)/*.c $(TAGS)/*.h) $(SDK)/mosaic_plugin.h check.h fakesets.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/tags-card\" test_tags.c $(TAGS)/tags_model.c -o $@
