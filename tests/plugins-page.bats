#!/usr/bin/env bats

load test_helper

setup() {
	setup_fake_card
	PAK_DIR="$PAK"
	. "$PAK/bin/common.sh"
	. "$PAK/bin/plugins.sh"

	mkdir -p "$PAK/plugins/favorites.plugin" "$SDCARD_PATH/Tools/$PLATFORM/Grid.pak" "$SDCARD_PATH/Tools/$PLATFORM/Clock.pak"
	printf 'id=favorites\nname=Favorites\napi=1\n' >"$PAK/plugins/favorites.plugin/plugin.txt"
	printf 'id = grid\r\nname=Grid View\napi=1\n' >"$SDCARD_PATH/Tools/$PLATFORM/Grid.pak/plugin.txt"
}

@test "lists bundled plugins first, then installed ones, and skips other paks" {
	run plugin_dirs
	[ "$status" -eq 0 ]
	[ "${#lines[@]}" -eq 2 ]
	[ "${lines[0]}" = "$PAK/plugins/favorites.plugin	1" ]
	[ "${lines[1]}" = "$SDCARD_PATH/Tools/$PLATFORM/Grid.pak	0" ]
}

@test "reads manifest fields, tolerating spaces and CRLF" {
	[ "$(plugin_field "$SDCARD_PATH/Tools/$PLATFORM/Grid.pak" id)" = "grid" ]
	[ "$(plugin_field "$SDCARD_PATH/Tools/$PLATFORM/Grid.pak" name)" = "Grid View" ]
}

@test "bundled plugins start off and installed ones on" {
	! plugin_enabled favorites 1
	plugin_enabled grid 0
}

@test "the last choice wins" {
	printf 'favorites=on\nfavorites=off\n' >"$PLUGIN_CHOICES"
	! plugin_enabled favorites 1
}

@test "turning a plugin on or off keeps its place in the load order" {
	printf 'grid=on\nfavorites=off\nslot.layout=grid\n' >"$PLUGIN_CHOICES"
	plugin_set favorites on
	[ "$(cat "$PLUGIN_CHOICES")" = "$(printf 'grid=on\nfavorites=on\nslot.layout=grid')" ]
	plugin_enabled favorites 1
}

@test "a new choice goes at the end" {
	plugin_set grid off
	plugin_set favorites on
	[ "$(cat "$PLUGIN_CHOICES")" = "$(printf 'grid=off\nfavorites=on')" ]
}

@test "lists the settings a plugin declares" {
	printf 'id=favorites\nsetting.collection=text|Collection|Favorites\r\nsetting.order=choice|Order|newest|newest,oldest\nsetting.broken=text\n' \
		>"$PAK/plugins/favorites.plugin/plugin.txt"
	run plugin_settings "$PAK/plugins/favorites.plugin"
	[ "$status" -eq 0 ]
	[ "${#lines[@]}" -eq 2 ]
	[ "${lines[0]}" = "$(printf 'collection\ttext\tCollection\tFavorites\t')" ]
	[ "${lines[1]}" = "$(printf 'order\tchoice\tOrder\tnewest\tnewest,oldest')" ]
}

@test "plugins without settings list none" {
	[ -z "$(plugin_settings "$SDCARD_PATH/Tools/$PLATFORM/Grid.pak")" ]
}

@test "settings fall back to their default until set" {
	[ "$(plugin_cfg_get favorites collection Favorites)" = "Favorites" ]
	plugin_cfg_set favorites collection "My Games"
	[ "$(plugin_cfg_get favorites collection Favorites)" = "My Games" ]
}

@test "setting a value replaces it and keeps the plugin's other settings" {
	printf 'started=yes\ncollection=Old\n' >"$(plugin_cfg_path favorites)"
	plugin_cfg_set favorites collection "New = Better"
	[ "$(cat "$(plugin_cfg_path favorites)")" = "$(printf 'started=yes\ncollection=New = Better')" ]
}

@test "an empty value counts as set" {
	plugin_cfg_set favorites collection ""
	[ "$(plugin_cfg_get favorites collection Favorites)" = "" ]
}

@test "names can't break the menu's JSON" {
	[ "$(json_text 'Say "hi" \ there')" = "Say hi  there" ]
}
