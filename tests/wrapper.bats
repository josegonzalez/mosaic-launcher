#!/usr/bin/env bats

load test_helper

setup() {
	setup_fake_card
	turn_on
	"$PAK/bin/boot.sh" mount
}

@test "runs our launcher when it's on" {
	start_launcher
	[ "$(ran_last)" = ours ]
}

@test "runs stock as soon as the launcher is turned off" {
	rm -f "$STATE_DIR/enabled"
	start_launcher
	[ "$(ran_last)" = stock ]
}

@test "runs stock when the pak is uninstalled mid-session" {
	rm -rf "$PAK"
	start_launcher
	[ "$(ran_last)" = stock ]
}

@test "passes our launcher's exit code through" {
	echo 3 >"$BATS_TEST_TMPDIR/ours.exit"
	run start_launcher
	[ "$status" -eq 3 ]
}

@test "two crashes in a row hand the session back to stock" {
	echo 139 >"$BATS_TEST_TMPDIR/ours.exit"
	start_launcher || true
	[ ! -f "$STATE_DIR/fallback" ]
	start_launcher || true
	[ -f "$STATE_DIR/fallback" ]
	start_launcher
	[ "$(ran_last)" = stock ]
}

@test "a clean exit resets the crash count" {
	echo 139 >"$BATS_TEST_TMPDIR/ours.exit"
	start_launcher || true
	rm "$BATS_TEST_TMPDIR/ours.exit"
	start_launcher
	[ ! -f "$STATE_DIR/crashes" ]
	echo 139 >"$BATS_TEST_TMPDIR/ours.exit"
	start_launcher || true
	[ ! -f "$STATE_DIR/fallback" ]
}

@test "shutdown signals don't count as crashes" {
	echo 143 >"$BATS_TEST_TMPDIR/ours.exit"
	start_launcher || true
	echo 137 >"$BATS_TEST_TMPDIR/ours.exit"
	start_launcher || true
	[ ! -f "$STATE_DIR/fallback" ]
	[ ! -f "$STATE_DIR/crashes" ]
}

@test "a pak update takes effect on the next start" {
	fake_elf "$PAK/bin/$PLATFORM/nextui.elf" ours-v2
	start_launcher
	[ "$(ran_last)" = ours-v2 ]
}
