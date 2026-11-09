# shellcheck shell=sh
# shard: 2

# the bundled layout-hero-rail draws peeker's folder as a rail of cards
# under the selected game's hero: Apple's thumbnail from .media fills its
# card, scaled from 8x8, on a tile of the theme's background colour (so
# its middle differs from its top), and
# banana and Cherry, with no art, get letter tiles in the theme's accent
# colour with their letter across the middle; peeker reads each card back
# as a row decorator. Its settings set the cards' size.
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" layout-hero-rail art-boxart >/dev/null || fail "building layout-hero-rail"
build_plugin peeker 'id=peeker\nname=Peeker\napi=1\n'
PEEK_DIR="$CARD/Peeker"
mkdir -p "$PEEK_DIR/.media" "$STATE"
cp "$ROOT/tests/plugins/peeker/art.png" "$PEEK_DIR/.media/Apple.png"
printf 'layout-hero-rail=on\npeeker=on\nslot.layout=layout-hero-rail\n' >"$STATE/plugins.txt"
run_launcher "$WORK/hero-rail.log" "plugin peeker: cell Apple middle 00FF00" "plugin peeker: cell banana" \
	"plugin peeker: cell Cherry" || fail "the launcher exited early with layout-hero-rail loaded"
log="$WORK/hero-rail.log"
plugin=peeker
grep -qF "plugins: layout-hero-rail draws the game list" "$log" || fail "layout-hero-rail didn't take the game list"
grep -qF "plugin layout-hero-rail: cards 28% of the height, tinted by 51 of 255, logo fading in over 300 ms" "$log" \
	|| fail "layout-hero-rail's defaults are wrong: $(grep "plugin layout-hero-rail:" "$log")"
grep -qE "plugin layout-hero-rail: hero 1024x[0-9]+, 6 cards of 138 by 185, logo in " "$log" \
	|| fail "layout-hero-rail's rail is wrong: $(grep "plugin layout-hero-rail:" "$log")"
expect "open folder 1"
grep -qE "plugin peeker: cell Apple middle 00FF00 top [0-9A-F]{6}, accent 0, ink 1, named 1$" "$log" \
	|| fail "Apple's card isn't its art: $(grep "plugin peeker: cell Apple" "$log")"
grep -qE "plugin peeker: cell banana middle [0-9A-F]{6} top [0-9A-F]{6}, accent 1, ink 1, named 1$" "$log" \
	|| fail "banana has no letter tile: $(grep "plugin peeker: cell banana" "$log")"
grep -qE "plugin peeker: cell Cherry middle [0-9A-F]{6} top [0-9A-F]{6}, accent 1, ink 1, named 1$" "$log" \
	|| fail "Cherry has no letter tile: $(grep "plugin peeker: cell Cherry" "$log")"
grep -qF "plugins: layout-hero-rail's" "$log" && fail "layout-hero-rail's layout was turned off"
grep -qF "plugins: peeker's" "$log" && fail "peeker's decorator was turned off"

printf 'cards=small\ntint=0\nfade=0\n' >"$STATE/layout-hero-rail.cfg"
run_launcher "$WORK/hero-rail-set.log" "plugin peeker: cell Apple middle 00FF00" "plugin peeker: cell Cherry" \
	|| fail "the launcher exited early with layout-hero-rail's small cards"
log="$WORK/hero-rail-set.log"
grep -qF "plugin layout-hero-rail: cards 22% of the height, tinted by 0 of 255, logo fading in over 0 ms" "$log" \
	|| fail "layout-hero-rail's settings weren't read: $(grep "plugin layout-hero-rail:" "$log")"
grep -qE "plugin layout-hero-rail: hero 1024x[0-9]+, 8 cards of 108 by 145, " "$log" \
	|| fail "layout-hero-rail's small cards are wrong: $(grep "plugin layout-hero-rail:" "$log")"
rm -rf "$WORK/plugins/layout-hero-rail.plugin" "$WORK/plugins/art-boxart.plugin" "$WORK/plugins/peeker.plugin" "$PEEK_DIR" \
	"$STATE/layout-hero-rail.cfg" "$STATE/plugins.txt"

# hider's hidden names under layout-hero-rail: Apple's and Cherry's
# cards, once their art is in, have no name under them, and Banana's,
# which isn't hidden, keeps its
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" layout-hero-rail >/dev/null || fail "building layout-hero-rail"
build_plugin hider 'id=hider\nname=Hider\napi=1\n'
HIDE_DIR="$CARD/Hider"
mkdir -p "$HIDE_DIR"
cp "$ROOT/tests/plugins/peeker/art.png" "$HIDE_DIR/art.png"
printf 'layout-hero-rail=on\nhider=on\nslot.layout=layout-hero-rail\n' >"$STATE/plugins.txt"
run_launcher "$WORK/hero-rail-hidden.log" "plugin hider: row Apple selected 1 hidden 1 named 0" \
	"plugin hider: row Banana selected 0 hidden 0 named 1" "plugin hider: row Cherry selected 0 hidden 1 named 0" \
	|| fail "the launcher exited early with hider and layout-hero-rail"
log="$WORK/hero-rail-hidden.log"
grep -qF "plugins: layout-hero-rail draws the game list" "$log" || fail "layout-hero-rail didn't take the game list"
grep -qF "plugin hider: row Banana selected 0 hidden 1" "$log" && fail "Banana's name was hidden"
grep -qE "plugin hider: row Banana .* named 0" "$log" && fail "Banana's card has no name: $(grep "plugin hider: row Banana" "$log")"
rm -rf "$WORK/plugins/layout-hero-rail.plugin" "$WORK/plugins/hider.plugin" "$HIDE_DIR" "$STATE/plugins.txt"

# the hero, which dimmer reads back from each push's old frame: Art's
# backdrop, red on the left and blue on the right, tinted toward the
# theme's accent across the top and fading into the theme's background
# colour above the cards, with its logo from art-boxart's wheel folder,
# green, faded in at the bottom left once the selection settled; Shot's
# screenshot, green; and Bare, with no art, keeps stock's background
DIM_DIR="$WORK/dimmer"
mkdir -p "$DIM_DIR" "$CARD/Dimmer/.media/wheel"
cp "$ROOT/tests/plugins/drawer/art.png" "$DIM_DIR/backdrop.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$DIM_DIR/shot.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$CARD/Dimmer/.media/wheel/Art.png"
make -s -C "$ROOT/plugins" layout-hero-rail art-boxart ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building layout-hero-rail"
build_plugin dimmer 'id=dimmer\nname=Dimmer\napi=1\nslots=transition\n'
printf 'backdrop=%s\nshot=%s\nprobe=173,289\n' "$DIM_DIR/backdrop.png" "$DIM_DIR/shot.png" >"$STATE/dimmer.cfg"
printf 'dimmer=on\nart-boxart=on\nlayout-hero-rail=on\nslot.layout=layout-hero-rail\n' >"$STATE/plugins.txt"
run_launcher "$WORK/hero-rail-hero.log" "plugin dimmer: done" || fail "the launcher exited early with layout-hero-rail's hero"
log="$WORK/hero-rail-hero.log"
plugin=dimmer
grep -qF "plugins: layout-hero-rail draws the game list" "$log" || fail "layout-hero-rail didn't take the game list"
theme=$(sed -n 's/.*plugin dimmer: from Art .*, theme \([0-9A-F]*\)$/\1/p' "$log")
[ -n "$theme" ] || fail "dimmer didn't read Art's hero"
grep -qE "plugin dimmer: from Art top [D-F][0-9A-F]0[0-9A-F]{3}FF quarter [D-F][0-9A-F]0[0-9A-F]{3}FF fade [0-3][0-9A-F]0[0-9A-F]{3}FF low ${theme}FF right [0-3][0-9A-F]0[0-9A-F][C-F][0-9A-F]FF, " "$log" \
	|| fail "Art's hero isn't its tinted backdrop fading out: $(grep "plugin dimmer: from Art" "$log")"
expect "probe Art 00FF00FF"
grep -qE "plugin dimmer: from Shot top [0-3][0-9A-F][C-F][0-9A-F][0-3][0-9A-F]FF quarter [0-3][0-9A-F][C-F][0-9A-F][0-3][0-9A-F]FF fade [0-3][0-9A-F]{5}FF low ${theme}FF " "$log" \
	|| fail "Shot's hero isn't its screenshot: $(grep "plugin dimmer: from Shot" "$log")"
grep -qF "plugin dimmer: probe Shot 00FF00FF" "$log" && fail "Shot has a logo"
expect "from Bare top ${theme}FF quarter ${theme}FF fade ${theme}FF low ${theme}FF right ${theme}FF, theme $theme"
grep -qF "plugins: layout-hero-rail's" "$log" && fail "layout-hero-rail's layout was turned off"
rm -rf "$WORK/plugins/layout-hero-rail.plugin" "$WORK/plugins/art-boxart.plugin" "$WORK/plugins/dimmer.plugin" "$DIM_DIR" \
	"$CARD/Dimmer" "$STATE/dimmer.cfg" "$STATE/plugins.txt"
