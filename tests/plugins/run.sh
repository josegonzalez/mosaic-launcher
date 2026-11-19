#!/bin/sh
#
# Loads test plugins into the desktop build and checks what happened:
# a plugin that loads and uses the host table, one with the wrong API
# version, one that replaces the root and opens entries, one that sorts
# and renames a folder and moves the selection, one that adds quick menu
# tiles and shares a button, one that declines after registering things,
# one that powers off, one that sets stock toggles, one that reads theme
# colours and sets an LED colour, one that asks about Bluetooth, one that
# reads the Wi-Fi state, one that
# draws, one that writes pixels into its surfaces, one that draws the game list as a grid, two that decorate rows and
# add header widgets, one that decorates the game's art, two that show their own surfaces in place of it, one that shows its own in the bundled grid and fancy layouts' art, one that plays audio and one refused the device meanwhile, one that reads back the dots the bundled row-badges
# puts on rows, two that supply art and load it through the art
# cache, one that hears about the selection, one that reads .info
# sidecars, one that reads the play history, one that reads local
# RetroAchievements progress, one that picks transition
# styles, one that generates art, one that draws the background, one that
# looks at the bundled box art provider's art, one that reads the bundled
# grid, carousel, hero rail and shelf layouts' cells back, one that looks
# at the bundled placeholder art plugin's cards, one that hides entries'
# names, one that reads the bundled art backdrop plugin's background and
# the hero rail's hero back, one that reads the bundled header indicators
# back, one that reads the bundled fancy layout's art panel back, one that
# reads the bundled play time captions back, one that reads the bundled
# metadata band back, one that launches a game past the bundled play time
# limit, one that lists the bundled collections plus plugin's collections,
# two that filter input, pressing buttons themselves, one that reads the
# bundled launch animation's frames back, one that reads the bundled
# transition styles' frames back, one that presses the bundled hold
# lock's chord, one that walks the selection through the bundled video
# previews' clips, one with screens of its own, which also owns the home
# and switcher slots, draws stock's button hints and presses the bundled
# game switcher plus's buttons and the bundled screenshot viewer's, and
# one with an overlay, one that types a query into the bundled search plugin and reads its
# results back, one that translates the launcher's strings, one that
# supplies the theme and reads it back, one that supplies fallback fonts
# and reads its text back, one that captures and saves the screen, one
# that picks the hint style and draws single buttons, one that lists
# stock's palettes and reads the theme colours back, one that reads the
# bundled favorites and hidden plugins' entry sets, two that publish and
# subscribe to events, one that calls the bundled providers' services and
# two that provide their own, one that reads the keys the bundled theme
# plus passes on with its theme, one that opens a toast, a dialog and the
# PIN keyboard for the bundled UI sounds plugin to play sounds for, one
# that reads the bundled blue light tint back off the screen, one that
# opens and dismisses the bundled smart collections' rule collections,
# and one that crashes
# during startup. A preloaded library,
# presser, drives the Launcher pak's Plugins page through the bundled play
# time limit's protected settings and scrolls a long one, reads back those
# hints, and drives the
# bundled home dashboard's tabs and modes, the bundled tags plugin's
# checklist and the bundled theme plus's theme picker. It also checks the status pill shell
# paks' status files show.
#
# Each case waits up to PLUGIN_TEST_SECONDS (10 by default) for the log
# lines it checks, and stops as soon as they're in, so a slow machine only
# makes the run slower.
#
# The cases are in tests/plugins/cases/, run in name order with the helpers
# here; each prints how long it took. A case with a "# shard: all" line runs
# everywhere: in every shard and every worker. A case with an
# "# after: <case>" line uses what that case left (a log, a build) and
# always runs with it, after it; CASES adds the cases they're after. A case
# with an "# alone" line counts frames or times audio, which misses when
# other launchers share the CPUs, so with JOBS it runs after the workers,
# one at a time.
#
# With SHARD=<i>/<n>, only shard i of n runs, so CI can split the cases
# across jobs. The shards are balanced by each case's seconds in
# tests/plugins/times.txt (a case that isn't there counts as the median),
# biggest first into the shard with the least time so far.
#
# With JOBS=<n> (Linux only), the cases are split the same way across n
# workers that run at once, each in its own mount and network namespace
# with its own fake card mounted at the card's path, its own /tmp and
# loopback, and its own virtual display. Its logs are in
# build/tests/plugins/w<k>, and its output is printed when it's done.
#
# Every run writes build/tests/plugins/cases.tsv (each case and its
# milliseconds) and starts.tsv (each launcher start, from
# desktop-smoke.sh's SMOKE_TIMES), and prints the slowest of them;
# scripts/test-times.sh summarises them and updates times.txt.
#
#   tests/plugins/run.sh
#   SHARD=2/3 tests/plugins/run.sh
#   JOBS=3 SHARD=2/3 tests/plugins/run.sh
#   CASES="010-hello 940-jump-letter" tests/plugins/run.sh
#   LIST=1 JOBS=3 SHARD=2/3 tests/plugins/run.sh   what each worker would run

set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
# a worker's is under the run's, which it's given
WORK="${PLUGIN_TEST_WORK:-$ROOT/build/tests/plugins}"
# shellcheck source=scripts/timing.sh
. "$ROOT/scripts/timing.sh"
CARD=/var/tmp/nextui/sdcard
STATE="$CARD/.userdata/desktop/launcher"
ARCH="desktop-$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
CC="${CC:-cc}"
WAIT="${PLUGIN_TEST_SECONDS:-10}"
JOBS="${JOBS:-1}"
TIMES_FILE="$ROOT/tests/plugins/times.txt"
failures=0
case_log=""

# fail <message>: names the case whose log is $case_log, if there is one
fail() {
	if [ -n "$case_log" ]; then
		echo "FAIL: $(basename "$case_log" .log): $*; log in $case_log" >&2
	else
		echo "FAIL: $*" >&2
	fi
	failures=$((failures + 1))
}

# build_plugin <name> <manifest text>
build_plugin() {
	case_log=""
	dir="$WORK/plugins/$1.plugin"
	mkdir -p "$dir/lib/$ARCH"
	printf '%b' "$2" >"$dir/plugin.txt"
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -I"$ROOT/sdk" \
		"$ROOT/tests/plugins/$1/$1.c" -o "$dir/lib/$ARCH/plugin.so" || fail "building $1"
}

# clear_guard: forgets earlier starts' crashes, so one slow or failed case
# can't start the next ones with plugins paused
clear_guard() {
	rm -f "$STATE/loading" "$STATE/plugins_paused" "$STATE/crashes" "$STATE/fallback"
}

# clear_tmp: removes the launcher's files in /tmp that change its next start,
# before each case, so none inherits another's: the launch marker (post_return
# and launcher.return), where home was left from, and the paks' stay awake
# files. A case that wants one writes it itself. Stock's /tmp/last.txt stays,
# as 190-last-path follows what 080-drawer's launch saved there, so a case
# that needs a last path writes its own.
clear_tmp() {
	rm -f /tmp/mosaic-launcher-launched /tmp/mosaic-launcher-home /tmp/stay_awake /tmp/stay_alive
}

# start_launcher <log> [line...]: starts the launcher, keeping the crash
# guard's state. With lines, stops once the log has all of them and the
# first frame is up (the loading marker is gone); without, runs until it
# exits or $WAIT seconds pass. 0 if it ran until stopped, 1 if it exited on
# its own, 2 (also a failure here) if it had to be killed after ignoring
# SIGTERM. Not getting the lines in time is a failure too, and returns 0.
# A start that logged every line but never drew its first frame gets one
# more try, with a warning: CI's xvfb rarely stalls a job's first start
# like that (#233).
start_launcher() {
	case_log="$1"
	shift
	until=$(printf '%s\n' "$@")
	had_marker=0
	[ -e "$STATE/loading" ] && had_marker=1
	tries=0
	while :; do
		MOSAIC_LAUNCHER_PLUGIN_PATH="$WORK/plugins" SMOKE_UNTIL="$until" SMOKE_UNTIL_GONE="$STATE/loading" \
			SMOKE_TIMES="$WORK/starts.tsv" SMOKE_CASE="$case_name" \
			"$ROOT/scripts/desktop-smoke.sh" "$case_log" "$WAIT" >/dev/null 2>"$WORK/smoke.err"
		status=$?
		tries=$((tries + 1))
		if [ "$status" -ne 3 ] || [ "$tries" -gt 1 ] || [ "$had_marker" -eq 1 ]; then break; fi
		grep -q '^  still there: ' "$WORK/smoke.err" || break
		grep -q '^  missing line: ' "$WORK/smoke.err" && break
		stalled="${case_log%.log}-stalled.log"
		cp "$case_log" "$stalled"
		[ -f "$case_log.stacks" ] && mv "$case_log.stacks" "$stalled.stacks"
		echo "WARN: $(basename "$case_log" .log): no first frame after ${WAIT}s, starting it again; log in $stalled" >&2
		# the main thread's stack, so a passing retry still says where it waited
		[ -f "$stalled.stacks" ] && sed -n '/^Thread 1 /,/^$/p' "$stalled.stacks" | head -n 25 >&2
		# this start's marker, which would pause plugins on the next
		rm -f "$STATE/loading"
	done
	if [ "$status" -eq 2 ]; then
		fail "the launcher ignored SIGTERM and was killed"
		# its processes and main thread's stack, from the end of its log (SMOKE_STACKS)
		sed -n '/^--- still running/,$p' "$case_log" | sed -n '/^processes:$/,/^$/p; /^Thread 1 /,/^$/p' | head -n 60 >&2
	fi
	if [ "$status" -eq 3 ]; then
		why=$(sed -n 's/^  //p' "$WORK/smoke.err" | tr '\n' ',')
		fail "not ready after ${WAIT}s (${why%,})"
		return 0
	fi
	return "$status"
}

# run_launcher <log> [line...]: start_launcher from a clear crash guard
run_launcher() {
	clear_guard
	start_launcher "$@"
}

# expect <line>: plugin $plugin logged line in $log
# shellcheck disable=SC2154 # the cases set plugin and log
expect() {
	grep -qF "plugin $plugin: $1" "$log" || fail "$plugin didn't log \"$1\""
}

# what more than one case uses
PLAY_DB="$CARD/.userdata/shared/game_logs.sqlite"
# shellcheck disable=SC2034 # used by the cases
RECENTS="$CARD/.userdata/shared/.minui/recent.txt" \
	SETTINGS="$CARD/.userdata/shared/minuisettings.txt" \
	RA_DIR="$CARD/.userdata/shared/.ra" \
	RA_HASHES="$CARD/.userdata/shared/launcher/ra-hashes.txt" \
	HOME_LEFT=/tmp/mosaic-launcher-home

# make_play_db: writes the SQL on stdin into a new $PLAY_DB
make_play_db() {
	rm -f "$PLAY_DB"
	if command -v sqlite3 >/dev/null; then
		sqlite3 "$PLAY_DB"
	else
		python3 -I -c 'import sqlite3, sys; db = sqlite3.connect(sys.argv[1]); db.executescript(sys.stdin.read()); db.commit()' "$PLAY_DB"
	fi
}

# md5_of <file>
md5_of() {
	if command -v md5sum >/dev/null; then md5sum "$1" | cut -d' ' -f1; else md5 -q "$1"; fi
}

# frame_json <json> <file>: minarch's [u32 length][json][sha256 of json]
frame_json() {
	len=$(printf '%s' "$1" | wc -c | tr -d ' ')
	# shellcheck disable=SC2059 # the format is the length's four bytes
	{
		printf "$(printf '\\%03o\\%03o\\%03o\\%03o' $((len & 255)) $((len >> 8 & 255)) $((len >> 16 & 255)) $((len >> 24 & 255)))"
		printf '%s' "$1"
		printf '%s' "$1" | openssl dgst -sha256 -binary
	} >"$2"
}

# moved <log>: 1 when filterer's done line has the selection moving
moved() {
	sed -n 's/.*plugin filterer: done: index \([0-9-]*\) to \([0-9-]*\),.*/\1 \2/p' "$1" | {
		read -r before after
		[ -n "$before" ] && [ "$before" != "$after" ]
	}
}

# drew <text>: the page drew exactly this text
# shellcheck disable=SC2154 # the cases set texts
drew() {
	grep -qxF -- "$1" "$texts" || fail "the page didn't show \"$1\""
}

# seen: the screens screener says it's on in $log, in order
seen() { sed -n 's/.*plugin screener: now //p' "$log" | tr '\n' ' '; }

# everywhere <case name>: 0 when its "# shard:" line says all
everywhere() {
	grep -qx '# shard: all' "$ROOT/tests/plugins/cases/$1.sh"
}

# after <case name>: the case its "# after:" line names, if any
after() {
	sed -n 's/^# after: //p' "$ROOT/tests/plugins/cases/$1.sh" | head -n 1
}

# alone <case name>: 0 when it, or the first case of its chain of "# after:"
# lines, has an "# alone" line
alone() {
	grep -qx '# alone' "$ROOT/tests/plugins/cases/$1.sh" "$ROOT/tests/plugins/cases/$(group "$1").sh"
}

# group <case name>: the first case of the chain of "# after:" lines it's in
group() {
	group_name="$1"
	while group_after=$(after "$group_name") && [ -n "$group_after" ]; do group_name="$group_after"; done
	echo "$group_name"
}

# balance <n>: reads "<name> <group> <weight>" lines on stdin and prints
# "<bin> <name>" for each, putting the groups biggest first (by the sum of
# their cases' times.txt seconds, times their weight) into the one of n bins
# with the least time so far
balance() {
	awk -v bins="$1" -v times="$TIMES_FILE" '
		BEGIN { while ((getline line < times) > 0) if (split(line, f, " ") == 2) { secs[f[1]] = f[2]; known[++k] = f[2] } }
		NF { names[++n] = $1; group_of[$1] = $2; weight[$1] = $3 }
		END {
			# unknown cases count as the median
			for (i = 1; i <= k; i++) for (j = i + 1; j <= k; j++) if (known[j] < known[i]) { t = known[i]; known[i] = known[j]; known[j] = t }
			median = k ? known[int((k + 1) / 2)] : 10
			for (i = 1; i <= n; i++) {
				g = group_of[names[i]]
				if (!(g in g_time)) groups[++m] = g
				g_time[g] += ((names[i] in secs) ? secs[names[i]] : median) * weight[names[i]]
			}
			# biggest first, then by name, so the split is the same every run
			for (i = 1; i <= m; i++) for (j = i + 1; j <= m; j++) {
				a = groups[i]; b = groups[j]
				if (g_time[b] > g_time[a] || (g_time[b] == g_time[a] && b < a)) { groups[i] = b; groups[j] = a }
			}
			for (b = 1; b <= bins; b++) load[b] = 0
			for (i = 1; i <= m; i++) {
				best = 1
				for (b = 2; b <= bins; b++) if (load[b] < load[best]) best = b
				load[best] += g_time[groups[i]]
				bin_of[groups[i]] = best
			}
			for (i = 1; i <= n; i++) print bin_of[group_of[names[i]]], names[i]
		}
	'
}

# pick <i> <n> [workers]: of the case names on stdin, those balance puts in
# bin i of n, after the ones every bin runs, in name order. For workers it
# leaves out the cases that run alone; for shards they weigh JOBS times
# their seconds, as no other case runs beside them.
pick() {
	pick_all=""
	pick_rest=""
	while read -r pick_name; do
		[ -n "$pick_name" ] || continue
		if everywhere "$pick_name"; then
			pick_all="$pick_all$pick_name
"
		elif ! alone "$pick_name"; then
			pick_rest="$pick_rest$pick_name $(group "$pick_name") 1
"
		elif [ -z "${3:-}" ]; then
			pick_rest="$pick_rest$pick_name $(group "$pick_name") $JOBS
"
		fi
	done
	{
		printf '%s' "$pick_all"
		printf '%s' "$pick_rest" | balance "$2" | awk -v i="$1" '$1 == i { print $2 }'
	} | sort
}

# check_count <i>/<n> <what>: dies unless it's i of n with 1 <= i <= n
check_count() {
	check_index=${1%/*}
	check_total=${1#*/}
	case "$check_index/$check_total" in
		*[!0-9/]* | /* | */ | */*/*)
			echo "$2 must be <i>/<n>, eg. 1/3: $1" >&2
			exit 2
			;;
	esac
	if [ "$check_index" -lt 1 ] || [ "$check_index" -gt "$check_total" ]; then
		echo "$2 must be <i>/<n> with i from 1 to n: $1" >&2
		exit 2
	fi
}

# worker <k> <cases>: runs this script on just those cases in a namespace of
# its own, its output in $WORK/w<k>.out and .err; the exit status is the
# script's
worker() {
	worker_card="$ROOT/build/tests/cards/w$1"
	rm -rf "$worker_card"
	mkdir -p "$worker_card"
	# what upstream's prepare script puts on a new card
	skeleton="$ROOT/vendor/NextUI/skeleton"
	cp -R "$skeleton/BASE/." "$worker_card/"
	[ -d "$skeleton/EXTRAS" ] && cp -R "$skeleton/EXTRAS/." "$worker_card/"
	mkdir -p "$worker_card/.system"
	cp -R "$skeleton/SYSTEM/." "$worker_card/.system/"
	# without root, as root in a user namespace of its own
	map_root=--map-root-user
	[ "$(id -u)" -eq 0 ] && map_root=
	# shellcheck disable=SC2016 # the inner shell expands them
	unshare --mount --net --propagation private ${map_root:+"$map_root"} sh -c '
		ip link set lo up
		mount --bind "$1" "$2"
		mount -t tmpfs tmpfs /tmp
		shift 2
		exec "$@"
	' worker "$worker_card" "$CARD" env PLUGIN_TEST_WORK="$WORK/w$1" CASES="$2" JOBS=1 SHARD= \
		xvfb-run -a sh "$0" >"$WORK/w$1.out" 2>"$WORK/w$1.err"
}

# the cases this run has, by name
all_cases=$(for case_file in "$ROOT"/tests/plugins/cases/*.sh; do basename "$case_file" .sh; done)
if [ -n "${CASES:-}" ]; then
	run_cases=""
	for case_name in $CASES; do
		[ -f "$ROOT/tests/plugins/cases/$case_name.sh" ] || { echo "no case $case_name" >&2; exit 2; }
		# and the cases it's after
		while [ -n "$case_name" ]; do
			run_cases="$run_cases$case_name
"
			case_name=$(after "$case_name")
		done
	done
	run_cases=$(printf '%s' "$run_cases" | sort -u)
elif [ -n "${SHARD:-}" ]; then
	check_count "$SHARD" SHARD
	run_cases=$(printf '%s\n' "$all_cases" | pick "${SHARD%/*}" "${SHARD#*/}")
else
	run_cases=$all_cases
fi

# alone_cases: the run's cases that run alone with JOBS, one per line
alone_cases() {
	for alone_name in $run_cases; do
		if ! everywhere "$alone_name" && alone "$alone_name"; then echo "$alone_name"; fi
	done
}

# LIST=1: the cases, and each worker's with JOBS, with their seconds in
# times.txt, and nothing run
if [ -n "${LIST:-}" ]; then
	list() {
		awk -v times="$TIMES_FILE" '
			BEGIN { while ((getline line < times) > 0) if (split(line, f, " ") == 2) secs[f[1]] = f[2] }
			NF { t += secs[$1]; printf "  %4ss  %s\n", ($1 in secs) ? secs[$1] : "?", $1 }
			END { printf "  %4ds  in all\n", t }
		'
	}
	if [ "$JOBS" -gt 1 ]; then
		k=1
		while [ "$k" -le "$JOBS" ]; do
			echo "worker $k:"
			printf '%s\n' "$run_cases" | pick "$k" "$JOBS" workers | list
			k=$((k + 1))
		done
		echo "alone, after the workers:"
		alone_cases | list
	else
		printf '%s\n' "$run_cases" | list
	fi
	exit 0
fi

rm -rf "$WORK" "$STATE"
mkdir -p "$WORK"

if [ "$JOBS" -gt 1 ]; then
	[ "$(uname -s)" = Linux ] || { echo "JOBS needs Linux's namespaces; run scripts/docker-test.sh on macOS" >&2; exit 2; }
	mkdir -p "$CARD"
	# the bundled plugins once, so the parts they share outside a case's
	# folder (such as romm's BearSSL in build/bearssl) are built before
	# workers build those plugins at once
	make -s -j"$JOBS" -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/bundled" CC="$CC" >"$WORK/bundled.log" 2>"$WORK/bundled.err" \
		|| { cat "$WORK/bundled.err" >&2; fail "building the bundled plugins"; }
	# done_worker <k> <status> <title>: prints worker k's output and adds its
	# failures and times to the run's
	done_worker() {
		echo "$3:"
		cat "$WORK/w$1.out"
		cat "$WORK/w$1.err" >&2
		if [ "$2" -ne 0 ]; then
			worker_failures=$(grep -c '^FAIL' "$WORK/w$1.err")
			failures=$((failures + (worker_failures > 0 ? worker_failures : 1)))
		fi
		cat "$WORK/w$1/cases.tsv" >>"$WORK/cases.tsv" 2>/dev/null
		cat "$WORK/w$1/starts.tsv" >>"$WORK/starts.tsv" 2>/dev/null
	}
	k=1
	while [ "$k" -le "$JOBS" ]; do
		mine=$(printf '%s\n' "$run_cases" | pick "$k" "$JOBS" workers | tr '\n' ' ')
		# a worker with no cases would run them all, as CASES would be empty
		if [ -n "${mine% }" ]; then
			worker "$k" "$mine" &
			eval "pid_$k=\$!"
		else
			eval "pid_$k="
		fi
		k=$((k + 1))
	done
	k=1
	while [ "$k" -le "$JOBS" ]; do
		eval "worker_pid=\$pid_$k"
		if [ -n "$worker_pid" ]; then
			wait "$worker_pid"
			done_worker "$k" $? "worker $k"
		fi
		k=$((k + 1))
	done
	# then the ones that run alone, in one more worker with the CPUs to
	# itself, after the ones every worker runs (whose logs they may check)
	mine=$(alone_cases | tr '\n' ' ')
	if [ -n "$mine" ]; then
		worker "$k" "$(for every_name in $run_cases; do if everywhere "$every_name"; then printf '%s ' "$every_name"; fi; done)$mine"
		done_worker "$k" $? "alone, after the workers"
	fi
else
	# the card from the skeleton when there isn't one, before the folders
	# below make it look prepared to upstream's script, which then skips it
	bash "$ROOT/vendor/NextUI/workspace/desktop/prepare_fake_sd_root.sh" >/dev/null || fail "preparing the fake card"
	for case_name in $run_cases; do
		case_started=$(now_ms)
		# where the cases write the shared settings and recents, which only a
		# launch makes on a new card
		mkdir -p "$(dirname "$RECENTS")"
		clear_tmp
		# shellcheck source=/dev/null
		. "$ROOT/tests/plugins/cases/$case_name.sh"
		case_ms=$(($(now_ms) - case_started))
		printf '%s\t%d\n' "$case_name" "$case_ms" >>"$WORK/cases.tsv"
		echo "case $case_name: $((case_ms / 1000))s"
	done
	rm -rf "$STATE"
fi

# a worker's are printed by the run that started it
[ -z "${PLUGIN_TEST_WORK:-}" ] && "$ROOT/scripts/test-times.sh" summary "$WORK" 10

if [ "$failures" -gt 0 ]; then
	echo "$failures plugin check(s) failed" >&2
	exit 1
fi
echo "ok plugins"
