#!/bin/sh
#
# Start the desktop build against the faux SD card, let it run for a few
# seconds, then stop it. Fails if it exits on its own before then, which is
# how a startup crash shows up (exit 1). If it's still running a few seconds
# after SIGTERM it gets SIGKILL and the run fails too (exit 2). Writes its log
# to $1 for comparing runs. Anything after the seconds is passed to nextui.elf.
#
# With SMOKE_UNTIL set (fixed strings, one per line), the seconds are the
# most it waits: it stops once the log has every line, and the file named by
# SMOKE_UNTIL_GONE (if any) is gone, then SMOKE_SETTLE more seconds (1 by
# default) pass. If that doesn't happen in time it stops the launcher and
# fails (exit 3). With SMOKE_STACKS set too, it first writes every thread's
# stack to <log file>.stacks with gdb, which needs permission to attach.
#
# With SMOKE_STACKS set, a launcher still running after SIGTERM also has
# its process tree (it and every process under it, with their command
# lines) and every thread's stack added to the end of its log before the
# SIGKILL. It still fails.
#
# SMOKE_PRELOAD names a library to preload into nextui.elf alone (LD_PRELOAD,
# or DYLD_INSERT_LIBRARIES on macOS, which drops DYLD_ variables on the way
# through /bin/sh and env, so it's set here).
#
#   ./scripts/desktop-smoke.sh build/smoke.log [seconds] [args...]

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOG="${1:?usage: $0 <log file> [seconds]}"
SECONDS_TO_RUN="${2:-4}"
STOP_SECONDS=5
UNTIL="${SMOKE_UNTIL:-}"
UNTIL_GONE="${SMOKE_UNTIL_GONE:-}"
PRELOAD="${SMOKE_PRELOAD:-}"
SETTLE="${SMOKE_SETTLE:-1}"
STACKS="${SMOKE_STACKS:-}"
shift
[ $# -gt 0 ] && shift

case "$(uname -s)" in
	Darwin)
		export DYLD_LIBRARY_PATH=/var/tmp/nextui/lib
		PRELOAD_VAR=DYLD_INSERT_LIBRARIES
		;;
	*)
		export LD_LIBRARY_PATH=/var/tmp/nextui/lib
		PRELOAD_VAR=LD_PRELOAD
		;;
esac

# missing: prints each SMOKE_UNTIL line the log doesn't have yet
missing() {
	printf '%s\n' "$UNTIL" | while IFS= read -r line; do
		[ -n "$line" ] && ! grep -qF -- "$line" "$LOG" && echo "$line"
	done
}

ready() {
	[ -n "$UNTIL" ] || return 1
	[ -n "$UNTIL_GONE" ] && [ -e "$UNTIL_GONE" ] && return 1
	[ -z "$(missing)" ]
}

# stacks: every thread's stack in the launcher, from gdb, which a minute
# ends where timeout is there, so a gdb that hangs can't hold up the run
stacks() {
	set -- gdb -p "$pid" -batch -ex "set pagination off" -ex "thread apply all bt"
	command -v timeout >/dev/null && set -- timeout 60 "$@"
	"$@" 2>&1 </dev/null
}

# tree: the launcher and every process under it, indented under its parent
tree() {
	ps -e -o pid= -o ppid= -o stat= -o args= | awk -v root="$pid" '
		{
			# pid ppid stat command line, one space apart
			args = $0
			sub(/^[ \t]*[^ \t]+[ \t]+[^ \t]+[ \t]+[^ \t]+[ \t]+/, "", args)
			line[$1] = $1 " " $2 " " $3 " " args
			kids[$2] = kids[$2] " " $1
		}
		function show(p, indent,    list, count, i) {
			print indent line[p]
			count = split(kids[p], list, " ")
			for (i = 1; i <= count; i++) show(list[i], indent "  ")
		}
		END { if (root in line) show(root, ""); else print root " is gone" }
	'
}

bash "$ROOT/vendor/NextUI/workspace/desktop/prepare_fake_sd_root.sh" >/dev/null
rm -f "$LOG.stopping"

if [ -n "$PRELOAD" ]; then
	# not through env, which macOS also strips DYLD_ variables from
	(
		export "$PRELOAD_VAR=$PRELOAD"
		exec "$ROOT/build/desktop/nextui.elf" "$@"
	) >"$LOG" 2>&1 &
else
	"$ROOT/build/desktop/nextui.elf" "$@" >"$LOG" 2>&1 &
fi
pid=$!

# poll, so an early exit or a ready log ends the wait
waited=0
is_ready=0
while kill -0 "$pid" 2>/dev/null && [ "$waited" -lt $((SECONDS_TO_RUN * 10)) ]; do
	if ready; then
		is_ready=1
		sleep "$SETTLE"
		break
	fi
	sleep 0.1
	waited=$((waited + 1))
done

if kill -0 "$pid" 2>/dev/null; then
	# where a start that isn't ready waits, before the SIGTERM ends it
	stacks=0
	if [ -n "$STACKS" ] && [ "$is_ready" -eq 0 ] && [ -n "$UNTIL" ] && ! ready; then
		stacks >"$LOG.stacks" && stacks=1
	fi
	kill "$pid"
	# a launcher that doesn't stop on SIGTERM would hold up whoever runs this
	tries=0
	while kill -0 "$pid" 2>/dev/null && [ "$tries" -lt $((STOP_SECONDS * 10)) ]; do
		sleep 0.1
		tries=$((tries + 1))
	done
	if kill -0 "$pid" 2>/dev/null; then
		# where it's stuck, kept aside until the SIGKILL stops its writes to the log
		if [ -n "$STACKS" ]; then
			{
				echo "processes:"
				tree
				echo
				echo "stacks:"
				stacks
			} >"$LOG.stopping"
		fi
		kill -KILL "$pid"
		wait "$pid" 2>/dev/null
		echo "still running ${STOP_SECONDS}s after SIGTERM, killed it; log in $LOG" >&2
		if [ -f "$LOG.stopping" ]; then
			{
				echo
				echo "--- still running ${STOP_SECONDS}s after SIGTERM"
				cat "$LOG.stopping"
			} >>"$LOG"
			rm -f "$LOG.stopping"
			echo "  processes and stacks at the end of the log" >&2
		fi
		exit 2
	fi
	wait "$pid" 2>/dev/null
	if [ "$is_ready" -eq 1 ]; then
		echo "ready, stopped it; log in $LOG"
		exit 0
	fi
	# check once more, in case the last line came in as the wait ran out
	if [ -n "$UNTIL" ] && ! ready; then
		echo "not ready after ${SECONDS_TO_RUN}s; log in $LOG" >&2
		missing | sed 's/^/  missing line: /' >&2
		[ -n "$UNTIL_GONE" ] && [ -e "$UNTIL_GONE" ] && echo "  still there: $UNTIL_GONE" >&2
		[ "$stacks" -eq 1 ] && echo "  stacks in $LOG.stacks" >&2
		exit 3
	fi
	echo "still running after ${SECONDS_TO_RUN}s; log in $LOG"
	exit 0
fi

wait "$pid"
status=$?
echo "exited early with status $status; log in $LOG" >&2
exit 1
