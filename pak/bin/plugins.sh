#!/bin/sh
# Plugin listing and on/off state for the Launcher pak's Plugins page.
# Sourced after common.sh. Follows the same rules as the launcher
# (launcher/pluginset.c): the last <id>=on|off line in plugins.txt wins, and
# without one, bundled plugins are off and installed ones are on.

PLUGIN_CHOICES="$STATE_DIR/plugins.txt"

# Prints "<dir><TAB><bundled>" for every plugin directory, bundled first.
plugin_dirs() {
	for dir in "$PAK_DIR"/plugins/*.plugin; do
		if [ -f "$dir/plugin.txt" ]; then printf '%s\t1\n' "$dir"; fi
	done
	for dir in "$SDCARD_PATH/Tools/$PLATFORM"/*.pak; do
		if [ -f "$dir/plugin.txt" ]; then printf '%s\t0\n' "$dir"; fi
	done
}

# plugin_field <dir> <key>
plugin_field() {
	sed -n "s/^[[:space:]]*$2[[:space:]]*=[[:space:]]*\(.*\)$/\1/p" "$1/plugin.txt" | tr -d '\r' | tail -n 1
}

# plugin_enabled <id> <bundled>
plugin_enabled() {
	choice=""
	if [ -f "$PLUGIN_CHOICES" ]; then
		choice="$(sed -n "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*\([a-z]*\)[[:space:]]*$/\1/p" "$PLUGIN_CHOICES" | tail -n 1)"
	fi
	case "$choice" in
		on) return 0 ;;
		off) return 1 ;;
	esac
	[ "$2" = "0" ]
}

# plugin_set <id> on|off. Rewrites the plugin's line where it is, so load
# order doesn't change, or adds one at the end.
plugin_set() {
	mkdir -p "$STATE_DIR"
	if [ -f "$PLUGIN_CHOICES" ] && grep -q "^[[:space:]]*$1[[:space:]]*=" "$PLUGIN_CHOICES"; then
		sed "s/^[[:space:]]*$1[[:space:]]*=.*$/$1=$2/" "$PLUGIN_CHOICES" >"$PLUGIN_CHOICES.tmp"
		mv "$PLUGIN_CHOICES.tmp" "$PLUGIN_CHOICES"
	else
		echo "$1=$2" >>"$PLUGIN_CHOICES"
	fi
}

# Safe to put inside a JSON string.
json_text() {
	# drops double quotes and backslashes
	# shellcheck disable=SC1003
	printf '%s' "$1" | tr -d '"\\'
}
