#!/bin/sh
#
# Start the desktop build against the faux SD card, let it run for a few
# seconds, then stop it. Fails if it exits on its own before then, which is
# how a startup crash shows up (exit 1). If it's still running a few seconds
# after SIGTERM it gets SIGKILL and the run fails too (exit 2). Writes its log
# to $1 for comparing runs. Anything after the seconds is passed to nextui.elf.
#
#   ./scripts/desktop-smoke.sh build/smoke.log [seconds] [args...]

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOG="${1:?usage: $0 <log file> [seconds]}"
SECONDS_TO_RUN="${2:-4}"
STOP_SECONDS=5
shift
[ $# -gt 0 ] && shift

case "$(uname -s)" in
	Darwin) export DYLD_LIBRARY_PATH=/var/tmp/nextui/lib ;;
	*) export LD_LIBRARY_PATH=/var/tmp/nextui/lib ;;
esac

bash "$ROOT/vendor/NextUI/workspace/desktop/prepare_fake_sd_root.sh" >/dev/null

"$ROOT/build/desktop/nextui.elf" "$@" >"$LOG" 2>&1 &
pid=$!
sleep "$SECONDS_TO_RUN"

if kill -0 "$pid" 2>/dev/null; then
	kill "$pid"
	# a launcher that doesn't stop on SIGTERM would hold up whoever runs this
	tries=0
	while kill -0 "$pid" 2>/dev/null && [ "$tries" -lt $((STOP_SECONDS * 10)) ]; do
		sleep 0.1
		tries=$((tries + 1))
	done
	if kill -0 "$pid" 2>/dev/null; then
		kill -KILL "$pid"
		wait "$pid" 2>/dev/null
		echo "still running ${STOP_SECONDS}s after SIGTERM, killed it; log in $LOG" >&2
		exit 2
	fi
	wait "$pid" 2>/dev/null
	echo "still running after ${SECONDS_TO_RUN}s; log in $LOG"
	exit 0
fi

wait "$pid"
status=$?
echo "exited early with status $status; log in $LOG" >&2
exit 1
