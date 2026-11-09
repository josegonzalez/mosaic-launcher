POWER_SOURCES =test_power.c $(LAUNCHER)/power.c $(LAUNCHER)/powerwatch.c

$(BUILD)/test_power: $(POWER_SOURCES) $(wildcard $(LAUNCHER)/*.h) check.h
	mkdir -p $(BUILD)
	$(CC) $(CARD_CFLAGS) -Werror $(POWER_SOURCES) -o $@
