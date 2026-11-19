# shellcheck shell=sh

# layout-hero-rail's Cocoon look, which dimmer reads back from each push's
# old frame. With the defaults, blur and animation on: Art's backdrop, red
# on the left and blue on the right, stays sharp across the top, and the
# band behind the cards is a blurred, darkened copy of it, still red on the
# left but darker; the selected card, Art's, has a ring in the theme's
# accent colour round it (dimmer's probe, on the ring's left side); and Bare,
# with no art, keeps stock's background. With both off, the hero fades
# into the theme's background colour above the cards as before, and the
# ring stays.
CO_DIR="$WORK/cocoon"
mkdir -p "$CO_DIR" "$CARD/Dimmer/.media/wheel" "$STATE"
cp "$ROOT/tests/plugins/drawer/art.png" "$CO_DIR/backdrop.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$CO_DIR/shot.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$CARD/Dimmer/.media/wheel/Art.png"
make -s -C "$ROOT/plugins" layout-hero-rail art-boxart ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building layout-hero-rail"
build_plugin dimmer 'id=dimmer\nname=Dimmer\napi=1\nslots=transition\n'
# the ring's left side: 3 wide, a frame of 6 out from the first card at 68
printf 'backdrop=%s\nshot=%s\nprobe=63,500\n' "$CO_DIR/backdrop.png" "$CO_DIR/shot.png" >"$STATE/dimmer.cfg"
printf 'dimmer=on\nart-boxart=on\nlayout-hero-rail=on\nslot.layout=layout-hero-rail\n' >"$STATE/plugins.txt"
rm -f "$STATE/layout-hero-rail.cfg"
log="$WORK/hero-cocoon.log"
run_launcher "$log" "plugin dimmer: done" || fail "the launcher exited early with layout-hero-rail's blur"
plugin=dimmer
grep -qF "plugins: layout-hero-rail draws the game list" "$log" || fail "layout-hero-rail didn't take the game list"
grep -qE "plugin layout-hero-rail: cards .*, blur on, animation on$" "$log" \
	|| fail "layout-hero-rail's blur and animation aren't on by default: $(grep "plugin layout-hero-rail: cards" "$log")"
theme=$(sed -n 's/.*plugin dimmer: from Art .*, theme \([0-9A-F]*\)$/\1/p' "$log")
[ -n "$theme" ] || fail "dimmer didn't read Art's hero"
grep -qE "plugin dimmer: from Art top [D-F][0-9A-F]0[0-9A-F]{3}FF quarter [D-F][0-9A-F]0[0-9A-F]{3}FF fade [5-B][0-9A-F][0-2][0-9A-F]{3}FF low ${theme}FF right [0-3][0-9A-F]0[0-9A-F][C-F][0-9A-F]FF, " "$log" \
	|| fail "Art's band isn't its blurred, darkened backdrop: $(grep "plugin dimmer: from Art" "$log")"
grep -qE "plugin dimmer: probe Art [89A][0-9A-F][12][0-9A-F]5[0-9A-F]FF$" "$log" \
	|| fail "Art's card has no ring in the accent: $(grep "plugin dimmer: probe Art" "$log")"
expect "from Bare top ${theme}FF quarter ${theme}FF fade ${theme}FF low ${theme}FF right ${theme}FF, theme $theme"
grep -qF "plugins: layout-hero-rail's" "$log" && fail "layout-hero-rail's layout was turned off"

printf 'blur=off\nanimation=off\n' >"$STATE/layout-hero-rail.cfg"
log="$WORK/hero-cocoon-off.log"
run_launcher "$log" "plugin dimmer: done" || fail "the launcher exited early with layout-hero-rail's blur off"
grep -qE "plugin layout-hero-rail: cards .*, blur off, animation off$" "$log" \
	|| fail "layout-hero-rail's blur and animation settings weren't read: $(grep "plugin layout-hero-rail: cards" "$log")"
theme=$(sed -n 's/.*plugin dimmer: from Art .*, theme \([0-9A-F]*\)$/\1/p' "$log")
grep -qE "plugin dimmer: from Art top [D-F][0-9A-F]0[0-9A-F]{3}FF quarter [D-F][0-9A-F]0[0-9A-F]{3}FF fade [0-3][0-9A-F]0[0-9A-F]{3}FF low ${theme}FF " "$log" \
	|| fail "Art's hero doesn't fade out with the blur off: $(grep "plugin dimmer: from Art" "$log")"
grep -qE "plugin dimmer: probe Art [89A][0-9A-F][12][0-9A-F]5[0-9A-F]FF$" "$log" \
	|| fail "Art's card lost its ring with the animation off: $(grep "plugin dimmer: probe Art" "$log")"
rm -rf "$WORK/plugins/layout-hero-rail.plugin" "$WORK/plugins/art-boxart.plugin" "$WORK/plugins/dimmer.plugin" "$CO_DIR" \
	"$CARD/Dimmer" "$STATE/dimmer.cfg" "$STATE/plugins.txt" "$STATE/layout-hero-rail.cfg"
