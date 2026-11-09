# shellcheck shell=sh
# shard: 2

# themer owns the theme slot (and the transition slot, to read the screen
# back): its colours, background colour, font, text step and UI sizes take
# effect, ui_size and theme_color report them, a pill's end is stock's
# sheet at stock's size and smooth-scaled at the theme's, a wallpaper
# fills a spot with no stock background but never covers a hand-placed
# bg.png, bad values are refused one by one, and an error, at the start or
# on a reload, puts stock's theme back. Without slots=theme it's never
# asked.
build_plugin themer 'id=themer\nname=Themer\napi=1\nslots=theme,transition\n'
printf 'themer=on\n' >"$STATE/plugins.txt"
plugin=themer
TH_DIR="$CARD/Themes/Themer"
mkdir -p "$TH_DIR"
cp "$CARD/.system/res/font2.ttf" "$TH_DIR/font.ttf"
# th_png <file> <r> <g> <b>: an 8x8 PNG of one colour
th_png() {
	python3 -I -c '
import struct, sys, zlib
r, g, b = (int(v) for v in sys.argv[2:5])
def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
rows = b"".join(b"\0" + bytes([r, g, b]) * 8 for _ in range(8))
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 8, 8, 8, 2, 0, 0, 0)) \
    + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
open(sys.argv[1], "wb").write(png)
' "$@" || fail "writing $1"
}
th_png "$TH_DIR/green.png" 0 255 0
th_png "$TH_DIR/blue.png" 0 0 255
th_png "$TH_DIR/red.png" 255 0 0

# th_run <name> <config>: the launcher with themer's config, until it's done
th_run() {
	log="$WORK/$1.log"
	printf '%b' "$2" >"$STATE/themer.cfg"
	run_launcher "$log" "plugin themer: done" || fail "the launcher exited early with themer ($1)"
}
# th_value <what> <log>: a number themer logged, as "<what> <n>"
th_value() { sed -n "s/.*plugin themer: start .*$1 \([0-9-]*\).*/\1/p" "$2" | head -n 1; }
# th_edge <stock|smooth>: the end of a hint pill in $log is stock's sheet
# unscaled, or smooth-scaled from it (see themer/edge.py)
th_edge() {
	edge=$(grep -m 1 "plugin themer: edge pill" "$log") || fail "themer logged no hint pill edge ($1)"
	python3 -I "$ROOT/tests/plugins/themer/edge.py" "$1" "$CARD/.system/res" "$edge" \
		|| fail "the hint pill's end isn't $1: $edge"
}

# stock's: nothing set, and with no plugin taking the slot
th_run themer-empty 'mode=empty\n'
expect "init empty, asked 0"
expect "asked 1, size matches"
expect "start sizes pill 30 button 20 padding 5 rows 7 step 0"
stock_bg=$(sed -n 's/.*plugin themer: start colors \([0-9A-F ]*\), text.*/\1/p' "$log" | awk '{print $7}')
stock_colors=$(sed -n 's/.*plugin themer: start colors \([0-9A-F ]*\), text.*/\1/p' "$log")
stock_text=$(th_value text "$log")
expect "frame $stock_bg"
th_edge stock
grep -qF "row 90, other -1" "$log" || fail "stock's row isn't 90 high: $(grep "start colors" "$log")"

# colours, font, text step and sizes
th_run themer-good "mode=good\nfont=$TH_DIR/font.ttf\n"
grep -qF "plugins: themer supplies the theme: pill 36, button 24, padding 8, 5 rows, text step 2, font $(cd "$TH_DIR" && pwd -P)/font.ttf" "$log" \
	|| fail "themer's theme wasn't put in place: $(grep "supplies the theme" "$log")"
expect "start sizes pill 36 button 24 padding 8 rows 5 step 2"
expect "start colors ABCDEF $(echo "$stock_colors" | cut -d' ' -f2-6) 123456"
grep -qF "row 108," "$log" || fail "the theme's row isn't 108 high: $(grep "start colors" "$log")"
[ "$(th_value text "$log")" -gt "$stock_text" ] || fail "the text step didn't make text taller"
expect "frame 123456"
th_edge smooth
grep -qF "refused" "$log" && fail "a good theme had something refused"
grep -qF "so stock's is used" "$log" && fail "a good theme had something refused: $(grep "so stock's is used" "$log")"

# a wallpaper fills spots with no stock background: a game's folder takes
# the list one, and the main one without it
th_run themer-walls "mode=good\nwallpaper=$TH_DIR/green.png\nwallpaper_list=$TH_DIR/blue.png\n"
expect "frame 0000FF"
th_run themer-wall "mode=good\nwallpaper=$TH_DIR/green.png\n"
expect "frame 00FF00"
# a bg.png placed by hand wins
cp "$TH_DIR/red.png" "$CARD/bg.png"
th_run themer-stock-bg "mode=good\nwallpaper=$TH_DIR/green.png\nwallpaper_list=$TH_DIR/blue.png\n"
rm -f "$CARD/bg.png"
expect "frame FF0000"

# bad values are refused one by one; the rest of the theme stays
mkdir -p "$WORK/off-card"
cp "$TH_DIR/font.ttf" "$WORK/off-card/font.ttf"
ln -sf "$WORK/off-card/font.ttf" "$TH_DIR/escape.ttf"
printf 'not a font\n' >"$TH_DIR/text.ttf"
th_run themer-bad "mode=bad\nfont=$TH_DIR/escape.ttf\nwallpaper=Themes/Themer/green.png\nwallpaper_list=$TH_DIR\nwallpaper_quick=$TH_DIR/none.png\n"
for refused in "pill_size 200 isn't from 18 to 60" "padding -1 isn't from 1 to 30" "color2 0x1000000 isn't 0xRRGGBB" \
	"contrast 5 isn't from 10 to 210" "font ($TH_DIR/escape.ttf) isn't on the card" \
	"wallpaper (Themes/Themer/green.png) isn't a full path" "wallpaper_list ($TH_DIR) isn't a regular file" \
	"wallpaper_quick ($TH_DIR/none.png) doesn't exist"; do
	grep -qF "plugins: themer's theme $refused, so stock's is used" "$log" || fail "themer's bad \"$refused\" wasn't refused"
done
expect "start sizes pill 30 button 20 padding 5 rows 7 step 0"
expect "start colors $stock_colors"
expect "frame $stock_bg"
th_run themer-bad-font "mode=good\nfont=$TH_DIR/text.ttf\n"
grep -qF "plugins: themer's theme font ($(cd "$TH_DIR" && pwd -P)/text.ttf) won't open, so stock's is used" "$log" \
	|| fail "a file that isn't a font was used"
expect "start sizes pill 36 button 24 padding 8 rows 5 step 2"

# an error puts stock's theme back, at the start and on a reload
th_run themer-fail 'mode=fail\n'
grep -qF "plugins: themer's theme failed (7), using stock's until the launcher restarts" "$log" || fail "themer's error wasn't logged"
expect "start sizes pill 30 button 20 padding 5 rows 7 step 0"
expect "start colors $stock_colors"
expect "frame $stock_bg"
th_run themer-reload 'mode=reload\n'
expect "start sizes pill 36 button 24 padding 8 rows 5 step 2"
expect "asked 2, size matches"
grep -qF "plugins: themer's theme failed (7)" "$log" || fail "themer's error on the reload wasn't logged"
expect "reloaded sizes pill 30 button 20 padding 5 rows 7 step 0"
expect "reloaded colors $stock_colors"
grep -qF "asked 3" "$log" && fail "themer was asked again after its error"

# without the slot it's never asked
printf 'id=themer\nname=Themer\napi=1\nslots=transition\n' >"$WORK/plugins/themer.plugin/plugin.txt"
th_run themer-unslotted 'mode=good\n'
grep -qF "plugins: themer has a theme but no slots=theme, so it won't be asked" "$log" || fail "the missing theme slot wasn't reported"
grep -qF "plugin themer: asked" "$log" && fail "themer was asked without the slot"
expect "start sizes pill 30 button 20 padding 5 rows 7 step 0"

rm -rf "$WORK/plugins/themer.plugin" "$CARD/Themes" "$WORK/off-card" "$STATE/themer.cfg" "$STATE/plugins.txt"
grep -qF "supplies the theme" "$WORK/hello.log" && fail "a theme was put in place with no plugin taking the slot"
