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

@test "with SMOKE_UNTIL it stops once the log has every line" {
	fake_launcher 'trap "exit 0" TERM; echo one; sleep 0.3; echo two; while :; do sleep 0.1; done'
	start=$(date +%s)
	SMOKE_UNTIL="$(printf 'one\ntwo')" SMOKE_SETTLE=0 run "$ROOT/scripts/desktop-smoke.sh" "$LOG" 30
	[ "$status" -eq 0 ]
	[ "$output" = "ready, stopped it; log in $LOG" ]
	[ $(($(date +%s) - start)) -lt 10 ]
}

@test "with SMOKE_UNTIL_GONE it also waits for the file to go" {
	marker="$BATS_TEST_TMPDIR/loading"
	touch "$marker"
	fake_launcher "trap 'exit 0' TERM; echo one; sleep 1; rm '$marker'; echo gone; while :; do sleep 0.1; done"
	SMOKE_UNTIL=one SMOKE_UNTIL_GONE="$marker" SMOKE_SETTLE=0 run "$ROOT/scripts/desktop-smoke.sh" "$LOG" 30
	[ "$status" -eq 0 ]
	grep -q gone "$LOG"
}

@test "with SMOKE_UNTIL a launcher that never logs the lines fails" {
	fake_launcher 'trap "exit 0" TERM; echo one; while :; do sleep 0.1; done'
	SMOKE_UNTIL="$(printf 'one\ntwo')" run "$ROOT/scripts/desktop-smoke.sh" "$LOG" 1
	[ "$status" -eq 3 ]
	[ "${lines[0]}" = "not ready after 1s; log in $LOG" ]
	[ "${lines[1]}" = "  missing line: two" ]
	[ "${#lines[@]}" -eq 2 ]
}

# fake_gdb: a gdb on PATH that logs how it was run
fake_gdb() {
	mkdir -p "$BATS_TEST_TMPDIR/bin"
	printf '#!/bin/sh\necho "gdb $*" >>"%s"\necho "Thread 1 (main)"\n' "$BATS_TEST_TMPDIR/gdb-calls" >"$BATS_TEST_TMPDIR/bin/gdb"
	chmod +x "$BATS_TEST_TMPDIR/bin/gdb"
	PATH="$BATS_TEST_TMPDIR/bin:$PATH"
}

@test "with SMOKE_STACKS a launcher that isn't ready has its stacks taken" {
	fake_gdb
	fake_launcher 'trap "exit 0" TERM; echo one; while :; do sleep 0.1; done'
	SMOKE_UNTIL="$(printf 'one\ntwo')" SMOKE_STACKS=1 run "$ROOT/scripts/desktop-smoke.sh" "$LOG" 1
	[ "$status" -eq 3 ]
	[ "${lines[1]}" = "  missing line: two" ]
	[ "${lines[2]}" = "  stacks in $LOG.stacks" ]
	[ "$(cat "$LOG.stacks")" = "Thread 1 (main)" ]
	grep -q -- "-p [0-9]" "$BATS_TEST_TMPDIR/gdb-calls"
}

@test "with SMOKE_STACKS a ready launcher has no stacks taken" {
	fake_gdb
	fake_launcher 'trap "exit 0" TERM; echo one; while :; do sleep 0.1; done'
	SMOKE_UNTIL=one SMOKE_STACKS=1 SMOKE_SETTLE=0 run "$ROOT/scripts/desktop-smoke.sh" "$LOG" 30
	[ "$status" -eq 0 ]
	[ ! -e "$LOG.stacks" ]
	[ ! -e "$BATS_TEST_TMPDIR/gdb-calls" ]
}

@test "with SMOKE_UNTIL a launcher that exits after the lines fails" {
	fake_launcher 'echo one; sleep 0.3; exit 3'
	SMOKE_UNTIL=one SMOKE_SETTLE=2 run "$ROOT/scripts/desktop-smoke.sh" "$LOG" 30
	[ "$status" -eq 1 ]
	[ "$output" = "exited early with status 3; log in $LOG" ]
}
