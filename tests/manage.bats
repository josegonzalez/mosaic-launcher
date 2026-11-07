#!/usr/bin/env bats

# What the settings screens ask manage.sh to do, and what launch.sh starts.

load test_helper

setup() {
	setup_fake_card
	MANAGE="$PAK/bin/manage.sh"
}

status_of() {
	"$MANAGE" status | sed -n "s/^$1=//p"
}

@test "turn-on mounts the launcher and installs the boot hook" {
	touch "$STATE_DIR/fallback" "$STATE_DIR/crashes" "$STATE_DIR/plugins_paused" "$STATE_DIR/loading"
	run "$MANAGE" turn-on
	[ "$status" -eq 0 ]
	[ -f "$STATE_DIR/enabled" ]
	[ "$(cat "$STATE_DIR/pak_dir")" = "$PAK" ]
	[ -x "$USERDATA_PATH/.hooks/boot.d/00-launcher.sync.sh" ]
	grep -q "mount --bind" "$MOUNT_LOG"
	[ ! -e "$STATE_DIR/fallback" ]
	[ ! -e "$STATE_DIR/crashes" ]
	[ ! -e "$STATE_DIR/plugins_paused" ]
	[ ! -e "$STATE_DIR/loading" ]
}

@test "turn-on refuses an unsupported NextUI" {
	printf 'NextUI-20990101-0\nabcdef1\n' >"$SDCARD_PATH/.system/version.txt"
	run "$MANAGE" turn-on
	[ "$status" -eq 3 ]
	[ ! -f "$STATE_DIR/enabled" ]
	[ ! -f "$MOUNT_LOG" ]
}

@test "turn-on falls back to copy mode when bind mounts fail" {
	touch "$BATS_TEST_TMPDIR/mount.fail"
	run "$MANAGE" turn-on
	[ "$status" -eq 0 ]
	[ "$(status_of status)" = on ]
	start_launcher
	[ "$(ran_last)" = ours ]
}

@test "turn-off unmounts and removes the boot hook" {
	"$MANAGE" turn-on
	run "$MANAGE" turn-off
	[ "$status" -eq 0 ]
	[ ! -f "$STATE_DIR/enabled" ]
	[ ! -e "$USERDATA_PATH/.hooks/boot.d/00-launcher.sync.sh" ]
	grep -q "^umount" "$MOUNT_LOG"
	start_launcher
	[ "$(ran_last)" = stock ]
}

@test "try-again clears the crash guard" {
	turn_on
	touch "$STATE_DIR/fallback" "$STATE_DIR/crashes" "$STATE_DIR/plugins_paused" "$STATE_DIR/loading"
	run "$MANAGE" try-again
	[ "$status" -eq 0 ]
	[ ! -e "$STATE_DIR/fallback" ]
	[ ! -e "$STATE_DIR/crashes" ]
	[ ! -e "$STATE_DIR/plugins_paused" ]
	[ ! -e "$STATE_DIR/loading" ]
	[ -f "$STATE_DIR/enabled" ]
}

@test "status follows the launcher through its states" {
	[ "$(status_of status)" = off ]
	[ "$(status_of enabled)" = 0 ]
	[ "$(status_of release)" = NextUI-20261013-0 ]

	turn_on
	[ "$(status_of status)" = pending ] # on, but not mounted this boot
	[ "$(status_of enabled)" = 1 ]

	"$MANAGE" turn-on
	[ "$(status_of status)" = on ]
	[ "$(status_of try_again)" = 0 ]

	touch "$STATE_DIR/plugins_paused"
	[ "$(status_of status)" = paused ]
	[ "$(status_of try_again)" = 1 ]

	touch "$STATE_DIR/fallback"
	[ "$(status_of status)" = fallback ]

	touch "$STATE_DIR/unsupported"
	[ "$(status_of status)" = unsupported ]
}

@test "an unknown command is refused" {
	run "$MANAGE" frobnicate
	[ "$status" -eq 2 ]
}

@test "launch.sh starts the settings screens, passing its arguments" {
	cat >"$PAK/bin/$PLATFORM/nextui.elf" <<EOF
#!/bin/sh
echo "\$*" >"$BATS_TEST_TMPDIR/args"
EOF
	run "$PAK/launch.sh"
	[ "$status" -eq 0 ]
	[ "$(cat "$BATS_TEST_TMPDIR/args")" = "--settings" ]
	run "$PAK/launch.sh" plugins
	[ "$(cat "$BATS_TEST_TMPDIR/args")" = "--settings plugins" ]
}
