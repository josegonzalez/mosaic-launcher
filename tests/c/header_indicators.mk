# statvfs on the build folder stands in for the card.
HEADER_INDICATORS := ../../plugins/header-indicators

$(BUILD)/test_header_indicators: test_header_indicators.c $(wildcard $(HEADER_INDICATORS)/*.c $(HEADER_INDICATORS)/*.h) $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -DTEST_DIR=\"$(BUILD)\" test_header_indicators.c $(HEADER_INDICATORS)/indicators_model.c -o $@
