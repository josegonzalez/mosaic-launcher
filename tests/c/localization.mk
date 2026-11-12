LOCALIZATION := ../../plugins/localization

$(BUILD)/test_localization: test_localization.c $(wildcard $(LOCALIZATION)/*.c $(LOCALIZATION)/*.h \
		$(LOCALIZATION)/res/lang/launcher/*.po ../../plugins/*/res/lang/*.po ../../plugins/*/po/*.pot \
		../../template/res/lang/*.po ../../template/po/*.pot) \
		../../po/launcher.pot $(SDK)/mosaic_plugin.h check.h
	mkdir -p $(BUILD)
	$(CC) -std=gnu99 -Wall -Werror -I. -I$(SDK) -I$(LOCALIZATION) -DTEST_DIR=\"$(BUILD)/localization\" \
		-DLOCALIZATION_TEST_RES=\"$(BUILD)/localization/res\" -DREPO_DIR=\"$(abspath ../..)\" \
		test_localization.c $(LOCALIZATION)/po_model.c $(LOCALIZATION)/names_model.c -o $@
