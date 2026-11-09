# shellcheck shell=sh
# shard: 2

# the bundled layout-carousel draws peeker's folder as a carousel: Apple,
# selected, in the middle with its thumbnail from .media filling its cover
# and its name under it, and banana and Cherry to its right on letter cards
# in the theme's accent colour; peeker reads each cover back as a row
# decorator. With art-boxart on, a box comes before a thumbnail: Apple's
# red and blue box stands in for its green thumbnail, letterboxed, and
# Cherry gets its green box.
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" layout-carousel art-boxart >/dev/null || fail "building layout-carousel"
build_plugin peeker 'id=peeker\nname=Peeker\napi=1\n'
PEEK_DIR="$CARD/Peeker"
mkdir -p "$PEEK_DIR/.media" "$STATE"
cp "$ROOT/tests/plugins/peeker/art.png" "$PEEK_DIR/.media/Apple.png"
printf 'layout-carousel=on\npeeker=on\nslot.layout=layout-carousel\n' >"$STATE/plugins.txt"
run_launcher "$WORK/layout-carousel.log" "plugin peeker: cell Apple middle 00FF00" "plugin peeker: cell banana" \
	"plugin peeker: cell Cherry" || fail "the launcher exited early with layout-carousel loaded"
log="$WORK/layout-carousel.log"
plugin=peeker
grep -qF "plugins: layout-carousel draws the game list" "$log" || fail "layout-carousel didn't take the game list"
# 3/10 of the 1000 wide room, and two either side
grep -qF "plugin layout-carousel: strip of 5 covers, 300 by 300" "$log" || fail "layout-carousel's strip is wrong: $(grep "plugin layout-carousel:" "$log")"
expect "open folder 1"
expect "cell Apple middle 00FF00 top 00FF00, accent 0, ink 0, named 1"
grep -qE "plugin peeker: cell banana middle [0-9A-F]{6} top [0-9A-F]{6}, accent 1, ink 1, named 0$" "$log" \
	|| fail "banana has no letter card: $(grep "plugin peeker: cell banana" "$log")"
grep -qE "plugin peeker: cell Cherry middle [0-9A-F]{6} top [0-9A-F]{6}, accent 1, ink 1, named 0$" "$log" \
	|| fail "Cherry has no letter card: $(grep "plugin peeker: cell Cherry" "$log")"
grep -qF "plugins: layout-carousel's" "$log" && fail "layout-carousel's layout was turned off"
grep -qF "plugins: peeker's" "$log" && fail "peeker's decorator was turned off"

printf 'slide=off\n' >"$STATE/layout-carousel.cfg"
mkdir -p "$PEEK_DIR/.media/boxart2d"
cp "$ROOT/tests/plugins/drawer/art.png" "$PEEK_DIR/.media/boxart2d/Apple.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$PEEK_DIR/.media/boxart2d/Cherry.png"
printf 'art-boxart=on\nlayout-carousel=on\npeeker=on\nslot.layout=layout-carousel\n' >"$STATE/plugins.txt"
run_launcher "$WORK/layout-carousel-box.log" "plugin peeker: cell Cherry middle 00FF00" \
	|| fail "the launcher exited early with layout-carousel and art-boxart"
log="$WORK/layout-carousel-box.log"
expect "cell Cherry middle 00FF00 top 00FF00, accent 0, ink 0, named 0"
apple=$(grep "plugin peeker: cell Apple" "$log" | tail -n 1)
case "$apple" in
*"cell Apple middle 00FF00"* | *"accent 1"* | "") fail "Apple's box didn't stand in for its thumbnail: $apple" ;;
esac
rm -rf "$WORK/plugins/peeker.plugin" "$PEEK_DIR" "$STATE/layout-carousel.cfg"

# under layout-carousel, hider's selected Apple, whose name is hidden,
# shows its art without its name, and with no art keeps its name
HIDE_DIR="$CARD/Hider"
mkdir -p "$HIDE_DIR"
cp "$ROOT/tests/plugins/drawer/art.png" "$HIDE_DIR/art.png"
build_plugin hider 'id=hider\nname=Hider\napi=1\nslots=transition\n'
printf 'hider=on\nlayout-carousel=on\nslot.layout=layout-carousel\n' >"$STATE/plugins.txt"
run_launcher "$WORK/layout-carousel-hider.log" "plugin hider: done" || fail "the launcher exited early with hider under layout-carousel"
log="$WORK/layout-carousel-hider.log"
plugin=hider
grep -qE "plugin hider: row Apple selected 1 hidden 1 named 0 " "$log" || fail "Apple's hidden name was drawn: $(grep "plugin hider: row Apple" "$log")"
printf 'art=none\n' >"$STATE/hider.cfg"
run_launcher "$WORK/layout-carousel-hider-noart.log" "plugin hider: done" || fail "the launcher exited early with hider's art off under layout-carousel"
log="$WORK/layout-carousel-hider-noart.log"
grep -qE "plugin hider: row Apple selected 1 hidden 1 named 1 " "$log" || fail "Apple lost its name with no art: $(grep "plugin hider: row Apple" "$log")"
grep -qF "plugins: layout-carousel's" "$log" && fail "layout-carousel's layout was turned off"
rm -rf "$WORK/plugins/layout-carousel.plugin" "$WORK/plugins/art-boxart.plugin" "$WORK/plugins/hider.plugin" "$HIDE_DIR" \
	"$STATE/hider.cfg" "$STATE/plugins.txt"
