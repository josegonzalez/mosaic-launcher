# shellcheck shell=sh
# shard: 1

# the bundled art-boxart answers art from each fork's folder convention, as
# looker sees it through art_find: nx-redux's .media/boxart2d and
# .media/boxart (a .png, then a .jpg or .jpeg), MyMinUI's Imgs, UnuOS's
# .res, visible Images folders, a folder's cover.png and a Pico-8 cart as
# its own art, in that order, with nx-redux's .media/boxart as the 3D box,
# .media/wheel as the logo and .media/screenshot as the screenshot, and
# stock's .media thumbnail kept unless the thumbnail setting is on. The
# Imgs and Images folders are left out of the list, and no names are
# hidden until cover_name hides the covered folder's. Then the settings
# turn each off.
BOX_DIR="$CARD/Boxart"
mkdir -p "$BOX_DIR/.media/boxart2d" "$BOX_DIR/.media/boxart" "$BOX_DIR/.media/wheel" "$BOX_DIR/.media/screenshot" \
	"$BOX_DIR/.media/mix" "$BOX_DIR/Imgs" "$BOX_DIR/.res" "$BOX_DIR/Images" "$BOX_DIR/Cover" "$STATE"
for game in Alpha Bravo Charlie Delta Foxtrot Golf; do : >"$BOX_DIR/$game.gb"; done
: >"$BOX_DIR/Cover/Game.gb"
for art in .media/Alpha .media/boxart2d/Alpha .media/boxart/Alpha .media/wheel/Alpha .media/screenshot/Alpha \
	Imgs/Alpha Imgs/Bravo .res/Charlie Images/Delta Cover/cover Echo.p8 .media/mix/Foxtrot; do
	cp "$ROOT/tests/plugins/drawer/art.png" "$BOX_DIR/$art.png"
done
# art_find only names files, so a .png copy stands in for the .jpg and .jpeg art
for art in .media/boxart/Golf.jpg .media/wheel/Golf.jpeg .media/screenshot/Golf.jpg; do
	cp "$ROOT/tests/plugins/drawer/art.png" "$BOX_DIR/$art"
done
make -s -C "$ROOT/plugins" art-boxart ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building art-boxart"
build_plugin looker 'id=looker\nname=Looker\napi=1\n'
printf 'art-boxart=on\nlooker=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/boxart.log" "plugin looker: done" || fail "the launcher exited early with art-boxart loaded"
log="$WORK/boxart.log"
plugin=looker
grep -qF "plugins: loaded art-boxart" "$log" || fail "art-boxart didn't load"
expect "open folder 1"
expect "entries Alpha.gb,Bravo.gb,Charlie.gb,Cover,Delta.gb,Echo.p8.png,Foxtrot.gb,Golf.gb"
expect 'art Alpha.gb thumbnail 1 ".media/Alpha.png"'
expect 'art Alpha.gb box 1 ".media/boxart2d/Alpha.png"'
expect 'art Alpha.gb box3d 1 ".media/boxart/Alpha.png"'
expect 'art Alpha.gb logo 1 ".media/wheel/Alpha.png"'
expect 'art Alpha.gb screenshot 1 ".media/screenshot/Alpha.png"'
expect 'art Bravo.gb box 1 "Imgs/Bravo.png"'
expect 'art Bravo.gb thumbnail 0 ".media/Bravo.png"'
expect 'art Bravo.gb box3d 0 ""'
expect 'art Charlie.gb box 1 ".res/Charlie.png"'
expect 'art Delta.gb box 1 "Images/Delta.png"'
expect 'art Cover box 1 "Cover/cover.png"'
expect 'art Echo.p8.png box 1 "Echo.p8.png"'
# nx-redux's mix composite isn't any one kind
expect 'art Foxtrot.gb box 0 ""'
expect 'art Foxtrot.gb logo 0 ""'
expect 'art Foxtrot.gb screenshot 0 ""'
expect 'art Golf.gb box 1 ".media/boxart/Golf.jpg"'
expect 'art Golf.gb box3d 1 ".media/boxart/Golf.jpg"'
expect 'art Golf.gb logo 1 ".media/wheel/Golf.jpeg"'
expect 'art Golf.gb screenshot 1 ".media/screenshot/Golf.jpg"'
# folders keep their names by default
expect "hidden none"

# with cover_name on, the folder with a cover shows as its cover, its name
# hidden, and nothing else's is
printf 'cover_name=on\n' >"$STATE/art-boxart.cfg"
run_launcher "$WORK/boxart-cover-name.log" "plugin looker: done" || fail "the launcher exited early with art-boxart hiding covered folders' names"
log="$WORK/boxart-cover-name.log"
expect "hidden Cover"

# the thumbnail setting puts this art before stock's, and with nx-redux's
# folders, covers and carts off the next convention answers; the art
# folders show with hide off
printf 'thumbnail=on\nmedia=off\ncover=off\nrom_art=off\nhide=off\n' >"$STATE/art-boxart.cfg"
run_launcher "$WORK/boxart-settings.log" "plugin looker: done" || fail "the launcher exited early with art-boxart's settings changed"
log="$WORK/boxart-settings.log"
expect "entries Alpha.gb,Bravo.gb,Charlie.gb,Cover,Delta.gb,Echo.p8.png,Foxtrot.gb,Golf.gb,Images,Imgs"
expect 'art Alpha.gb thumbnail 1 "Imgs/Alpha.png"'
expect 'art Alpha.gb box 1 "Imgs/Alpha.png"'
expect 'art Alpha.gb box3d 0 ""'
expect 'art Alpha.gb logo 0 ""'
expect 'art Alpha.gb screenshot 0 ""'
expect 'art Golf.gb box 0 ""'
expect 'art Bravo.gb thumbnail 1 "Imgs/Bravo.png"'
expect 'art Cover box 0 ""'
expect 'art Echo.p8.png box 0 ""'

# with Imgs, .res and Images off, their art is gone and the folders show
# even with hide on
printf 'imgs=off\nres=off\nimages=off\n' >"$STATE/art-boxart.cfg"
run_launcher "$WORK/boxart-off.log" "plugin looker: done" || fail "the launcher exited early with art-boxart's folders off"
log="$WORK/boxart-off.log"
expect "entries Alpha.gb,Bravo.gb,Charlie.gb,Cover,Delta.gb,Echo.p8.png,Foxtrot.gb,Golf.gb,Images,Imgs"
expect 'art Alpha.gb box 1 ".media/boxart2d/Alpha.png"'
expect 'art Bravo.gb box 0 ""'
expect 'art Charlie.gb box 0 ""'
expect 'art Delta.gb box 0 ""'
expect 'art Cover box 1 "Cover/cover.png"'
expect 'art Echo.p8.png box 1 "Echo.p8.png"'
rm -rf "$WORK/plugins/art-boxart.plugin" "$WORK/plugins/looker.plugin" "$BOX_DIR" "$STATE/art-boxart.cfg" "$STATE/plugins.txt"
