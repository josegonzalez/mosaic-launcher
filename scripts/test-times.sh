#!/bin/sh
#
# Reads the timings the tests write, to find what's slow and to balance the
# plugin cases across CI's shards and a run's workers.
#
# tests/plugins/run.sh writes cases.tsv (a case and its milliseconds per
# line) and starts.tsv (a launcher start per line: its case, its log, then
# milliseconds in all, preparing the card, until ready, settling and
# stopping, then how it ended) in its work folder, build/tests/plugins.
# tests/c writes build/tests/unit.tsv (a unit test and its milliseconds).
#
#   scripts/test-times.sh summary <dir> [count]  the slowest cases and
#       starts in a run's folder, and where the starts' time went
#   scripts/test-times.sh slowest <tsv> [count]  the slowest rows of a
#       "<name> <ms>" file, such as unit.tsv
#   scripts/test-times.sh update <dir>...        writes tests/plugins/times.txt
#       from the cases.tsv in each folder (such as one per CI shard), keeping
#       the cases they don't have and dropping cases that are gone
#   scripts/test-times.sh fetch [run]            downloads a CI run's
#       plugin-times-<shard> artifacts (the latest run on main by default)
#       and updates times.txt from them

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TIMES_FILE="$ROOT/tests/plugins/times.txt"

# slowest <tsv> <count>
slowest() {
	[ -s "$1" ] || return 0
	sort -t "$(printf '\t')" -k2,2nr "$1" | head -n "$2" | awk -F '\t' '{ printf "  %7.1fs  %s\n", $2 / 1000, $1 }'
}

summary() {
	dir="$1"
	count="$2"
	if [ -s "$dir/cases.tsv" ]; then
		awk -F '\t' '{ t += $2; n++ } END { printf "%d cases, %.0fs in all\n", n, t / 1000 }' "$dir/cases.tsv"
		echo "slowest cases:"
		slowest "$dir/cases.tsv" "$count"
	fi
	[ -s "$dir/starts.tsv" ] || return 0
	echo
	# where a start's time goes, and what the cases spend outside their starts
	awk -F '\t' '
		FILENAME ~ /cases\.tsv$/ { case_ms[$1] = $2; next }
		{
			n++; all += $3; prep += $4; up += $5; settle += $6; stop += $7; how[$8]++
			in_starts[$1] += $3
		}
		END {
			printf "%d launcher starts, %.0fs in all:\n", n, all / 1000
			printf "  %7.0fs  preparing the card\n", prep / 1000
			printf "  %7.0fs  until ready (or the wait ran out)\n", up / 1000
			printf "  %7.0fs  settling once ready\n", settle / 1000
			printf "  %7.0fs  stopping\n", stop / 1000
			line = ""
			for (h in how) line = line " " h "=" how[h]
			print "  how they ended:" line
			for (c in case_ms) outside += case_ms[c] - in_starts[c]
			printf "%.0fs of the cases is outside their starts (building plugins, checks)\n", outside / 1000
		}
	' "$dir/cases.tsv" "$dir/starts.tsv"
	echo "slowest starts (until ready):"
	awk -F '\t' '{ printf "%s/%s\t%d\t%s\n", $1, $2, $5, $8 }' "$dir/starts.tsv" |
		sort -t "$(printf '\t')" -k2,2nr | head -n "$count" |
		awk -F '\t' '{ printf "  %7.1fs  %s (%s)\n", $2 / 1000, $1, $3 }'
	echo "cases that spend the most outside their starts:"
	awk -F '\t' '
		FILENAME ~ /cases\.tsv$/ { case_ms[$1] = $2; next }
		{ in_starts[$1] += $3 }
		END { for (c in case_ms) printf "%s\t%d\n", c, case_ms[c] - in_starts[c] }
	' "$dir/cases.tsv" "$dir/starts.tsv" | sort -t "$(printf '\t')" -k2,2nr | head -n "$count" |
		awk -F '\t' '{ printf "  %7.1fs  %s\n", $2 / 1000, $1 }'
}

update() {
	tmp="$(mktemp)"
	merged="$(mktemp)"
	for dir in "$@"; do
		[ -s "$dir/cases.tsv" ] && cat "$dir/cases.tsv"
	done >"$tmp"
	[ -s "$tmp" ] || { echo "no cases.tsv in $*" >&2; rm -f "$tmp"; exit 1; }
	# whole seconds, rounded up; a case in more than one folder (one every
	# shard runs) keeps its longest
	{
		awk -F '\t' '{ s = int(($2 + 999) / 1000); if (!($1 in t) || s > t[$1]) t[$1] = s } END { for (c in t) print c, t[c], "new" }' "$tmp"
		[ -f "$TIMES_FILE" ] && awk '{ print $1, $2, "old" }' "$TIMES_FILE"
	} | awk -v cases="$ROOT/tests/plugins/cases" '
		$3 == "new" { t[$1] = $2; next }
		!($1 in t) { t[$1] = $2 }
		END {
			for (c in t) if ((getline line < (cases "/" c ".sh")) > 0) { close(cases "/" c ".sh"); print c, t[c] }
		}
	' | sort >"$merged"
	mv "$merged" "$TIMES_FILE"
	rm -f "$tmp"
	echo "wrote $TIMES_FILE ($(wc -l <"$TIMES_FILE" | tr -d ' ') cases)"
}

fetch() {
	run="${1:-}"
	if [ -z "$run" ]; then
		run=$(gh run list --workflow ci.yml --branch main --status success --limit 1 --json databaseId --jq '.[0].databaseId')
	fi
	tmp="$(mktemp -d)"
	gh run download "$run" --pattern 'plugin-times-*' --dir "$tmp" || { rm -rf "$tmp"; exit 1; }
	update "$tmp"/*
	rm -rf "$tmp"
}

case "${1:-}" in
	summary) summary "${2:?usage: $0 summary <dir> [count]}" "${3:-10}" ;;
	slowest) slowest "${2:?usage: $0 slowest <tsv> [count]}" "${3:-10}" ;;
	update)
		shift
		[ $# -gt 0 ] || { echo "usage: $0 update <dir>..." >&2; exit 2; }
		update "$@"
		;;
	fetch) fetch "${2:-}" ;;
	*)
		sed -n '3,/^$/s/^# \{0,1\}//p' "$0" >&2
		exit 2
		;;
esac
