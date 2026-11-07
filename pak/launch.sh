#!/bin/sh
# Launcher.pak - turns the plugin-capable launcher on or off.
#
# The launcher itself takes over from stock nextui.elf through boot.sh. This
# script is only the settings screen the user reaches from Tools.

PAK_DIR="$(cd "$(dirname "$0")" && pwd)"
PAK_NAME="$(basename "$PAK_DIR" .pak)"
# shellcheck source=pak/bin/common.sh
. "$PAK_DIR/bin/common.sh"
# shellcheck source=pak/bin/plugins.sh
. "$PAK_DIR/bin/plugins.sh"

mkdir -p "$LOGS_PATH" "$STATE_DIR"
exec >>"$LOGS_PATH/$PAK_NAME.txt" 2>&1
echo "$0 $*"

# minui-list and minui-presenter ship per platform.
export PATH="$PAK_DIR/bin/$PLATFORM:$PATH"

WORK_DIR="/tmp/$PAK_NAME"
mkdir -p "$WORK_DIR"

cleanup() {
	rm -rf "$WORK_DIR"
	killall minui-presenter >/dev/null 2>&1 || true
	# Both tools pause keymon while they own the screen. If one died early,
	# volume and brightness keys would stay dead until a reboot.
	killall -CONT keymon.elf >/dev/null 2>&1 || true
}
trap cleanup EXIT INT TERM

show_message() {
	minui-presenter --message "$1" --timeout "${2:-4}" --confirm-show --confirm-text "OK"
}

status_text() {
	if [ -f "$STATE_DIR/unsupported" ]; then
		echo "Waiting for a supported NextUI"
	elif ! is_enabled; then
		echo "Off - using the stock launcher"
	elif in_fallback; then
		echo "Paused after crashes - using stock"
	elif plugins_paused && [ -n "$(active_mode)" ]; then
		echo "On, plugins paused after a crash"
	elif [ -n "$(active_mode)" ]; then
		echo "On"
	else
		echo "On after the next reboot"
	fi
}

write_menu() {
	{
		echo '{"items": ['
		printf '{"name": "Status: %s", "features": {"unselectable": true}},\n' "$(status_text)"
		if in_fallback || plugins_paused; then
			echo '{"name": "Try again"},'
		fi
		if [ -n "$(plugin_dirs)" ]; then
			echo '{"name": "Plugins"},'
		fi
		if is_enabled; then
			echo '{"name": "Turn off"}'
		else
			echo '{"name": "Turn on"}'
		fi
		echo ']}'
	} >"$WORK_DIR/menu.json"
}

turn_on() {
	if ! version_supported; then
		show_message "This build supports a different NextUI release.\nInstalled: $(nextui_release_name)\nUpdate Launcher from the Pak Store." 0
		return
	fi

	echo "$PAK_DIR" >"$STATE_DIR/pak_dir"
	touch "$STATE_DIR/enabled"
	rm -f "$STATE_DIR/fallback" "$STATE_DIR/crashes" "$STATE_DIR/unsupported" \
		"$STATE_DIR/plugins_paused" "$STATE_DIR/loading"

	mkdir -p "$(dirname "$HOOK_STUB")"
	cp "$PAK_DIR/bin/hook-stub.sh" "$HOOK_STUB"
	chmod +x "$HOOK_STUB"

	if "$PAK_DIR/bin/boot.sh" mount; then
		show_message "Launcher is on.\nIt starts when you leave this screen."
	else
		show_message "Launcher could not start.\nThe stock launcher is still in use."
	fi
}

turn_off() {
	rm -f "$STATE_DIR/enabled"
	"$PAK_DIR/bin/boot.sh" unmount
	rm -f "$HOOK_STUB"
	show_message "Launcher is off.\nThe stock launcher is back."
}

# One row per plugin, "<name>: On" or "<name>: Off". selecting one flips it.
# The launcher reads the choices the next time it starts, which is when the
# user leaves this pak.
plugins_page() {
	while true; do
		rows="$WORK_DIR/plugin-rows"
		: >"$rows"
		{
			echo '{"items": ['
			first=1
			plugin_dirs | while IFS="$(printf '\t')" read -r dir bundled; do
				id="$(plugin_field "$dir" id)"
				name="$(json_text "$(plugin_field "$dir" name)")"
				[ -n "$id" ] || continue
				[ -n "$name" ] || name="$id"
				if plugin_enabled "$id" "$bundled"; then label="$name: On"; else label="$name: Off"; fi
				printf '%s\t%s\n' "$label" "$id" >>"$rows"
				[ "$first" = 1 ] || echo ','
				first=0
				printf '{"name": "%s"}' "$label"
			done
			echo ']}'
		} >"$WORK_DIR/plugins.json"

		rm -f "$WORK_DIR/selection"
		if ! minui-list --file "$WORK_DIR/plugins.json" --title "Plugins" \
			--write-location "$WORK_DIR/selection" --confirm-text "TOGGLE"; then
			return
		fi

		picked="$(cat "$WORK_DIR/selection" 2>/dev/null)"
		id="$(awk -F '\t' -v label="$picked" '$1 == label { print $2; exit }' "$rows")"
		[ -n "$id" ] || continue
		case "$picked" in
			*": On") plugin_set "$id" off ;;
			*) plugin_set "$id" on ;;
		esac
	done
}

try_again() {
	rm -f "$STATE_DIR/fallback" "$STATE_DIR/crashes" "$STATE_DIR/plugins_paused" "$STATE_DIR/loading"
	show_message "Crash guard reset.\nThe launcher and its plugins start when you leave this screen."
}

# Plugin paks open the Plugins page directly: launch.sh plugins
if [ "${1:-}" = "plugins" ]; then
	plugins_page
	exit 0
fi

while true; do
	write_menu
	rm -f "$WORK_DIR/selection"
	if ! minui-list --file "$WORK_DIR/menu.json" --title "Launcher" \
		--write-location "$WORK_DIR/selection" --confirm-text "SELECT"; then
		exit 0
	fi

	case "$(cat "$WORK_DIR/selection" 2>/dev/null)" in
		"Turn on") turn_on ;;
		"Turn off") turn_off ;;
		"Try again") try_again ;;
		"Plugins") plugins_page ;;
	esac
done
