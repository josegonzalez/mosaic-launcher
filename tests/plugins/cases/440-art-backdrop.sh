# shellcheck shell=sh
# shard: 1

# the bundled art-backdrop draws the selected game's art behind the list,
# which dimmer reads back from each push's old frame down the left edge and
# at the top right. By default it's a gradient over the whole screen,
# dimmed by half: Art's backdrop, red on the left and blue on the right,
# at full strength over the top half and fading out below it; Shot falls
# back to its screenshot, green; and Bare, with no art, keeps stock's
# background, the theme's colour.
DIM_DIR="$WORK/dimmer"
mkdir -p "$DIM_DIR"
cp "$ROOT/tests/plugins/drawer/art.png" "$DIM_DIR/backdrop.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$DIM_DIR/shot.png"
make -s -C "$ROOT/plugins" art-backdrop ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building art-backdrop"
build_plugin dimmer 'id=dimmer\nname=Dimmer\napi=1\nslots=transition\n'
printf 'backdrop=%s\nshot=%s\n' "$DIM_DIR/backdrop.png" "$DIM_DIR/shot.png" >"$STATE/dimmer.cfg"
printf 'dimmer=on\nart-backdrop=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/backdrop.log" "plugin dimmer: done" || fail "the launcher exited early with art-backdrop loaded"
log="$WORK/backdrop.log"
plugin=dimmer
grep -qF "plugins: art-backdrop draws the background" "$log" || fail "art-backdrop didn't take the background slot"
grep -qF "plugin art-backdrop: gradient, dimmed by 128 of 255, over 1/1 of the screen, from 4 kinds of art" "$log" \
	|| fail "art-backdrop's defaults are wrong: $(grep "plugin art-backdrop:" "$log")"
grep -qE "plugin dimmer: from Art top [78][0-9A-F]0000FF quarter [78][0-9A-F]0000FF fade [456][0-9A-F]0000FF low [123][0-9A-F]0000FF right 0000[78][0-9A-F]FF, " "$log" \
	|| fail "Art's backdrop isn't a dimmed gradient: $(grep "plugin dimmer: from Art" "$log")"
grep -qE "plugin dimmer: from Shot top 00[78][0-9A-F]00FF quarter 00[78][0-9A-F]00FF fade 00[456][0-9A-F]00FF low 00[123][0-9A-F]00FF " "$log" \
	|| fail "Shot's screenshot isn't behind the list: $(grep "plugin dimmer: from Shot" "$log")"
# stock_is <game> <log>: every pixel dimmer read for game is the theme's colour
stock_is() {
	line=$(grep "plugin dimmer: from $1 " "$2")
	theme=$(echo "$line" | sed -n 's/.*, theme \([0-9A-F]*\)$/\1/p')
	[ -n "$theme" ] || { fail "dimmer didn't read $1's background"; return; }
	want="from $1 top ${theme}FF quarter ${theme}FF fade ${theme}FF low ${theme}FF right ${theme}FF, theme $theme"
	echo "$line" | grep -qF "$want" || fail "$1 doesn't have stock's background: $line"
}
stock_is Bare "$log"
grep -qF "stock's background until" "$log" && fail "art-backdrop's background was turned off"

# full-bleed over the top third, dimmed by three quarters, without the
# screenshot: Art's backdrop is darker and stops a third of the way down,
# and Shot keeps stock's background
printf 'style=full\nheight=third\ndim=75\nscreenshot=off\n' >"$STATE/art-backdrop.cfg"
run_launcher "$WORK/backdrop-third.log" "plugin dimmer: done" || fail "the launcher exited early with art-backdrop's third"
log="$WORK/backdrop-third.log"
grep -qF "plugin art-backdrop: full-bleed, dimmed by 191 of 255, over 1/3 of the screen, from 3 kinds of art" "$log" \
	|| fail "art-backdrop's settings weren't read: $(grep "plugin art-backdrop:" "$log")"
theme=$(sed -n 's/.*plugin dimmer: from Art .*, theme \([0-9A-F]*\)$/\1/p' "$log")
grep -qE "plugin dimmer: from Art top [34][0-9A-F]0000FF quarter [34][0-9A-F]0000FF fade ${theme}FF low ${theme}FF right 0000[34][0-9A-F]FF, " "$log" \
	|| fail "Art's backdrop isn't full-bleed over a third: $(grep "plugin dimmer: from Art" "$log")"
stock_is Shot "$log"
stock_is Bare "$log"
rm -rf "$WORK/plugins/art-backdrop.plugin" "$WORK/plugins/dimmer.plugin" "$DIM_DIR" "$STATE/art-backdrop.cfg" "$STATE/dimmer.cfg" "$STATE/plugins.txt"
