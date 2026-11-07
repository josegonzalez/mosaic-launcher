#!/usr/bin/env bats

# scripts/desktop-smoke.sh against fake launchers, in a copy of the repo
# layout so the shared fake card isn't touched.

REPO_ROOT="$(cd "$BATS_TEST_DIRNAME/.." && pwd)"

setup() {
	ROOT="$BATS_TEST_TMPDIR/repo"
	mkdir -p "$ROOT/scripts" "$ROOT/build/desktop" "$ROOT/vendor/NextUI/workspace/desktop"
	cp "$REPO_ROOT/scripts/desktop-smoke.sh" "$ROOT/scripts/"
	printf '#!/bin/sh\n' >"$ROOT/vendor/NextUI/workspace/desktop/prepare_fake_sd_root.sh"
	LOG="$BATS_TEST_TMPDIR/smoke.log"
}

# fake_launcher <body>: the nextui.elf the script starts
fake_launcher() {
	printf '#!/bin/sh\n%s\n' "$1" >"$ROOT/build/desktop/nextui.elf"
	chmod +x "$ROOT/build/desktop/nextui.elf"
}

smoke() {
	"$ROOT/scripts/desktop-smoke.sh" "$LOG" 1 "$@"
}

@test "a launcher that stops on SIGTERM passes" {
	fake_launcher 'trap "exit 0" TERM; echo "started $*"; while :; do sleep 0.1; done'
	run smoke --settings
	[ "$status" -eq 0 ]
	[ "$output" = "still running after 1s; log in $LOG" ]
	[ "$(cat "$LOG")" = "started --settings" ]
}

@test "a launcher that exits on its own fails" {
	fake_launcher 'exit 3'
	run smoke
	[ "$status" -eq 1 ]
	[ "$output" = "exited early with status 3; log in $LOG" ]
}

@test "a launcher that ignores SIGTERM is killed and fails" {
	fake_launcher "trap '' TERM; echo \$\$ >'$BATS_TEST_TMPDIR/pid'; while :; do sleep 0.1; done"
	start=$(date +%s)
	run smoke
	[ "$status" -eq 2 ]
	[ "$output" = "still running 5s after SIGTERM, killed it; log in $LOG" ]
	[ $(($(date +%s) - start)) -lt 15 ]
	! kill -0 "$(cat "$BATS_TEST_TMPDIR/pid")" 2>/dev/null
}
