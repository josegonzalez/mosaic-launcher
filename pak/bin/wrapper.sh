#!/bin/sh
# NEXTUI_LAUNCHER_WRAPPER
#
# Stands in for $SYSTEM_PATH/bin/nextui.elf. MinUI.pak/launch.sh starts it in a
# loop, and each start picks this pak's launcher or the stock one.
#
# boot.sh copies this file out of the pak before using it, so it keeps working
# after the pak is uninstalled mid-session. It must not source anything from
# the pak for the same reason.

RUN_DIR="${NEXTUI_LAUNCHER_RUN_DIR:-/tmp/nextui-launcher}"
STATE_DIR="$USERDATA_PATH/launcher"

stock="$RUN_DIR/nextui.stock.elf"
[ -f "$RUN_DIR/stock_path" ] && stock="$(cat "$RUN_DIR/stock_path")"
[ -x "$stock" ] || stock="$SYSTEM_PATH/bin/nextui.stock.elf"

pak_dir=""
[ -f "$RUN_DIR/pak_dir" ] && pak_dir="$(cat "$RUN_DIR/pak_dir")"
ours="$pak_dir/bin/$PLATFORM/nextui.elf"

run_stock() {
	exec "$stock" "$@"
}

[ -n "$pak_dir" ] && [ -x "$ours" ] || run_stock "$@"
[ -f "$STATE_DIR/enabled" ] || run_stock "$@"
[ -f "$STATE_DIR/fallback" ] && run_stock "$@"

"$ours" "$@"
status=$?

# A normal exit is 0, including every game or pak launch. SIGTERM and
# SIGKILL come from shutdown, not from a bug, so they don't count. Anything
# else is a crash, and two in a row hands the session back to stock.
case "$status" in
	0 | 137 | 143)
		[ -f "$STATE_DIR/crashes" ] && rm -f "$STATE_DIR/crashes"
		;;
	*)
		crashes=0
		[ -f "$STATE_DIR/crashes" ] && crashes="$(cat "$STATE_DIR/crashes")"
		crashes=$((crashes + 1))
		if [ "$crashes" -ge 2 ]; then
			touch "$STATE_DIR/fallback"
			rm -f "$STATE_DIR/crashes"
		else
			echo "$crashes" >"$STATE_DIR/crashes"
		fi
		;;
esac

exit "$status"
