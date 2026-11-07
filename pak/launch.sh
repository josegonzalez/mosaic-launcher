#!/bin/sh
# Launcher.pak - turns the plugin-capable launcher on or off.
#
# The launcher itself takes over from stock nextui.elf through boot.sh. This
# script is only the settings screen the user reaches from Tools.

PAK_DIR="$(cd "$(dirname "$0")" && pwd)"
PAK_NAME="$(basename "$PAK_DIR" .pak)"
# shellcheck source=pak/bin/common.sh
. "$PAK_DIR/bin/common.sh"

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
		if in_fallback; then
			echo '{"name": "Try again"},'
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
	rm -f "$STATE_DIR/fallback" "$STATE_DIR/crashes" "$STATE_DIR/unsupported"

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

try_again() {
	rm -f "$STATE_DIR/fallback" "$STATE_DIR/crashes"
	show_message "Crash guard reset.\nThe launcher starts when you leave this screen."
}

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
	esac
done
