# Shared setup for the pak script tests. Builds a fake SD card in the test's
# temp dir, with a stock launcher, an installed Launcher.pak, and fake
# mount/umount that emulate what a bind mount looks like from outside.

REPO_ROOT="$(cd "$BATS_TEST_DIRNAME/.." && pwd)"
PINNED_HASH="39a06dea0123456789abcdef0123456789abcdef"

setup_fake_card() {
	export SDCARD_PATH="$BATS_TEST_TMPDIR/sdcard"
	export PLATFORM=testplat
	export SYSTEM_PATH="$SDCARD_PATH/.system/$PLATFORM"
	export USERDATA_PATH="$SDCARD_PATH/.userdata/$PLATFORM"
	export LOGS_PATH="$USERDATA_PATH/logs"
	export MOSAIC_LAUNCHER_RUN_DIR="$BATS_TEST_TMPDIR/run"
	STATE_DIR="$USERDATA_PATH/launcher"
	STOCK_BIN="$SYSTEM_PATH/bin/nextui.elf"
	RAN="$BATS_TEST_TMPDIR/ran"

	mkdir -p "$SYSTEM_PATH/bin" "$USERDATA_PATH" "$STATE_DIR"
	fake_elf "$STOCK_BIN" stock

	PAK="$SDCARD_PATH/Tools/$PLATFORM/Launcher.pak"
	mkdir -p "$PAK"
	cp -R "$REPO_ROOT/pak/." "$PAK/"
	mkdir -p "$PAK/bin/$PLATFORM"
	fake_elf "$PAK/bin/$PLATFORM/nextui.elf" ours
	echo "$PINNED_HASH" >"$PAK/supported-versions.txt"
	printf 'NextUI-20261013-0\n39a06de\n' >"$SDCARD_PATH/.system/version.txt"

	setup_fake_mount
}

# A launcher that records that it ran and exits with the code in
# $BATS_TEST_TMPDIR/<name>.exit, or 0.
fake_elf() {
	cat >"$1" <<EOF
#!/bin/sh
echo "$2" >>"$BATS_TEST_TMPDIR/ran"
code=0
[ -f "$BATS_TEST_TMPDIR/$2.exit" ] && code="\$(cat "$BATS_TEST_TMPDIR/$2.exit")"
exit "\$code"
EOF
	chmod +x "$1"
}

# mount --bind SRC DST stashes DST and copies SRC over it, keeping SRC's mode,
# which is what a bind mount looks like to anything reading DST. umount DST
# puts the stash back. Touching $BATS_TEST_TMPDIR/mount.fail makes mount fail.
setup_fake_mount() {
	MOUNT_LOG="$BATS_TEST_TMPDIR/mounts.log"
	local fake="$BATS_TEST_TMPDIR/fakebin"
	mkdir -p "$fake" "$BATS_TEST_TMPDIR/stash"
	cat >"$fake/mount" <<EOF
#!/bin/sh
echo "mount \$*" >>"$MOUNT_LOG"
[ -f "$BATS_TEST_TMPDIR/mount.fail" ] && exit 32
[ "\$1" = "--bind" ] || exit 1
key="\$(echo "\$3" | cksum | cut -d' ' -f1)"
cp -p "\$3" "$BATS_TEST_TMPDIR/stash/\$key"
cp -p "\$2" "\$3"
EOF
	cat >"$fake/umount" <<EOF
#!/bin/sh
echo "umount \$*" >>"$MOUNT_LOG"
key="\$(echo "\$1" | cksum | cut -d' ' -f1)"
cp -p "$BATS_TEST_TMPDIR/stash/\$key" "\$1"
rm -f "$BATS_TEST_TMPDIR/stash/\$key"
EOF
	chmod +x "$fake/mount" "$fake/umount"
	export MOSAIC_LAUNCHER_MOUNT="$fake/mount"
	export MOSAIC_LAUNCHER_UMOUNT="$fake/umount"
}

turn_on() {
	echo "$PAK" >"$STATE_DIR/pak_dir"
	touch "$STATE_DIR/enabled"
}

# What launch.sh does on each pass of its loop.
start_launcher() {
	"$STOCK_BIN"
}

ran_last() {
	tail -n 1 "$RAN"
}
