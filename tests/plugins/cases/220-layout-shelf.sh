# shellcheck shell=sh
# shard: 1

# the bundled layout-shelf draws peeker's folder as GB cartridges on
# shelves, as its games are .gb: Apple's thumbnail from .media fills its
# label, and banana and Cherry, with no art, get letter labels; peeker
# reads each cartridge back as a row decorator, from the top of its grey
# body. With art-boxart on, Cherry's box stands in for a thumbnail, and
# its settings set three shelves and no lift animation.
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" layout-shelf art-boxart >/dev/null || fail "building layout-shelf"
build_plugin peeker 'id=peeker\nname=Peeker\napi=1\n'
PEEK_DIR="$CARD/Peeker"
mkdir -p "$PEEK_DIR/.media" "$STATE"
cp "$ROOT/tests/plugins/peeker/art.png" "$PEEK_DIR/.media/Apple.png"
printf 'layout-shelf=on\npeeker=on\nslot.layout=layout-shelf\n' >"$STATE/plugins.txt"
run_launcher "$WORK/layout-shelf.log" "plugin peeker: cell Apple middle 00FF00" "plugin peeker: cell banana" \
	"plugin peeker: cell Cherry" || fail "the launcher exited early with layout-shelf loaded"
log="$WORK/layout-shelf.log"
plugin=peeker
grep -qF "plugins: layout-shelf draws the game list" "$log" || fail "layout-shelf didn't take the game list"
grep -qE "plugin layout-shelf: 2 shelves of [0-9]+ GB cartridges, [0-9]+ by [0-9]+$" "$log" \
	|| fail "layout-shelf's shelves are wrong: $(grep "plugin layout-shelf:" "$log")"
expect "open folder 1"
expect "cell Apple middle 00FF00 top B4B4BC, accent 0, ink 1, named 1"
grep -qE "plugin peeker: cell banana middle [0-9A-F]{6} top B4B4BC, accent 0, ink 1, named 1$" "$log" \
	|| fail "banana has no letter label: $(grep "plugin peeker: cell banana" "$log")"
grep -qF "plugin peeker: cell banana middle 00FF00" "$log" && fail "banana has Apple's art"
grep -qE "plugin peeker: cell Cherry middle [0-9A-F]{6} top B4B4BC, accent 0, ink 1, named 1$" "$log" \
	|| fail "Cherry has no letter label: $(grep "plugin peeker: cell Cherry" "$log")"
grep -qF "plugins: layout-shelf's" "$log" && fail "layout-shelf's layout was turned off"
grep -qF "plugins: peeker's" "$log" && fail "peeker's decorator was turned off"

printf 'shelves=3\nanimate=off\n' >"$STATE/layout-shelf.cfg"
mkdir -p "$PEEK_DIR/.media/boxart2d"
cp "$ROOT/tests/plugins/peeker/art.png" "$PEEK_DIR/.media/boxart2d/Cherry.png"
printf 'art-boxart=on\nlayout-shelf=on\npeeker=on\nslot.layout=layout-shelf\n' >"$STATE/plugins.txt"
run_launcher "$WORK/layout-shelf-set.log" "plugin peeker: cell Apple middle 00FF00" "plugin peeker: cell Cherry middle 00FF00" \
	|| fail "the launcher exited early with layout-shelf set to three shelves"
log="$WORK/layout-shelf-set.log"
grep -qE "plugin layout-shelf: 3 shelves of [0-9]+ GB cartridges" "$log" || fail "layout-shelf's settings weren't used: $(grep "plugin layout-shelf:" "$log")"
expect "cell Cherry middle 00FF00 top B4B4BC, accent 0, ink 1, named 1"
rm -rf "$WORK/plugins/peeker.plugin" "$PEEK_DIR" "$STATE/layout-shelf.cfg"

# under layout-shelf, hider's selected Apple, whose name is hidden, shows
# its art without its name tag, and with no art keeps it
HIDE_DIR="$CARD/Hider"
mkdir -p "$HIDE_DIR"
cp "$ROOT/tests/plugins/drawer/art.png" "$HIDE_DIR/art.png"
build_plugin hider 'id=hider\nname=Hider\napi=1\nslots=transition\n'
printf 'hider=on\nlayout-shelf=on\nslot.layout=layout-shelf\n' >"$STATE/plugins.txt"
run_launcher "$WORK/layout-shelf-hider.log" "plugin hider: done" || fail "the launcher exited early with hider under layout-shelf"
log="$WORK/layout-shelf-hider.log"
plugin=hider
grep -qE "plugin hider: row Apple selected 1 hidden 1 named 0 " "$log" || fail "Apple's hidden name was drawn: $(grep "plugin hider: row Apple" "$log")"
grep -qE "plugin hider: row Banana selected 0 hidden 0 named 1 " "$log" || fail "Banana lost its name: $(grep "plugin hider: row Banana" "$log")"
printf 'art=none\n' >"$STATE/hider.cfg"
run_launcher "$WORK/layout-shelf-hider-noart.log" "plugin hider: done" || fail "the launcher exited early with hider's art off under layout-shelf"
log="$WORK/layout-shelf-hider-noart.log"
grep -qE "plugin hider: row Apple selected 1 hidden 1 named 1 " "$log" || fail "Apple lost its name with no art: $(grep "plugin hider: row Apple" "$log")"
grep -qF "plugins: layout-shelf's" "$log" && fail "layout-shelf's layout was turned off"
rm -rf "$WORK/plugins/layout-shelf.plugin" "$WORK/plugins/art-boxart.plugin" "$WORK/plugins/hider.plugin" "$HIDE_DIR" \
	"$STATE/hider.cfg" "$STATE/plugins.txt"
