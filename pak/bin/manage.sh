#!/bin/sh
# Turns the launcher on or off and resets its crash guard. The settings
# screens (nextui.elf --settings) call this for anything that mounts,
# unmounts or edits the boot hook; it's also handy over SSH.
#
#   manage.sh turn-on     0 on, 3 this NextUI isn't supported, 1 couldn't start
#   manage.sh turn-off
#   manage.sh try-again   clears the crash guard and paused plugins
#   manage.sh status      prints key=value lines: status (unsupported, off,
#                         fallback, paused, on or pending), enabled,
#                         try_again and release

PAK_DIR="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=pak/bin/common.sh
. "$PAK_DIR/bin/common.sh"

turn_on() {
	version_supported || return 3

	mkdir -p "$STATE_DIR"
	echo "$PAK_DIR" >"$STATE_DIR/pak_dir"
	touch "$STATE_DIR/enabled"
	rm -f "$STATE_DIR/fallback" "$STATE_DIR/crashes" "$STATE_DIR/unsupported" \
		"$STATE_DIR/plugins_paused" "$STATE_DIR/loading"

	mkdir -p "$(dirname "$HOOK_STUB")"
	cp "$PAK_DIR/bin/hook-stub.sh" "$HOOK_STUB"
	chmod +x "$HOOK_STUB"

	"$PAK_DIR/bin/boot.sh" mount || return 1
}

turn_off() {
	rm -f "$STATE_DIR/enabled"
	"$PAK_DIR/bin/boot.sh" unmount
	rm -f "$HOOK_STUB"
}

try_again() {
	rm -f "$STATE_DIR/fallback" "$STATE_DIR/crashes" "$STATE_DIR/plugins_paused" "$STATE_DIR/loading"
}

status() {
	if [ -f "$STATE_DIR/unsupported" ]; then
		state=unsupported
	elif ! is_enabled; then
		state=off
	elif in_fallback; then
		state=fallback
	elif plugins_paused && [ -n "$(active_mode)" ]; then
		state=paused
	elif [ -n "$(active_mode)" ]; then
		state=on
	else
		state=pending
	fi
	echo "status=$state"
	if is_enabled; then echo "enabled=1"; else echo "enabled=0"; fi
	if in_fallback || plugins_paused; then echo "try_again=1"; else echo "try_again=0"; fi
	echo "release=$(nextui_release_name)"
}

case "${1:-}" in
	turn-on) turn_on ;;
	turn-off) turn_off ;;
	try-again) try_again ;;
	status) status ;;
	*)
		echo "usage: $0 turn-on|turn-off|try-again|status" >&2
		exit 2
		;;
esac
