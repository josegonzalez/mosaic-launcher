#!/bin/sh
#
# Writes PLUGINS.md's "Bundled plugins" table from each bundled plugin's
# plugins/<name>/README.md, one row per plugin in name order. A README is a
# "# <name>" heading, a blank line and the row's text, which may wrap over
# several lines. With --check, changes nothing and fails if the table is out
# of date.
#
#   ./scripts/plugin-table.sh [--check]

set -eu
export LC_ALL=C

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DOC="$ROOT/PLUGINS.md"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

{
	echo '| Plugin | What it does |'
	echo '|---|---|'
	for readme in "$ROOT"/plugins/*/README.md; do
		name="$(basename "$(dirname "$readme")")"
		text=$(sed '1,/^$/d' "$readme" | tr '\n' ' ' | sed 's/ *$//')
		[ -n "$text" ] || {
			echo "plugins/$name/README.md has no text after its heading" >&2
			exit 1
		}
		# shellcheck disable=SC2016 # the backticks are markdown's
		printf '| `%s` | %s |\n' "$name" "$text"
	done
} >"$tmp/table"

# the table is the first run of lines starting with | after the heading
awk -v table="$tmp/table" '
	/^## Bundled plugins$/ { section = 1 }
	section == 1 && /^\174/ {
		if (!done) {
			while ((getline line < table) > 0) print line
			done = 1
		}
		next
	}
	section == 1 && done { section = 2 }
	{ print }
	END { if (!done) exit 1 }
' "$DOC" >"$tmp/PLUGINS.md" || {
	echo "PLUGINS.md has no \"Bundled plugins\" table" >&2
	exit 1
}

if [ "${1:-}" = --check ]; then
	cmp -s "$DOC" "$tmp/PLUGINS.md" || {
		echo "PLUGINS.md's bundled plugins table is out of date; run ./scripts/plugin-table.sh" >&2
		exit 1
	}
else
	cp "$tmp/PLUGINS.md" "$DOC"
fi
