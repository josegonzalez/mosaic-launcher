# shellcheck shell=sh
# shard: 3

# drawer tries every drawing call on surfaces of its own, reading pixels
# back, and has bad arguments, a FIFO, a folder, a file that isn't an
# image, a PNG whose header says it's just over 4096 pixels wide, and a PNG
# and a JPEG whose headers say they're huge refused, while a BMP still loads; the surfaces it leaves are freed after its quit, and
# when its init declines
DRAW_DIR="$WORK/drawer"
mkdir -p "$DRAW_DIR" "$STATE"
mkfifo "$DRAW_DIR/fifo"
echo "not an image" >"$DRAW_DIR/text.png"
# a PNG's signature and IHDR, 100000x100000, and a JPEG's SOI and SOF0, 65535x65535
printf '\211PNG\r\n\032\n\000\000\000\rIHDR\000\001\206\240\000\001\206\240\010\006\000\000\000\000\000\000\000' >"$DRAW_DIR/huge.png"
printf '\377\330\377\300\000\021\010\377\377\377\377\003' >"$DRAW_DIR/huge.jpg"
# a PNG's signature and IHDR, 4097x1, with no image data, so only its header can refuse it
printf '\211PNG\r\n\032\n\000\000\000\rIHDR\000\000\020\001\000\000\000\001\010\006\000\000\000\000\000\000\000' >"$DRAW_DIR/wide.png"
# a 2x1 24-bit BMP, red then blue
printf 'BM\076\000\000\000\000\000\000\000\066\000\000\000\050\000\000\000\002\000\000\000\001\000\000\000\001\000\030\000' >"$DRAW_DIR/art.bmp"
printf '\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000' >>"$DRAW_DIR/art.bmp"
printf '\000\000\377\377\000\000\000\000' >>"$DRAW_DIR/art.bmp"
printf 'art=%s\ndir=%s\ntext=%s\nfifo=%s\nhuge_png=%s\nhuge_jpeg=%s\nbmp=%s\nwide_png=%s\n' "$ROOT/tests/plugins/drawer/art.png" "$DRAW_DIR" \
	"$DRAW_DIR/text.png" "$DRAW_DIR/fifo" "$DRAW_DIR/huge.png" "$DRAW_DIR/huge.jpg" "$DRAW_DIR/art.bmp" "$DRAW_DIR/wide.png" >"$STATE/drawer.cfg"
printf 'drawer=on\n' >"$STATE/plugins.txt"
build_plugin drawer 'id=drawer\nname=Drawer\napi=1\n'
run_launcher "$WORK/drawer.log" "plugin drawer: leaving 7 surfaces" "plugins: loaded drawer" || fail "the launcher exited early with drawer loaded"
log="$WORK/drawer.log"
plugin=drawer
expect "screen 1024x768 scale 3"
expect "new ok, zero NULL, too wide NULL, not loaded NULL, no id NULL"
expect "size 0 40x20, null -1, unknown -1, no out -1"
expect "new pixel 000000 00, outside - -"
expect "budget first ok, second NULL, freed 0"
expect "fill 0, pixel FF0000 FF"
expect "blend 0, half 1, outside FF0000 FF"
expect "fill bad rect -1, null -1, unknown -1, clear 0"
expect "off the edge 0, huge -1, pixel FF0000 FF 00FF00 FF"
expect "round 0, corner 000000 00, edge 00FF00 FF, middle 00FF00 FF, negative -1"
expect "theme fill 1"
expect "image ok 4x2, left FF0000 FF, right 0000FF FF"
expect "image dir NULL, missing NULL, not an image NULL, fifo NULL, null NULL, not loaded NULL"
expect "image wide png NULL"
expect "image huge png NULL, huge jpeg NULL, bmp ok 2x1, left FF0000 FF, right 0000FF FF"
expect "contain ok, bar 000000 00, left FF0000 FF, right 0000FF FF"
expect "cover ok, left FF0000 FF, right 0000FF FF"
expect "stretch ok, FF0000 FF 0000FF FF"
expect "scale bad fit NULL, zero NULL, null NULL"
expect "blit 0 FF0000 FF 0000FF FF, stretched 0 FF0000 FF 0000FF FF, part 0 0000FF FF, outside 000000 00"
expect "blit onto itself -1, null -1, bad rect -1"
expect "text width 1, height 1, smaller 1, two lines 1, bad font -1 -1, null -1"
expect "truncate fits 1, dots 1, whole 1, small out 1"
expect "truncate no room -1, negative -1, null -1"
expect "draw text 1, inked 1, truncated 1, null -1, bad font -1, negative -1"
expect "free 0, again -1, not mine -1, null -1, then fill -1, then blit -1"
grep -qF "plugins: refused a 2048x2048 surface for drawer, its surfaces would take over 32 MiB" "$log" || fail "the refused surface wasn't logged"
grep -qF "plugins: drawer can't load $DRAW_DIR/fifo" "$log" || fail "the refused FIFO wasn't logged"
grep -qF "plugins: drawer can't load $DRAW_DIR/huge.png: 100000x100000 is over 8192 a side or 40000000 pixels" "$log" \
	|| fail "the huge PNG wasn't refused by its header"
grep -qF "plugins: drawer can't load $DRAW_DIR/huge.jpg: 65535x65535 is over 8192 a side or 40000000 pixels" "$log" \
	|| fail "the huge JPEG wasn't refused by its header"
grep -qF "plugins: drawer can't load $DRAW_DIR/wide.png: 4097x1 is over 4096 a side" "$log" \
	|| fail "the PNG over 4096 a side wasn't refused by its header"
grep -q "plugins: freed .* surface\|plugins: refused a .* surface" "$WORK/hello.log" && fail "surfaces were made without a plugin asking"

# a launch quits the launcher, running plugins' quit; the card's recents
# and /tmp/next are put back afterwards, and /tmp/last.txt after the next case
PAK="$DRAW_DIR/Drawn.pak"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/recent.txt.saved"
[ -f /tmp/next ] && cp /tmp/next "$WORK/next.saved"
[ -f /tmp/last.txt ] && cp /tmp/last.txt "$WORK/last.txt.saved"
mkdir -p "$PAK"
: >"$PAK/launch.sh"
echo "pak=$PAK" >>"$STATE/drawer.cfg"
run_launcher "$WORK/drawer-quit.log" && fail "the launcher didn't quit to launch drawer's pak"
log="$WORK/drawer-quit.log"
expect "open pak 1"
quit_line=$(grep -nF "plugin drawer: quit" "$log" | cut -d: -f1)
freed_line=$(grep -nF "plugins: freed 7 surfaces drawer left" "$log" | cut -d: -f1)
if [ -z "$quit_line" ] || [ -z "$freed_line" ]; then
	fail "drawer's quit didn't run, or its surfaces weren't freed after it"
elif [ "$freed_line" -lt "$quit_line" ]; then
	fail "drawer's surfaces were freed before its quit"
fi

if [ -f "$WORK/recent.txt.saved" ]; then cp "$WORK/recent.txt.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
if [ -f "$WORK/next.saved" ]; then cp "$WORK/next.saved" /tmp/next; else rm -f /tmp/next; fi
grep -qxF "$PAK" /tmp/last.txt 2>/dev/null || fail "opening drawer's pak didn't save it as the last path"
rm -f "$WORK/recent.txt.saved" "$WORK/next.saved"
printf 'pak=\ndecline=1\n' >>"$STATE/drawer.cfg"
run_launcher "$WORK/drawer-decline.log" "plugins: drawer declined to start" || fail "the launcher exited early after drawer declined"
log="$WORK/drawer-decline.log"
expect "declining"
grep -qF "plugins: freed 7 surfaces drawer left" "$log" || fail "drawer's surfaces weren't freed when it declined"
grep -qF "plugin drawer: quit" "$log" && fail "a declined plugin's quit ran"
rm -rf "$WORK/plugins/drawer.plugin" "$DRAW_DIR" "$STATE/drawer.cfg" "$STATE/plugins.txt"
