METADATA_BAND := ../../plugins/metadata-band

$(BUILD)/test_metadata_band: test_metadata_band.c $(wildcard $(METADATA_BAND)/*.c $(METADATA_BAND)/*.h) check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. test_metadata_band.c $(METADATA_BAND)/band_model.c -o $@
