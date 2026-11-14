#!/bin/sh
#
# Takes the screenshots in the bundled plugins' docs (plugins/<id>/docs/*.png)
# with the desktop build, on the sample card in tests/screenshots/card.
# `make screenshots` runs it; run it by hand whenever a plugin's UI changes,
# since CI doesn't compare images (macOS and Linux SDL draw differently).
#
#   scripts/screenshots.sh prepare        writes build/screenshots/workspace
#   scripts/screenshots.sh [plugin...]    takes every plugin's shots, or these
#   scripts/screenshots.sh --check        fails if a README or doc embeds an
#                                         image that isn't there (make lint)
#
# prepare writes a copy of NextUI's desktop platform with the h700's 640x480
# screen and sizes, whose card is build/screenshots/sdcard, so the shots never
# touch the fake card at /var/tmp/nextui/sdcard the tests use. The makefile
# builds the launcher against it into build/screenshots/desktop.
#
# Each plugin's shots are in tests/screenshots/shots/<id>.sh, a list of
# blocks run with the helpers below:
#
#   begin                   a fresh copy of the sample card, no plugins on
#   on <id>...              turns plugins on, in this order
#   slot <slot> <id>        gives an exclusive slot to a plugin
#   cfg <id> <key=value>... a plugin's settings
#   press <keys>            presser's keys after a wait, eg. "down down y" or
#                           "select+start/1500" (held together 1.5 seconds)
#   after <ms>              keep the frame this long after the last press
#                           (600 by default, before stock starts scrolling
#                           a long selected name), eg. an animation's middle
#   args <arg>...           passed to nextui.elf, eg. --settings plugins
#   shot <name>             starts the launcher and saves that frame as
#                           plugins/<id>/docs/<name>.png, <id> being the file's
#
# The launcher's own shots, such as its lock, are in shots/launcher.sh, and
# are saved in docs/images/ in place of a plugin's docs.
#
# A block can also change the card under $CARD before its shot, such as
# $CARD/Collections/Favorites.txt, or export tests/screenshots/device.c's
# variables for a device's sample values in place of this computer's.

set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$ROOT/build/screenshots"
CARD="$WORK/sdcard"
TEMPLATE="$WORK/template"
STATE="$CARD/.userdata/desktop/launcher"
ELF="$WORK/desktop/nextui.elf"
PLUGINS="$ROOT/build/desktop-plugins"
SHOTS="$ROOT/tests/screenshots"
SKELETON="$ROOT/vendor/NextUI/skeleton"
WAIT="${SCREENSHOT_SECONDS:-30}"
CC="${CC:-cc}"
failures=0
count=0

# prepare: the desktop platform with the h700's screen and our own card
prepare() {
	from="$ROOT/vendor/NextUI/workspace"
	to="$WORK/workspace"
	rm -rf "$to"
	mkdir -p "$to/desktop/platform" "$to/all"
	# stock's transient files, such as the last path, in build/screenshots/tmp
	# rather than /tmp, where the tests' launcher keeps its own
	cp -R "$from/all/common" "$to/all/"
	sed "s|\"/tmp/|\"$WORK/tmp/|" "$from/all/common/defines.h" >"$to/all/common/defines.h"
	if grep -q '"/tmp/' "$to/all/common/defines.h" || ! grep -q "LAST_PATH \"$WORK/tmp/" "$to/all/common/defines.h"; then
		echo "screenshots: NextUI's defines.h changed; can't move its /tmp files" >&2
		exit 1
	fi
	ln -s "$from/desktop/libmsettings" "$to/desktop/libmsettings"
	cp "$from/desktop/platform/"* "$to/desktop/platform/"
	# the active lines only; the commented ones start with //
	sed -e "s|^#define SDCARD_PATH .*|#define SDCARD_PATH \"$CARD\"|" \
		-e 's|^#define FIXED_SCALE.*|#define FIXED_SCALE 2|' \
		-e 's|^#define FIXED_WIDTH.*|#define FIXED_WIDTH 640|' \
		-e 's|^#define FIXED_HEIGHT.*|#define FIXED_HEIGHT 480|' \
		-e 's|^#define MAIN_ROW_COUNT.*|#define MAIN_ROW_COUNT 6|' \
		-e 's|^#define PADDING.*|#define PADDING 10|' \
		"$from/desktop/platform/platform.h" >"$to/desktop/platform/platform.h"
	for line in "SDCARD_PATH \"$CARD\"" "FIXED_SCALE 2" "FIXED_WIDTH 640" "FIXED_HEIGHT 480" "MAIN_ROW_COUNT 6" "PADDING 10"; do
		if [ "$(grep -c "^#define $line\$" "$to/desktop/platform/platform.h")" -ne 1 ]; then
			echo "screenshots: NextUI's desktop platform.h changed; can't set $line" >&2
			exit 1
		fi
	done
}

# check: every local image the READMEs and docs embed, ![alt](path), exists
check() {
	missing=0
	for md in "$ROOT"/README.md "$ROOT"/docs/*.md "$ROOT"/plugins/*/README.md "$ROOT"/tests/screenshots/README.md; do
		dir=$(dirname "$md")
		# leaving out code blocks, such as the example in docs/DEVELOPMENT.md
		paths=$(awk '/^```/ { code = !code; next } !code' "$md" | grep -o '!\[[^]]*\]([^)]*)' | sed 's/.*](\([^) ]*\).*/\1/')
		while IFS= read -r path; do
			case "$path" in '' | http://* | https://*) continue ;; esac
			if [ ! -f "$dir/$path" ]; then
				echo "${md#"$ROOT"/} embeds $path, which isn't there" >&2
				missing=$((missing + 1))
			fi
		done <<EOF
$paths
EOF
	done
	[ "$missing" -eq 0 ]
}

case "${1:-}" in
	prepare)
		prepare
		exit 0
		;;
	--check)
		check
		exit
		;;
esac

for tool in magick convert; do
	command -v "$tool" >/dev/null && MAGICK="$tool" && break
done
if [ -z "${MAGICK:-}" ]; then
	echo "screenshots: needs ImageMagick (brew install imagemagick, or apt install imagemagick)" >&2
	exit 1
fi
if [ ! -x "$ELF" ] || [ ! -d "$PLUGINS" ]; then
	echo "screenshots: run make screenshots, which builds $ELF and the plugins first" >&2
	exit 1
fi

case "$(uname -s)" in
	Darwin)
		LIBRARY_VAR=DYLD_LIBRARY_PATH
		PRELOAD_VAR=DYLD_INSERT_LIBRARIES
		for lib in tests/plugins/presser/presser tests/screenshots/device; do
			"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup \
				"$ROOT/$lib.c" -o "$WORK/$(basename "$lib").so" || exit 1
		done
		;;
	*)
		LIBRARY_VAR=LD_LIBRARY_PATH
		PRELOAD_VAR=LD_PRELOAD
		for lib in tests/plugins/presser/presser tests/screenshots/device; do
			"$CC" -shared -fPIC -std=gnu99 -Wall -Werror \
				"$ROOT/$lib.c" -o "$WORK/$(basename "$lib").so" -ldl || exit 1
		done
		;;
esac

# the card every shot starts from: NextUI's skeleton for the desktop, the
# sample card over it, and the play history from its SQL
rm -rf "$TEMPLATE"
mkdir -p "$TEMPLATE/.system"
cp -R "$SKELETON/BASE/." "$TEMPLATE/"
for dir in "$SKELETON/EXTRAS/"*; do
	[ "$(basename "$dir")" = Tools ] && continue
	cp -R "$dir" "$TEMPLATE/"
done
mkdir -p "$TEMPLATE/Tools"
cp -R "$SKELETON/EXTRAS/Tools/desktop" "$TEMPLATE/Tools/"
cp -R "$SKELETON/SYSTEM/desktop" "$SKELETON/SYSTEM/res" "$TEMPLATE/.system/"
cp -R "$SHOTS/card/." "$TEMPLATE/"
# save state previews are BMPs, kept as PNGs in the sample card
find "$TEMPLATE" -name '*.bmp.png' | while IFS= read -r png; do
	"$MAGICK" "$png" "BMP3:${png%.png}" && rm "$png"
done
if [ -f "$SHOTS/play-history.sql" ]; then
	db="$TEMPLATE/.userdata/shared/game_logs.sqlite"
	mkdir -p "$(dirname "$db")"
	if command -v sqlite3 >/dev/null; then
		sqlite3 "$db" <"$SHOTS/play-history.sql" || exit 1
	else
		python3 -I -c 'import sqlite3, sys; db = sqlite3.connect(sys.argv[1]); db.executescript(sys.stdin.read()); db.commit()' \
			"$db" <"$SHOTS/play-history.sql" || exit 1
	fi
fi

fail() {
	echo "FAIL: $*" >&2
	failures=$((failures + 1))
}

begin() {
	rm -rf "$CARD" "$WORK/tmp"
	cp -R "$TEMPLATE" "$CARD"
	mkdir -p "$WORK/tmp"
	mkdir -p "$STATE"
	turned_on=""
	slots=""
	keys=""
	keep_after=600
	elf_args=""
}

on() { turned_on="$turned_on $*"; }
slot() { slots="$slots slot.$1=$2"; }

# plugins_txt: the plugins turned on, in order, and every other one off, as
# a plugin plugins.txt doesn't name loads
plugins_txt() {
	for id in $turned_on; do echo "$id=on"; done
	for dir in "$PLUGINS"/*.plugin; do
		id=$(basename "$dir" .plugin)
		case " $turned_on " in *" $id "*) ;; *) echo "$id=off" ;; esac
	done
	for line in $slots; do echo "$line"; done
}

cfg() {
	id="$1"
	shift
	for pair in "$@"; do echo "$pair" >>"$STATE/$id.cfg"; done
}

press() { keys="$*"; }

# docs_dir: where the shots in $plugin's file go
docs_dir() {
	if [ "$plugin" = launcher ]; then
		echo "$ROOT/docs/images"
	else
		echo "$ROOT/plugins/$plugin/docs"
	fi
}
after() { keep_after="$1"; }
args() { elf_args="$*"; }

# shot <name>: runs the launcher until presser is done, and keeps the frame
# shown "after" milliseconds after the last press as plugins/<id>/docs/<name>.png
shot() {
	out="$(docs_dir)/$1.png"
	log="$WORK/logs/$plugin-$1.log"
	frame="$WORK/frames/$plugin-$1"
	mkdir -p "$(dirname "$out")" "$WORK/logs" "$WORK/frames"
	rm -f "$frame.bmp" "$frame.txt"
	plugins_txt >"$STATE/plugins.txt"
	# shellcheck disable=SC2086 # elf_args is split on purpose
	(
		export "$LIBRARY_VAR=/var/tmp/nextui/lib" "$PRELOAD_VAR=$WORK/presser.so:$WORK/device.so"
		export MOSAIC_LAUNCHER_PLUGIN_PATH="$PLUGINS" USERDATA_PATH="$CARD/.userdata/desktop" \
			SDCARD_PATH="$CARD" SYSTEM_PATH="$CARD/.system/desktop" PLATFORM=desktop \
			MOSAIC_LAUNCHER_PAK_DIR="$ROOT/pak" MOSAIC_LAUNCHER_RUN_DIR="$WORK/run" SDL_AUDIODRIVER=dummy \
			PRESSER_KEYS="wait $keys" PRESSER_STEP=450 PRESSER_FRAME="$frame" PRESSER_SHOT="$keep_after"
		exec "$ELF" $elf_args
	) >"$log" 2>&1 &
	pid=$!
	waited=0
	while kill -0 "$pid" 2>/dev/null && [ "$waited" -lt $((WAIT * 10)) ]; do
		grep -qF "presser: done" "$log" && break
		sleep 0.1
		waited=$((waited + 1))
	done
	if kill -0 "$pid" 2>/dev/null; then
		grep -qF "presser: done" "$log" || fail "$plugin $1: presser wasn't done after ${WAIT}s; log in $log"
		kill "$pid"
		tries=0
		while kill -0 "$pid" 2>/dev/null && [ "$tries" -lt 50 ]; do
			sleep 0.1
			tries=$((tries + 1))
		done
		kill -0 "$pid" 2>/dev/null && kill -KILL "$pid"
	fi
	wait "$pid" 2>/dev/null
	# a launch leaves the launcher's own markers in /tmp, for the next start
	if grep -qF "[INFO] cmd: " "$log"; then
		rm -f /tmp/next /tmp/mosaic-launcher-launched /tmp/mosaic-launcher-home
	fi
	if [ ! -s "$frame.bmp" ]; then
		fail "$plugin $1: no frame saved; log in $log"
		return
	fi
	# 256 colours without dithering keeps the UI's flat colours exact and the files small
	"$MAGICK" "$frame.bmp" -alpha off -resize '640x480!' -dither None -colors 256 -strip \
		-define png:exclude-chunks=date,time -define png:compression-level=9 "PNG8:$out" \
		|| fail "$plugin $1: converting the frame"
	count=$((count + 1))
	echo "  ${out#"$ROOT"/}"
}

if [ $# -eq 0 ]; then
	set -- "$SHOTS"/shots/*.sh
else
	for id in "$@"; do
		shift
		set -- "$@" "$SHOTS/shots/$id.sh"
	done
fi
for file in "$@"; do
	plugin=$(basename "$file" .sh)
	if [ ! -f "$file" ]; then
		fail "$plugin has no shots in $file"
		continue
	fi
	echo "$plugin"
	# a shot renamed or dropped leaves no picture behind
	rm -f "$(docs_dir)/"*.png
	# shellcheck source=/dev/null
	. "$file"
done
rm -rf "$CARD"

if [ "$failures" -gt 0 ]; then
	echo "$failures screenshot(s) failed" >&2
	exit 1
fi
echo "ok screenshots: $count"
