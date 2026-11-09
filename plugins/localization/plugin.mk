###########################################################
# localization: its bundled language packs, res/*.po, go in res/ beside
# its plugin.txt, where it finds them from its plugin.so's path (dladdr).

LOCALIZATION_RES := $(wildcard localization/res/*.po)

.PHONY: localization-res
localization-res:
	mkdir -p $(OUT)/localization.plugin/res
	cp $(LOCALIZATION_RES) $(OUT)/localization.plugin/res/

localization: localization-res
localization: PLUGIN_LIBS = -ldl
