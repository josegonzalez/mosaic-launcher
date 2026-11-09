# shellcheck shell=sh
# shard: 2

# the bundled second-screen shows the selection in a second window, drawn
# by its own second-screen-display program: clipper opens a folder and
# moves from Apple (green art and a .info sidecar) to Banana (neither),
# and the program saves each frame it shows, read back from its window,
# to $MOSAIC_SECOND_SCREEN_SHOT, so the art's middle can be checked there
SS_DIR="$CARD/Roms/Second Screen (SST)"
SS_SHOTS="$WORK/second-screen-shots"
mkdir -p "$SS_DIR/.media" "$STATE"
: >"$SS_DIR/Apple.gb"
: >"$SS_DIR/Banana.gb"
cp "$ROOT/tests/plugins/peeker/art.png" "$SS_DIR/.media/Apple.png"
printf 'year=1994-11-21\npublisher=Nintendo\ngenre=Platformer\ntier=3\n' >"$SS_DIR/.media/Apple.info"
make -s -C "$ROOT/plugins" second-screen ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building second-screen"
build_plugin clipper 'id=clipper\nname=Clipper\napi=1\n'
printf 'folder=Roms/Second Screen (SST)\nstep=2000\n' >"$STATE/clipper.cfg"
printf 'second-screen=on\nclipper=on\n' >"$STATE/plugins.txt"

# ss_frame <name> <what>: the number of the last frame second-screen drew
# for name, logged as what (eg. "no art, 1 title line, Game, 0 info lines")
ss_frame() {
	grep -F "plugin second-screen: frame " "$log" | grep -F ": $1: $2, read in " |
		sed -n 's/.*plugin second-screen: frame \([0-9]*\): .*/\1/p' | tail -n 1
}

# ss_pixel <bmp> <x> <y>: the pixel as "r g b" in a 24-bit BMP the display
# saved, at the frame's coordinates (frame_w wide), wherever its rows start
ss_pixel() {
	off=$(od -An -tu4 -j10 -N4 "$1" | tr -d ' ')
	bw=$(od -An -tu4 -j18 -N4 "$1" | tr -d ' ')
	bh=$(od -An -tu4 -j22 -N4 "$1" | tr -d ' ')
	s=$((bw / frame_w))
	[ "$s" -ge 1 ] || s=1
	stride=$(((bw * 3 + 3) / 4 * 4))
	row=$((bh - 1 - $3 * s))
	od -An -tu1 -j$((off + row * stride + $2 * s * 3)) -N3 "$1" | awk 'NF { print $3, $2, $1; exit }'
}

# ss_check <name> <what> <x> <y> <want>: second-screen drew name's frame
# as what, and the display showed it with "r g b" want at x, y, or with
# "not r g b" anything else there
ss_check() {
	n=$(ss_frame "$1" "$2")
	if [ -z "$n" ]; then
		fail "second-screen drew no frame for $1 as \"$2\": $(grep -F "plugin second-screen: frame " "$log")"
		return
	fi
	grep -qF "second-screen-display: showing frame $n, " "$log" || fail "the display didn't show $1's frame $n"
	shot="$SS_SHOTS/frame-$n.bmp"
	if [ ! -s "$shot" ]; then
		fail "the display saved no frame $n for $1"
		return
	fi
	got=$(ss_pixel "$shot" "$3" "$4")
	case "$5" in
		"not $got") fail "$1's frame $n is \"$got\" at $3,$4" ;;
		"not "*) ;;
		"$got") ;;
		*) fail "$1's frame $n is \"$got\" at $3,$4, wanted \"$5\"" ;;
	esac
}

# 640x480 by default: the art in a square on the left, the text beside it
rm -rf "$SS_SHOTS"
mkdir -p "$SS_SHOTS"
frame_w=640
MOSAIC_SECOND_SCREEN_SHOT="$SS_SHOTS" run_launcher "$WORK/second-screen.log" "plugin clipper: done" \
	"second-screen-display: showing frame 2," || fail "the launcher exited early with second-screen loaded"
log="$WORK/second-screen.log"
plugin=second-screen
expect "showing the selection in a 640x480 window, experimental, frames in /tmp/mosaic-second-screen-"
grep -qF "second-screen-display: opened a 640x480 window with " "$log" || fail "the display didn't open its window"
ss_check Apple "art 8x8 at 24,98 284x284, 1 title line, Game, 3 info lines" 166 240 "0 255 0"
ss_check Banana "no art, 1 title line, Game, 0 info lines" 166 240 "not 0 255 0"
grep -qF "plugins: second-screen" "$log" && fail "the launcher turned second-screen off: $(grep -F "plugins: second-screen" "$log")"
grep -qE "plugin second-screen: (couldn't|can't)" "$log" && fail "second-screen failed: $(grep -E "plugin second-screen: (couldn't|can't)" "$log")"
# the display quits with the launcher and takes its frame file with it
ss_file=$(sed -n 's/.*frames in \(\/tmp\/mosaic-second-screen-[0-9]*\.frame\).*/\1/p' "$log" | head -n 1)
sleep 1
[ -n "$ss_file" ] && [ -e "$ss_file" ] && fail "the display left $ss_file behind"

# a narrower window has the art on top and the text under it
rm -rf "$SS_SHOTS"
mkdir -p "$SS_SHOTS"
frame_w=480
printf 'size=480x640\n' >"$STATE/second-screen.cfg"
MOSAIC_SECOND_SCREEN_SHOT="$SS_SHOTS" run_launcher "$WORK/second-screen-tall.log" "plugin clipper: done" \
	"second-screen-display: showing frame 2," || fail "the launcher exited early with second-screen's size set"
log="$WORK/second-screen-tall.log"
expect "showing the selection in a 480x640 window, experimental"
grep -qF "second-screen-display: opened a 480x640 window with " "$log" || fail "the display didn't open its tall window"
ss_check Apple "art 8x8 at 98,24 284x284, 1 title line, Game, 3 info lines" 240 166 "0 255 0"

rm -rf "$WORK/plugins/clipper.plugin" "$WORK/plugins/second-screen.plugin" "$SS_DIR" "$SS_SHOTS" \
	"$STATE/clipper.cfg" "$STATE/second-screen.cfg" "$STATE/plugins.txt"
