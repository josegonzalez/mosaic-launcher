# shellcheck shell=sh
# shard: 1

# reeler shows a surface in place of the selected game's art under the
# bundled layout-grid, which draws it in the selected tile, and
# layout-fancy, which draws it in its panel with art decorators under it:
# new frames of it redraw the layout with no request_redraw, and it goes
# when cleared or when the selection moves, leaving the game's own art.
# Under gridder, which doesn't draw it, new frames don't redraw the list
REEL_DIR="$CARD/Reeler"
mkdir -p "$REEL_DIR/.media" "$STATE"
cp "$ROOT/tests/plugins/peeker/art.png" "$REEL_DIR/.media/Apple.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$REEL_DIR/.media/Banana.png"
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" layout-grid layout-fancy >/dev/null || fail "building the layouts"
build_plugin reeler 'id=reeler\nname=Reeler\napi=1\n'
# check_reeler <layout> <what reeler reads>: the case's lines in $log
check_reeler() {
	plugin=reeler
	expect "open folder 1"
	expect "$2 Apple 00FF00"
	expect "show 0"
	expect "$2 Apple FF0000"
	expect "shown FF0000"
	grep -qE "plugin reeler: frames 30 failed 0, redraws ([2-9][0-9]|[1-9][0-9][0-9]), now 0000FF, " "$log" \
		|| fail "new frames didn't redraw $1: $(grep "plugin reeler: frames" "$log")"
	expect "$2 Apple 0000FF"
	expect "clear 0"
	expect "cleared 00FF00"
	expect "show again 0"
	expect "select Banana 0"
	expect "moved Banana 00FF00"
	grep -qF "plugins: $1's" "$log" && fail "$1's layout was turned off with reeler"
	grep -qF "plugins: reeler's" "$log" && fail "reeler's decorators were turned off under $1"
	grep -F "plugin reeler: frames" "$log"
}
printf 'layout-grid=on\nreeler=on\nslot.layout=layout-grid\n' >"$STATE/plugins.txt"
run_launcher "$WORK/reeler-grid.log" "plugin reeler: done" || fail "the launcher exited early with reeler under layout-grid"
log="$WORK/reeler-grid.log"
check_reeler layout-grid cell
printf 'read=art\n' >"$STATE/reeler.cfg"
printf 'layout-fancy=on\nreeler=on\nslot.layout=layout-fancy\n' >"$STATE/plugins.txt"
run_launcher "$WORK/reeler-fancy.log" "plugin reeler: done" || fail "the launcher exited early with reeler under layout-fancy"
log="$WORK/reeler-fancy.log"
check_reeler layout-fancy art
# the panel's art, under which art decorators go: the thumbnail, then the frame fitted across the panel
grep -qE "plugin reeler: art Apple 00FF00 rect 519,[0-9]+ [0-9]+x[0-9]+$" "$log" || fail "layout-fancy's art rect is wrong: $(grep "plugin reeler: art Apple" "$log")"
grep -qE "plugin reeler: art Apple FF0000 rect 519,[0-9]+ 460x306$" "$log" || fail "layout-fancy's panel doesn't fit the frame: $(grep "plugin reeler: art Apple FF0000" "$log")"
rm -rf "$WORK/plugins/layout-grid.plugin" "$WORK/plugins/layout-fancy.plugin"
build_plugin gridder 'id=gridder\nname=Gridder\napi=1\nslots=layout\n'
printf 'plain=1\n' >"$STATE/reeler.cfg"
printf 'gridder=on\nreeler=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/reeler-gridder.log" "plugin reeler: done" || fail "the launcher exited early with reeler under gridder"
log="$WORK/reeler-gridder.log"
grep -qF "plugins: gridder draws the game list" "$log" || fail "gridder didn't take the game list"
expect "show 0"
grep -qE "plugin reeler: frames 30 failed 0, redraws [0-2], " "$log" \
	|| fail "new frames redrew a layout that doesn't draw them: $(grep "plugin reeler: frames" "$log")"
rm -rf "$WORK/plugins/reeler.plugin" "$WORK/plugins/gridder.plugin" "$WORK/plugins/layout-grid.plugin" \
	"$WORK/plugins/layout-fancy.plugin" "$REEL_DIR" "$STATE/reeler.cfg" "$STATE/plugins.txt"
