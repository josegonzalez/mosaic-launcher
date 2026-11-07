#!/bin/sh
# Puts the wrapper in front of the stock launcher, or takes it away.
#
#   boot.sh mount     called by the boot hook stub and the manager
#   boot.sh unmount   called by the manager when the launcher is turned off
#
# Nothing in .system/ changes in bind mode: the stock binary is bind-mounted
# somewhere reachable, then the wrapper is bind-mounted over it. Copy mode is
# the fallback for devices that refuse file bind mounts.

PAK_DIR="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck source=pak/bin/common.sh
. "$PAK_DIR/bin/common.sh"

log() {
	echo "boot.sh: $*"
}

prepare_run_dir() {
	mkdir -p "$RUN_DIR"
	cp "$PAK_DIR/bin/wrapper.sh" "$RUN_DIR/wrapper.sh"
	chmod +x "$RUN_DIR/wrapper.sh"
	echo "$PAK_DIR" >"$RUN_DIR/pak_dir"
}

mount_bind() {
	touch "$RUN_DIR/nextui.stock.elf"
	if ! "$MOUNT" --bind "$STOCK_BIN" "$RUN_DIR/nextui.stock.elf"; then
		rm -f "$RUN_DIR/nextui.stock.elf"
		return 1
	fi
	if ! "$MOUNT" --bind "$RUN_DIR/wrapper.sh" "$STOCK_BIN"; then
		"$UMOUNT" "$RUN_DIR/nextui.stock.elf"
		rm -f "$RUN_DIR/nextui.stock.elf"
		return 1
	fi
	echo "$RUN_DIR/nextui.stock.elf" >"$RUN_DIR/stock_path"
	echo bind >"$RUN_DIR/mode"
}

mount_copy() {
	# A NextUI update puts a fresh stock binary back in place, so only back up
	# what's there when it isn't our wrapper.
	if ! grep -q "$WRAPPER_MARKER" "$STOCK_BIN" 2>/dev/null; then
		cp "$STOCK_BIN" "$STOCK_BACKUP" || return 1
	fi
	cp "$RUN_DIR/wrapper.sh" "$STOCK_BIN" || return 1
	chmod +x "$STOCK_BIN"
	echo "$STOCK_BACKUP" >"$RUN_DIR/stock_path"
	echo copy >"$RUN_DIR/mode"
}

do_mount() {
	if [ ! -x "$OUR_BIN" ]; then
		log "no launcher binary for $PLATFORM, leaving stock in place"
		return 0
	fi
	if ! is_enabled; then
		log "launcher is off, leaving stock in place"
		return 0
	fi
	if ! version_supported; then
		log "NextUI $(nextui_release_name) ($(nextui_build_hash)) is not supported by this build"
		mkdir -p "$STATE_DIR"
		nextui_release_name >"$STATE_DIR/unsupported"
		return 0
	fi
	rm -f "$STATE_DIR/unsupported"

	prepare_run_dir
	case "$(active_mode)" in
		bind)
			# Already mounted this boot. prepare_run_dir refreshed the wrapper
			# and pak path in case the pak was updated since.
			return 0
			;;
		copy)
			# Installed by copy on an earlier boot. The binary in .system is
			# our wrapper, so it must never be bind-mounted as "stock".
			mount_copy
			return
			;;
	esac

	if mount_bind; then
		log "active (bind mount)"
	elif mount_copy; then
		log "active (copy, bind mounts unavailable)"
	else
		log "could not activate, leaving stock in place"
		rm -rf "$RUN_DIR"
		return 1
	fi
}

do_unmount() {
	case "$(active_mode)" in
		bind)
			"$UMOUNT" "$STOCK_BIN"
			"$UMOUNT" "$RUN_DIR/nextui.stock.elf"
			;;
		copy)
			if [ -f "$STOCK_BACKUP" ]; then
				mv -f "$STOCK_BACKUP" "$STOCK_BIN"
			fi
			;;
	esac
	rm -rf "$RUN_DIR"
	log "stock launcher restored"
}

case "${1:-mount}" in
	mount) do_mount ;;
	unmount) do_unmount ;;
	*)
		echo "usage: $0 [mount|unmount]" >&2
		exit 2
		;;
esac
