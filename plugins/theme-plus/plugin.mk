###########################################################
# theme-plus: its bundled themes, each a folder with a theme.txt in
# theme-plus/themes/, go into the plugin's own folder, beside lib/.

theme-plus: theme-plus-themes

.PHONY: theme-plus-themes
theme-plus-themes:
	mkdir -p $(OUT)/theme-plus.plugin
	rm -rf $(OUT)/theme-plus.plugin/themes
	cp -R theme-plus/themes $(OUT)/theme-plus.plugin/themes
