TASKS_PLUGIN := ../../plugins/tasks

$(BUILD)/test_tasks: test_tasks.c $(wildcard $(TASKS_PLUGIN)/*.c $(TASKS_PLUGIN)/*.h) check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_SDCARD_PATH=\"$(BUILD)/tasks-card\" test_tasks.c \
		$(TASKS_PLUGIN)/tasks_model.c $(TASKS_PLUGIN)/tasks_run.c -o $@
