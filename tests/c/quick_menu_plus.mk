QUICK_MENU_PLUS := ../../plugins/quick-menu-plus

$(BUILD)/test_quick_menu_plus: test_quick_menu_plus.c $(wildcard $(QUICK_MENU_PLUS)/*.c $(QUICK_MENU_PLUS)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_DIR=\"$(BUILD)\" -DQMP_AWAKE_FILE=\"$(BUILD)/qmp-awake\" test_quick_menu_plus.c $(QUICK_MENU_PLUS)/quick_model.c -o $@
