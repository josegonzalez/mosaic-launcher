#!/bin/sh
#
# Makes the bundled plugins' quick menu tile icons from the Phosphor icons
# in scripts/quick-icons/phosphor/, as scripts/quick-icons/icons.txt lists
# them: white on clear, at stock's sizes for the tile's row (its top row's
# Games@<scale>x.png is 48 by 48 units, its bottom row's icons 12 by 12), at
# scales 2 and 3, the FIXED_SCALE of every device, into
# plugins/<plugin>/res/quick/<file>@<scale>x.png. Needs rsvg-convert
# (librsvg) and ImageMagick's magick.
#
#   ./scripts/quick-icons.sh          remake every icon
#   ./scripts/quick-icons.sh fetch    first copy any SVG icons.txt names
#                                     that phosphor/ lacks from Phosphor's
#                                     release (see "Quick menu icons" in docs/DEVELOPMENT.md)

set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="$ROOT/scripts/quick-icons"
RELEASE_URL="https://github.com/phosphor-icons/core/archive/refs/tags/v2.0.8.tar.gz"
RELEASE_SHA256="8b4dd9afb96b50c92a613f5786af6a7dcff6e416442c42c12ed831cea050a698"

# Prints each listed line's fields, without comments and blank lines.
entries() {
	grep -v '^#' "$DIR/icons.txt" | grep -v '^[[:space:]]*$'
}

fetch() {
	tmp="$(mktemp -d)"
	trap 'rm -rf "$tmp"' EXIT
	curl -fsSL -o "$tmp/core.tar.gz" "$RELEASE_URL"
	sum="$(shasum -a 256 "$tmp/core.tar.gz" | cut -d' ' -f1)"
	if [ "$sum" != "$RELEASE_SHA256" ]; then
		echo "the release's checksum is $sum, not $RELEASE_SHA256" >&2
		exit 1
	fi
	tar -xzf "$tmp/core.tar.gz" -C "$tmp"
	entries | while read -r _plugin _file icon _row; do
		svg="$DIR/phosphor/$icon-fill.svg"
		[ -f "$svg" ] || cp "$tmp/core-2.0.8/assets/fill/$icon-fill.svg" "$svg"
	done
}

if [ "${1:-}" = "fetch" ]; then
	fetch
elif [ "$#" -gt 0 ]; then
	echo "usage: $0 [fetch]" >&2
	exit 2
fi

for tool in rsvg-convert magick; do
	if ! command -v "$tool" >/dev/null; then
		echo "$tool not found; on macOS run: brew install librsvg imagemagick" >&2
		exit 1
	fi
done

entries | while read -r plugin file icon row; do
	svg="$DIR/phosphor/$icon-fill.svg"
	if [ ! -f "$svg" ]; then
		echo "missing $svg; run: $0 fetch" >&2
		exit 1
	fi
	case "$row" in
	top) units=48 ;;
	bottom) units=12 ;;
	*)
		echo "$file: row $row isn't top or bottom" >&2
		exit 1
		;;
	esac
	out="$ROOT/plugins/$plugin/res/quick"
	mkdir -p "$out"
	for scale in 2 3; do
		size=$((units * scale))
		# white with the icon's coverage as alpha, as grey and alpha to stay small
		rsvg-convert -w "$size" -h "$size" "$svg" |
			magick png:- -fill white -colorize 100 -strip \
				-define png:color-type=4 -define png:bit-depth=8 \
				"$out/$file@${scale}x.png"
	done
done
