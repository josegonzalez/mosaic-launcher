# shellcheck shell=sh
# shard: 1

# row-badges puts an unplayed dot and tier dots after rows' names, which
# dotter, after it in load order, reads back off the screen. Alpha is
# unplayed with tier 3, Bravo has a save and tier 1, Charlie a resume slot,
# Delta a state and Echo is in recents, and Foxtrot is unplayed with a tier
# that isn't one
BADGE_DIR="$CARD/Roms/Row Badges (RBT)"
SHARED="$CARD/.userdata/shared"
mkdir -p "$BADGE_DIR/.media" "$CARD/Saves/RBT" "$SHARED/RBT-test" "$SHARED/.minui/RBT" "$STATE"
for game in Alpha Bravo Charlie Delta Echo Foxtrot; do : >"$BADGE_DIR/$game.gb"; done
printf 'tier=3\n' >"$BADGE_DIR/.media/Alpha.info"
printf 'tier=1\n' >"$BADGE_DIR/.media/Bravo.info"
printf 'tier=high\n' >"$BADGE_DIR/.media/Foxtrot.info"
: >"$CARD/Saves/RBT/Bravo.gb.sav"
printf '0' >"$SHARED/.minui/RBT/Charlie.gb.txt"
: >"$SHARED/RBT-test/Delta.gb.st0"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/recent.txt.saved"
echo "/Roms/Row Badges (RBT)/Echo.gb" >"$RECENTS"
make -s -C "$ROOT/plugins" row-badges ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building row-badges"
build_plugin dotter 'id=dotter\nname=Dotter\napi=1\n'
printf 'folder=Roms/Row Badges (RBT)\n' >"$STATE/dotter.cfg"
printf 'unplayed=on\ntiers=on\n' >"$STATE/row-badges.cfg"
printf 'row-badges=on\ndotter=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/row-badges.log" "plugin dotter: row Foxtrot" || fail "the launcher exited early with row-badges loaded"
log="$WORK/row-badges.log"
plugin=row-badges
expect "unplayed on, tiers on"
plugin=dotter
expect "open folder 1"
expect "row Alpha unplayed 1 tiers 3"
expect "row Bravo unplayed 0 tiers 1"
expect "row Charlie unplayed 0 tiers 0"
expect "row Delta unplayed 0 tiers 0"
expect "row Echo unplayed 0 tiers 0"
expect "row Foxtrot unplayed 1 tiers 0"
grep -qF "plugins: row-badges's" "$log" && fail "row-badges' decorations were turned off"

# both badges are off by default
rm -f "$STATE/row-badges.cfg"
run_launcher "$WORK/row-badges-off.log" "plugin dotter: row Foxtrot" || fail "the launcher exited early with row-badges' defaults"
log="$WORK/row-badges-off.log"
plugin=row-badges
expect "unplayed off, tiers off"
plugin=dotter
expect "row Alpha unplayed 0 tiers 0"
expect "row Foxtrot unplayed 0 tiers 0"

# under a layout, cells with no name get the dots in their top right corner
mkdir -p "$CARD/Gridder/.media"
printf 'tier=2\n' >"$CARD/Gridder/.media/Apple.info"
printf 'unplayed=on\ntiers=on\n' >"$STATE/row-badges.cfg"
printf 'open=0\nfolder=Gridder\n' >"$STATE/dotter.cfg"
build_plugin gridder 'id=gridder\nname=Gridder\napi=1\nslots=layout\n'
printf 'gridder=on\nrow-badges=on\ndotter=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/row-badges-grid.log" "plugin gridder: renders" "plugin dotter: row Banana" || fail "the launcher exited early with row-badges under gridder"
log="$WORK/row-badges-grid.log"
plugin=dotter
expect "row Apple unplayed 0 tiers 2"
expect "row Banana unplayed 0 tiers 0"
grep -qF "plugins: row-badges's" "$log" && fail "row-badges' decorations were turned off under gridder"

# under the bundled layout-grid, which names each cell, the dots go after
# the cell's name
rm -rf "$WORK/plugins/gridder.plugin" "$CARD/Gridder"
make -s -C "$ROOT/plugins" layout-grid ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building layout-grid"
printf 'folder=Roms/Row Badges (RBT)\n' >"$STATE/dotter.cfg"
printf 'layout-grid=on\nrow-badges=on\ndotter=on\nslot.layout=layout-grid\n' >"$STATE/plugins.txt"
run_launcher "$WORK/row-badges-layout-grid.log" "plugin dotter: row Foxtrot" || fail "the launcher exited early with row-badges under layout-grid"
log="$WORK/row-badges-layout-grid.log"
plugin=dotter
expect "row Alpha unplayed 1 tiers 3"
expect "row Bravo unplayed 0 tiers 1"
expect "row Charlie unplayed 0 tiers 0"
expect "row Foxtrot unplayed 1 tiers 0"
grep -qF "plugins: row-badges's" "$log" && fail "row-badges' decorations were turned off under layout-grid"
grep -qF "plugins: layout-grid's" "$log" && fail "layout-grid was turned off with row-badges"
rm -rf "$WORK/plugins/dotter.plugin" "$WORK/plugins/row-badges.plugin" "$WORK/plugins/layout-grid.plugin" \
	"$BADGE_DIR" "$CARD/Saves/RBT" "$SHARED/RBT-test" "$SHARED/.minui/RBT" \
	"$STATE/dotter.cfg" "$STATE/row-badges.cfg" "$STATE/plugins.txt"
if [ -f "$WORK/recent.txt.saved" ]; then cp "$WORK/recent.txt.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
