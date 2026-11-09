# shellcheck shell=sh
# shard: 1

# the bundled layout-fancy draws glancer's folder as stock's rows beside a
# panel, which glancer reads down a column on each selected row: Alpha has
# a green thumbnail and a blue preview of its last state (slot 1), which
# stands in for the thumbnail; Beta has only its thumbnail; Gamma's last
# slot has no preview and it has no art, so its panel is empty. With
# prefer_preview off, Alpha's thumbnail goes on top and its preview under it.
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" layout-fancy >/dev/null || fail "building layout-fancy"
build_plugin glancer 'id=glancer\nname=Glancer\napi=1\n'
GLANCE_DIR="$CARD/Roms/Glancer (GLANCE)"
GLANCE_STATES="$CARD/.userdata/shared/.minui/GLANCE"
mkdir -p "$GLANCE_DIR/.media" "$GLANCE_STATES" "$STATE"
cp "$ROOT/tests/plugins/peeker/art.png" "$GLANCE_DIR/.media/Alpha.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$GLANCE_DIR/.media/Beta.png"
printf '1' >"$GLANCE_STATES/Alpha.gb.txt"
cp "$ROOT/tests/plugins/glancer/state.bmp" "$GLANCE_STATES/Alpha.gb.1.bmp"
cp "$ROOT/tests/plugins/glancer/state.bmp" "$GLANCE_STATES/Alpha.gb.0.bmp"
printf '3\n' >"$GLANCE_STATES/Gamma.gb.txt"
cp "$ROOT/tests/plugins/glancer/state.bmp" "$GLANCE_STATES/Gamma.gb.0.bmp"
printf 'layout-fancy=on\nglancer=on\nslot.layout=layout-fancy\n' >"$STATE/plugins.txt"
run_launcher "$WORK/layout-fancy.log" "plugin glancer: done" || fail "the launcher exited early with layout-fancy loaded"
log="$WORK/layout-fancy.log"
plugin=glancer
grep -qF "plugins: layout-fancy draws the game list" "$log" || fail "layout-fancy didn't take the game list"
grep -qF "plugin layout-fancy: rows 443 wide (443 at the top), panel 519," "$log" || fail "layout-fancy's layout is wrong: $(grep "plugin layout-fancy:" "$log")"
expect "open folder 1"
alpha=$(grep "plugin glancer: panel Alpha" "$log" | tail -n 1)
echo "$alpha" | grep -qE "panel Alpha runs 0000FF:[0-9]+-[0-9]+$" || fail "Alpha's state preview didn't stand in for its thumbnail: $alpha"
beta=$(grep "plugin glancer: panel Beta" "$log" | tail -n 1)
echo "$beta" | grep -qE "panel Beta runs 00FF00:[0-9]+-[0-9]+$" || fail "Beta's thumbnail isn't in the panel: $beta"
expect "panel Gamma runs none"
grep -qF "plugins: layout-fancy's" "$log" && fail "layout-fancy's layout was turned off"
grep -qF "plugins: glancer's" "$log" && fail "glancer's decorator was turned off"

printf 'prefer_preview=off\n' >"$STATE/layout-fancy.cfg"
run_launcher "$WORK/layout-fancy-both.log" "plugin glancer: done" || fail "the launcher exited early with layout-fancy's prefer_preview off"
log="$WORK/layout-fancy-both.log"
alpha=$(grep "plugin glancer: panel Alpha" "$log" | tail -n 1)
echo "$alpha" | grep -qE "panel Alpha runs 00FF00:[0-9]+-[0-9]+ 0000FF:[0-9]+-[0-9]+$" || fail "Alpha's thumbnail and preview aren't both in the panel: $alpha"
expect "panel Gamma runs none"
rm -rf "$WORK/plugins/layout-fancy.plugin" "$WORK/plugins/glancer.plugin" "$GLANCE_DIR" "$GLANCE_STATES" \
	"$STATE/layout-fancy.cfg" "$STATE/plugins.txt"
