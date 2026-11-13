#!/bin/sh
#
# Writes the index of bundled plugins in docs/plugins.md, between its
# "plugins:start" and "plugins:end" comments, from each bundled plugin's
# plugins/<name>/plugin.txt: its `category` (the heading it's listed under)
# and its one-sentence `summary`. Each entry links the plugin's README.md,
# which must start with a "# <name>" heading. Categories come in the order
# CATEGORIES lists them and plugins in name order. With --check, changes
# nothing and fails if the index is out of date.
#
#   ./scripts/plugin-table.sh [--check]

set -eu
export LC_ALL=C

CATEGORIES="Looks|Library|Launching games|Play time|Power|Network|Controls and setup|Extras"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DOC="$ROOT/docs/plugins.md"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

field() {
	sed -n "s/^$1=//p" "$2" | head -n 1
}

# id, category and summary of every plugin, tab separated
for txt in "$ROOT"/plugins/*/plugin.txt; do
	dir="$(dirname "$txt")"
	name="$(basename "$dir")"
	category="$(field category "$txt")"
	summary="$(field summary "$txt")"
	[ -n "$category" ] || {
		echo "plugins/$name/plugin.txt has no category=" >&2
		exit 1
	}
	[ -n "$summary" ] || {
		echo "plugins/$name/plugin.txt has no summary=" >&2
		exit 1
	}
	case "|$CATEGORIES|" in
	*"|$category|"*) ;;
	*)
		echo "plugins/$name/plugin.txt: category=$category isn't one of: $CATEGORIES" >&2
		exit 1
		;;
	esac
	[ "$(sed -n 1p "$dir/README.md")" = "# $name" ] || {
		echo "plugins/$name/README.md doesn't start with \"# $name\"" >&2
		exit 1
	}
	printf '%s\t%s\t%s\n' "$category" "$name" "$summary"
done >"$tmp/plugins"

{
	echo "$CATEGORIES" | tr '|' '\n' | while IFS= read -r category; do
		grep "^$category	" "$tmp/plugins" >"$tmp/group" || continue
		printf '## %s\n\n' "$category"
		while IFS='	' read -r _ name summary; do
			printf -- '- [%s](../plugins/%s/README.md) - %s\n' "$name" "$name" "$summary"
		done <"$tmp/group"
		echo
	done
} >"$tmp/index"

# the index replaces what's between the two comments
awk -v index_file="$tmp/index" '
	/^<!-- plugins:end -->$/ { skipping = 0 }
	!skipping { print }
	/^<!-- plugins:start/ {
		skipping = 1
		started = 1
		while ((getline line < index_file) > 0) print line
	}
	END { if (!started) exit 1 }
' "$DOC" >"$tmp/plugins.md" || {
	echo "docs/plugins.md has no plugins:start comment" >&2
	exit 1
}

if [ "${1:-}" = --check ]; then
	cmp -s "$DOC" "$tmp/plugins.md" || {
		diff -u "$DOC" "$tmp/plugins.md" >&2
		echo "docs/plugins.md is out of date; run ./scripts/plugin-table.sh" >&2
		exit 1
	}
else
	cp "$tmp/plugins.md" "$DOC"
fi
