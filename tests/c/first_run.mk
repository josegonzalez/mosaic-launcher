FIRST_RUN := ../../plugins/first-run

$(BUILD)/test_first_run: test_first_run.c $(wildcard $(FIRST_RUN)/*.c $(FIRST_RUN)/*.h) $(SDK)/mosaic_plugin.h check.h fakeloaded.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/first-run-card\" test_first_run.c $(FIRST_RUN)/wizard_model.c -o $@
