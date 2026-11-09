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
# results back, and one that crashes during startup. A preloaded library,
# presser, drives the Launcher pak's Plugins page through the bundled play
# time limit's protected settings and scrolls a long one, reads back those
# hints, and drives the
# bundled home dashboard's tabs and modes and the bundled tags plugin's
# checklist. It also checks the status pill shell
# paks' status files show.
#
# Each case waits up to PLUGIN_TEST_SECONDS (10 by default) for the log
# lines it checks, and stops as soon as they're in, so a slow machine only
# makes the run slower.
#
# The cases are in tests/plugins/cases/, run in name order with the helpers
# here. With SHARD=<i>/<n>, only the cases whose "# shard:" line is i (or
# all, or i plus a multiple of n) run, so CI can split them across jobs;
# each prints how long it took.
#
#   tests/plugins/run.sh
#   SHARD=2/3 tests/plugins/run.sh

set -u

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="$ROOT/build/tests/plugins"
CARD=/var/tmp/nextui/sdcard
STATE="$CARD/.userdata/desktop/launcher"
ARCH="desktop-$(uname -s | tr '[:upper:]' '[:lower:]')-$(uname -m)"
CC="${CC:-cc}"
WAIT="${PLUGIN_TEST_SECONDS:-10}"
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
	[ "$status" -eq 2 ] && fail "the launcher ignored SIGTERM and was killed"
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

# in_shard <case file>: 0 when the case's "# shard:" line puts it in $SHARD
in_shard() {
	[ -z "${SHARD:-}" ] && return 0
	case_shard=$(sed -n 's/^# shard: //p' "$1" | head -n 1)
	[ "$case_shard" = all ] && return 0
	case "$case_shard" in
	'' | *[!0-9]*)
		fail "$(basename "$1") has no \"# shard: <n>\" line"
		return 1
		;;
	esac
	[ $(((case_shard - 1) % shard_count)) -eq $((shard_index - 1)) ]
}

if [ -n "${SHARD:-}" ]; then
	shard_index=${SHARD%/*}
	shard_count=${SHARD#*/}
	case "$shard_index/$shard_count" in
	*[!0-9/]* | /* | */ | */*/*)
		echo "SHARD must be <i>/<n>, eg. 1/3: $SHARD" >&2
		exit 2
		;;
	esac
	if [ "$shard_index" -lt 1 ] || [ "$shard_index" -gt "$shard_count" ]; then
		echo "SHARD must be <i>/<n> with i from 1 to n: $SHARD" >&2
		exit 2
	fi
fi

rm -rf "$WORK" "$STATE"
for case_file in "$ROOT"/tests/plugins/cases/*.sh; do
	in_shard "$case_file" || continue
	case_started=$(date +%s)
	# shellcheck source=/dev/null
	. "$case_file"
	echo "case $(basename "$case_file" .sh): $(($(date +%s) - case_started))s"
done

rm -rf "$STATE"
if [ "$failures" -gt 0 ]; then
	echo "$failures plugin check(s) failed" >&2
	exit 1
fi
echo "ok plugins"
