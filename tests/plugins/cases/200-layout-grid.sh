# shellcheck shell=sh
# shard: 3

# the bundled layout-grid draws peeker's folder as a grid: Apple's
# thumbnail from .media fills its cell's art, scaled from 8x8, and banana
# and Cherry, with no art, get letter tiles in the theme's accent colour
# with their letter across the middle; peeker reads each cell back as a
# row decorator. Its settings set the columns and rows, and with
# art-boxart on, Cherry's box stands in for a thumbnail.
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" layout-grid art-boxart >/dev/null || fail "building layout-grid"
build_plugin peeker 'id=peeker\nname=Peeker\napi=1\n'
PEEK_DIR="$CARD/Peeker"
mkdir -p "$PEEK_DIR/.media" "$STATE"
cp "$ROOT/tests/plugins/peeker/art.png" "$PEEK_DIR/.media/Apple.png"
printf 'layout-grid=on\npeeker=on\nslot.layout=layout-grid\n' >"$STATE/plugins.txt"
run_launcher "$WORK/layout-grid.log" "plugin peeker: cell Apple middle 00FF00" "plugin peeker: cell banana" \
	"plugin peeker: cell Cherry" || fail "the launcher exited early with layout-grid loaded"
log="$WORK/layout-grid.log"
plugin=peeker
grep -qF "plugins: layout-grid draws the game list" "$log" || fail "layout-grid didn't take the game list"
# 4 columns at 1024 wide; the rows depend on whether the status pill shows
grep -qE "plugin layout-grid: grid of 4x[23] cells, 250 by [0-9]+$" "$log" || fail "layout-grid's grid is wrong: $(grep "plugin layout-grid:" "$log")"
expect "open folder 1"
expect "cell Apple middle 00FF00 top 00FF00, accent 0, ink 0, named 1"
grep -qE "plugin peeker: cell banana middle [0-9A-F]{6} top [0-9A-F]{6}, accent 1, ink 1, named 1$" "$log" \
	|| fail "banana has no letter tile: $(grep "plugin peeker: cell banana" "$log")"
grep -qE "plugin peeker: cell Cherry middle [0-9A-F]{6} top [0-9A-F]{6}, accent 1, ink 1, named 1$" "$log" \
	|| fail "Cherry has no letter tile: $(grep "plugin peeker: cell Cherry" "$log")"
grep -qF "plugins: layout-grid's" "$log" && fail "layout-grid's layout was turned off"
grep -qF "plugins: peeker's" "$log" && fail "peeker's decorator was turned off"

printf 'columns=3\nrows=1\n' >"$STATE/layout-grid.cfg"
mkdir -p "$PEEK_DIR/.media/boxart2d"
cp "$ROOT/tests/plugins/peeker/art.png" "$PEEK_DIR/.media/boxart2d/Cherry.png"
printf 'art-boxart=on\nlayout-grid=on\npeeker=on\nslot.layout=layout-grid\n' >"$STATE/plugins.txt"
run_launcher "$WORK/layout-grid-set.log" "plugin peeker: cell Apple middle 00FF00" "plugin peeker: cell Cherry middle 00FF00" \
	|| fail "the launcher exited early with layout-grid set to 3 columns"
log="$WORK/layout-grid-set.log"
grep -qF "plugin layout-grid: grid of 3x1 cells" "$log" || fail "layout-grid's settings weren't used: $(grep "plugin layout-grid:" "$log")"
# the tiles are taller than the art now, so only their middles are art
expect "cell Apple middle 00FF00 top"
expect "cell Cherry middle 00FF00 top"

# with art-boxart's cover_name on and peeker's games as folders, Apple's
# cover stands in for its name, and banana, whose cover.png isn't an
# image, keeps its name on its letter tile though it's hidden, as does
# Cherry, with no cover
rm -rf "$PEEK_DIR" "$STATE/layout-grid.cfg"
mkdir -p "$PEEK_DIR/Apple" "$PEEK_DIR/banana" "$PEEK_DIR/Cherry"
cp "$ROOT/tests/plugins/peeker/art.png" "$PEEK_DIR/Apple/cover.png"
printf 'not an image\n' >"$PEEK_DIR/banana/cover.png"
printf 'cover_name=on\n' >"$STATE/art-boxart.cfg"
printf 'dirs=on\n' >"$STATE/peeker.cfg"
run_launcher "$WORK/layout-grid-hidden.log" "plugin peeker: cell Apple middle 00FF00" "plugin peeker: cell banana" \
	"plugin peeker: cell Cherry" || fail "the launcher exited early with art-boxart hiding names under layout-grid"
log="$WORK/layout-grid-hidden.log"
expect "cell Apple middle 00FF00 top 00FF00, accent 0, ink 0, named 0, hidden 1"
grep -qE "plugin peeker: cell banana middle [0-9A-F]{6} top [0-9A-F]{6}, accent 1, ink 1, named 1, hidden 1$" "$log" \
	|| fail "banana lost its name on its letter tile: $(grep "plugin peeker: cell banana" "$log")"
grep -qE "plugin peeker: cell Cherry middle [0-9A-F]{6} top [0-9A-F]{6}, accent 1, ink 1, named 1, hidden 0$" "$log" \
	|| fail "Cherry lost its name: $(grep "plugin peeker: cell Cherry" "$log")"
grep -qF "plugins: layout-grid's" "$log" && fail "layout-grid's layout was turned off"
rm -rf "$WORK/plugins/layout-grid.plugin" "$WORK/plugins/art-boxart.plugin" "$WORK/plugins/peeker.plugin" "$PEEK_DIR" "$STATE/layout-grid.cfg" \
	"$STATE/art-boxart.cfg" "$STATE/peeker.cfg" "$STATE/plugins.txt"
