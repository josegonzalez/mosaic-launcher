# shellcheck shell=sh
# shard: 1

# painter generates art with art_surface: art_get hands it out read-only,
# off the plugin's budget, asked for once; a real file wins and art_find
# reports files only (writing where stock would look); the built-in list shows a generated thumbnail for a
# game with no art file, seen in the push's old frame; and art_surface
# can't ask for generated art or move the selection
PAINT_DIR="$WORK/paint"
mkdir -p "$PAINT_DIR" "$STATE"
cp "$ROOT/tests/plugins/drawer/art.png" "$PAINT_DIR/Real-thumb.png"
printf 'dir=%s\n' "$PAINT_DIR" >"$STATE/painter.cfg"
printf 'painter=on\n' >"$STATE/plugins.txt"
build_plugin painter 'id=painter\nname=Painter\napi=1\nslots=transition\n'
run_launcher "$WORK/painter.log" "plugin painter: done" || fail "the launcher exited early with painter loaded"
log="$WORK/painter.log"
plugin=painter
expect "open folder 1"
expect "asked Blank 0"
expect "inside get -1 select -1"
expect "blank box 1 1 same 1, 64x64 FF00FF"
expect "blank box read-only: fill -1, free -1"
expect "blank thumbnail 1 00FF00"
expect "blank find 0 \"$CARD/Painter/.media/Blank.png\""
expect "shy box -1 -1"
expect "budget 1"
expect "real thumbnail 1 FF0000"
expect "asks 3"
expect "open next 1"
expect "push to next old thumbnail green 1"
[ "$(grep -cE "plugin painter: asked (Blank|Real|Shy) " "$log")" -eq 3 ] || fail "painter was asked more than once for the same art: $(grep "plugin painter: asked" "$log" | tr '\n' ',')"
grep -qF "plugin painter: asked Real" "$log" && fail "painter was asked for art Real has a file for"
grep -qF "plugins: painter's art_surface" "$log" && fail "painter's art_surface was turned off"

# handing over the same surface twice is an error: it's not asked again
printf 'dir=%s\nmode=again\n' "$PAINT_DIR" >"$STATE/painter.cfg"
run_launcher "$WORK/painter-again.log" "plugin painter: done" || fail "the launcher exited early with painter's error"
log="$WORK/painter-again.log"
grep -qF "plugins: painter's art_surface returned a surface it didn't make, no longer asking it for art until the launcher restarts" "$log" \
	|| fail "painter's error wasn't logged"
expect "blank box -1 -1 same 0"
expect "shy box -1 -1"
expect "asks 2"
rm -rf "$WORK/plugins/painter.plugin" "$PAINT_DIR" "$STATE/painter.cfg" "$STATE/plugins.txt"
grep -qF "plugin painter:" "$WORK/hello.log" && fail "art was generated with no plugin making it"
