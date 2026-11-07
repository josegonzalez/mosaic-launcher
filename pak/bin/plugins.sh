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

# Prints "<key><TAB><type><TAB><label><TAB><default><TAB><options>" for each
# setting a plugin declares in plugin.txt:
#
#   setting.<key>=<type>|<label>|<default>[|<option>,<option>,...]
#
# where type is toggle (on/off), choice (one of the options) or text.
plugin_settings() {
	sed -n 's/^[[:space:]]*setting\.\([A-Za-z0-9_]*\)[[:space:]]*=[[:space:]]*\(.*\)$/\1|\2/p' "$1/plugin.txt" |
		tr -d '\r' | awk -F '|' 'NF >= 4 { printf "%s\t%s\t%s\t%s\t%s\n", $1, $2, $3, $4, $5 }'
}

# The plugin's settings file, which the launcher's cfg_get reads.
plugin_cfg_path() {
	echo "$STATE_DIR/$1.cfg"
}

# plugin_cfg_get <id> <key> <default>
plugin_cfg_get() {
	cfg="$(plugin_cfg_path "$1")"
	value=""
	found=0
	if [ -f "$cfg" ]; then
		line="$(grep "^$2=" "$cfg" | tail -n 1)"
		if [ -n "$line" ]; then
			value="${line#*=}"
			found=1
		fi
	fi
	if [ "$found" = 1 ]; then printf '%s' "$value"; else printf '%s' "$3"; fi
}

# plugin_cfg_set <id> <key> <value>. Values can't hold newlines.
plugin_cfg_set() {
	mkdir -p "$STATE_DIR"
	cfg="$(plugin_cfg_path "$1")"
	value="$(printf '%s' "$3" | tr -d '\r\n')"
	if [ -f "$cfg" ]; then
		grep -v "^$2=" "$cfg" >"$cfg.tmp" || true
	else
		: >"$cfg.tmp"
	fi
	printf '%s=%s\n' "$2" "$value" >>"$cfg.tmp"
	mv "$cfg.tmp" "$cfg"
}

# Safe to put inside a JSON string.
json_text() {
	# drops double quotes and backslashes
	# shellcheck disable=SC1003
	printf '%s' "$1" | tr -d '"\\'
}
