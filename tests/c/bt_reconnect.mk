BT_RECONNECT := ../../plugins/bt-reconnect

$(BUILD)/test_bt_reconnect: test_bt_reconnect.c $(wildcard $(BT_RECONNECT)/*.c $(BT_RECONNECT)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) test_bt_reconnect.c $(BT_RECONNECT)/bt_model.c -o $@
