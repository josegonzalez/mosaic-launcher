#!/usr/bin/env bats

load test_helper

setup() {
	setup_fake_card
}

@test "does nothing while the launcher is off" {
	run "$PAK/bin/boot.sh" mount
	[ "$status" -eq 0 ]
	[ ! -f "$MOUNT_LOG" ]
	start_launcher
	[ "$(ran_last)" = stock ]
}

@test "leaves stock alone on an unsupported NextUI" {
	turn_on
	printf 'NextUI-20991231-0\nffffff0\n' >"$SDCARD_PATH/.system/version.txt"
	run "$PAK/bin/boot.sh" mount
	[ "$status" -eq 0 ]
	[ ! -f "$MOUNT_LOG" ]
	[ "$(cat "$STATE_DIR/unsupported")" = "NextUI-20991231-0" ]
	start_launcher
	[ "$(ran_last)" = stock ]
}

@test "bind mode keeps the stock binary reachable, then mounts the wrapper over it" {
	turn_on
	run "$PAK/bin/boot.sh" mount
	[ "$status" -eq 0 ]
	[ "$(sed -n 1p "$MOUNT_LOG")" = "mount --bind $STOCK_BIN $NEXTUI_LAUNCHER_RUN_DIR/nextui.stock.elf" ]
	[ "$(sed -n 2p "$MOUNT_LOG")" = "mount --bind $NEXTUI_LAUNCHER_RUN_DIR/wrapper.sh $STOCK_BIN" ]
	[ "$(cat "$NEXTUI_LAUNCHER_RUN_DIR/mode")" = bind ]
	[ ! -f "$SYSTEM_PATH/bin/nextui.stock.elf" ]
	start_launcher
	[ "$(ran_last)" = ours ]
}

@test "mounting twice in one boot doesn't stack mounts" {
	turn_on
	"$PAK/bin/boot.sh" mount
	"$PAK/bin/boot.sh" mount
	[ "$(wc -l <"$MOUNT_LOG" | tr -d ' ')" -eq 2 ]
}

@test "falls back to copy mode when bind mounts fail" {
	turn_on
	touch "$BATS_TEST_TMPDIR/mount.fail"
	run "$PAK/bin/boot.sh" mount
	[ "$status" -eq 0 ]
	[ "$(cat "$NEXTUI_LAUNCHER_RUN_DIR/mode")" = copy ]
	grep -q NEXTUI_LAUNCHER_WRAPPER "$STOCK_BIN"
	"$SYSTEM_PATH/bin/nextui.stock.elf"
	[ "$(ran_last)" = stock ]
	start_launcher
	[ "$(ran_last)" = ours ]
}

@test "copy mode on a later boot never treats the wrapper as stock" {
	turn_on
	touch "$BATS_TEST_TMPDIR/mount.fail"
	"$PAK/bin/boot.sh" mount
	rm -rf "$NEXTUI_LAUNCHER_RUN_DIR" "$BATS_TEST_TMPDIR/mount.fail"
	run "$PAK/bin/boot.sh" mount
	[ "$status" -eq 0 ]
	[ "$(cat "$NEXTUI_LAUNCHER_RUN_DIR/mode")" = copy ]
	"$SYSTEM_PATH/bin/nextui.stock.elf"
	[ "$(ran_last)" = stock ]
}

@test "copy mode backs up a stock binary that a NextUI update put back" {
	turn_on
	touch "$BATS_TEST_TMPDIR/mount.fail"
	"$PAK/bin/boot.sh" mount
	rm -rf "$NEXTUI_LAUNCHER_RUN_DIR"
	fake_elf "$STOCK_BIN" updated-stock
	"$PAK/bin/boot.sh" mount
	"$SYSTEM_PATH/bin/nextui.stock.elf"
	[ "$(ran_last)" = updated-stock ]
}

@test "unmount restores stock in bind mode" {
	turn_on
	"$PAK/bin/boot.sh" mount
	run "$PAK/bin/boot.sh" unmount
	[ "$status" -eq 0 ]
	[ ! -d "$NEXTUI_LAUNCHER_RUN_DIR" ]
	start_launcher
	[ "$(ran_last)" = stock ]
}

@test "unmount restores stock in copy mode" {
	turn_on
	touch "$BATS_TEST_TMPDIR/mount.fail"
	"$PAK/bin/boot.sh" mount
	run "$PAK/bin/boot.sh" unmount
	[ "$status" -eq 0 ]
	[ ! -f "$SYSTEM_PATH/bin/nextui.stock.elf" ]
	! grep -q NEXTUI_LAUNCHER_WRAPPER "$STOCK_BIN"
	start_launcher
	[ "$(ran_last)" = stock ]
}
