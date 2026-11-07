#!/bin/sh
# Shared by boot.sh and launch.sh. Expects PAK_DIR to be set, plus the
# environment MinUI.pak/launch.sh exports (SDCARD_PATH, SYSTEM_PATH,
# USERDATA_PATH, PLATFORM).
# shellcheck disable=SC2034 # sourced; the callers use these

# Persistent state, on the SD card. Pak Store updates and uninstalls never
# touch .userdata, so this survives both.
STATE_DIR="$USERDATA_PATH/launcher"

# Per-boot state. tmpfs, so a reboot always starts from stock.
RUN_DIR="${NEXTUI_LAUNCHER_RUN_DIR:-/tmp/nextui-launcher}"

STOCK_BIN="$SYSTEM_PATH/bin/nextui.elf"
STOCK_BACKUP="$SYSTEM_PATH/bin/nextui.stock.elf"
HOOK_STUB="$USERDATA_PATH/.hooks/boot.d/00-launcher.sync.sh"
VERSION_FILE="$SDCARD_PATH/.system/version.txt"
SUPPORTED_FILE="$PAK_DIR/supported-versions.txt"
OUR_BIN="$PAK_DIR/bin/$PLATFORM/nextui.elf"

# Tests swap these for fakes; macOS has no bind mounts.
MOUNT="${NEXTUI_LAUNCHER_MOUNT:-mount}"
UMOUNT="${NEXTUI_LAUNCHER_UMOUNT:-umount}"

# Every copy of wrapper.sh contains this string, which is how copy mode tells
# our wrapper apart from a stock binary that a NextUI update put back.
WRAPPER_MARKER="NEXTUI_LAUNCHER_WRAPPER"

# The build hash of the installed NextUI, from the second line of version.txt.
nextui_build_hash() {
	[ -f "$VERSION_FILE" ] || return 0
	sed -n 2p "$VERSION_FILE" | tr -d '[:space:]'
}

# The release name of the installed NextUI, from the first line of version.txt.
nextui_release_name() {
	[ -f "$VERSION_FILE" ] || return 0
	sed -n 1p "$VERSION_FILE" | tr -d '\r'
}

# version.txt holds a short hash and supported-versions.txt holds full
# hashes, so a supported line has to start with the installed hash.
version_supported() {
	hash="$(nextui_build_hash)"
	case "$hash" in
		"" | *[!0-9a-f]*) return 1 ;;
	esac
	[ "${#hash}" -ge 7 ] || return 1
	[ -f "$SUPPORTED_FILE" ] || return 1
	grep -q "^$hash" "$SUPPORTED_FILE"
}

is_enabled() {
	[ -f "$STATE_DIR/enabled" ]
}

# Set by wrapper.sh after two crashes in a row. Cleared by turning the
# launcher on again from the manager.
in_fallback() {
	[ -f "$STATE_DIR/fallback" ]
}

# Set by the launcher when it crashed while starting with plugins loaded. It
# keeps running, without plugins, until the user tries again.
plugins_paused() {
	[ -f "$STATE_DIR/plugins_paused" ]
}

# bind, copy, or empty when nothing is active this boot.
active_mode() {
	if [ -f "$RUN_DIR/mode" ]; then
		cat "$RUN_DIR/mode"
	elif grep -q "$WRAPPER_MARKER" "$STOCK_BIN" 2>/dev/null; then
		echo copy
	fi
}
