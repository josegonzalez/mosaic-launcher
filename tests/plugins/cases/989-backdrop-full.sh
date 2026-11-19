# shellcheck shell=sh

# art-backdrop with fill at full covers the whole screen and darkens it
# toward the list, which dimmer reads back down the left edge and at the
# top right. Undimmed, Art's backdrop is red on the left and blue on the
# right: under the built-in list the left edge is darkened by 160 of 255
# all the way down, past where top's gradient fades out, and the top
# right is nearly at full strength. With layout-fancy's art on the left,
# its rows and the darkening are on the right instead.
DIM_DIR="$WORK/dimmer"
mkdir -p "$DIM_DIR" "$STATE"
cp "$ROOT/tests/plugins/drawer/art.png" "$DIM_DIR/backdrop.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$DIM_DIR/shot.png"
make -s -C "$ROOT/plugins" art-backdrop layout-fancy ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null \
	|| fail "building art-backdrop and layout-fancy"
build_plugin dimmer 'id=dimmer\nname=Dimmer\napi=1\nslots=transition\n'
printf 'backdrop=%s\nshot=%s\n' "$DIM_DIR/backdrop.png" "$DIM_DIR/shot.png" >"$STATE/dimmer.cfg"
printf 'fill=full\ndim=0\nheight=third\n' >"$STATE/art-backdrop.cfg"
printf 'dimmer=on\nart-backdrop=on\nlayout-fancy=off\n' >"$STATE/plugins.txt"
run_launcher "$WORK/backdrop-full.log" "plugin dimmer: done" || fail "the launcher exited early with art-backdrop's full fill"
log="$WORK/backdrop-full.log"
grep -qF "plugin art-backdrop: over the whole screen, dimmed by 0 of 255, from 4 kinds of art" "$log" \
	|| fail "art-backdrop's fill wasn't read: $(grep "plugin art-backdrop:" "$log")"
grep -qF "plugin art-backdrop: darkened toward the left, under the built-in list" "$log" \
	|| fail "art-backdrop didn't find the built-in list: $(grep "plugin art-backdrop:" "$log")"
grep -qE "plugin dimmer: from Art top [56][0-9A-F]0000FF quarter [56][0-9A-F]0000FF fade [56][0-9A-F]0000FF low [56][0-9A-F]0000FF right 0000F[0-9A-F]FF, " "$log" \
	|| fail "Art's backdrop isn't over the whole screen, darker on the left: $(grep "plugin dimmer: from Art" "$log")"
grep -qE "plugin dimmer: from Shot top 00[56][0-9A-F]00FF quarter 00[56][0-9A-F]00FF fade 00[56][0-9A-F]00FF low 00[56][0-9A-F]00FF " "$log" \
	|| fail "Shot's screenshot isn't over the whole screen: $(grep "plugin dimmer: from Shot" "$log")"
grep -qF "stock's background until" "$log" && fail "art-backdrop's background was turned off"

# layout-fancy with its art on the left: the left edge is at full strength
# and the top right darkened
printf 'position=left\n' >"$STATE/layout-fancy.cfg"
printf 'dimmer=on\nlayout-fancy=on\nart-backdrop=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/backdrop-fancy.log" "plugin dimmer: done" || fail "the launcher exited early with art-backdrop under layout-fancy"
log="$WORK/backdrop-fancy.log"
grep -qF "plugin art-backdrop: darkened toward the right, under layout-fancy" "$log" \
	|| fail "art-backdrop didn't read layout-fancy's position: $(grep "plugin art-backdrop:" "$log")"
grep -qE "plugin dimmer: from Art top F[0-9A-F]0000FF quarter F[0-9A-F]0000FF fade F[0-9A-F]0000FF low F[0-9A-F]0000FF right 0000[56][0-9A-F]FF, " "$log" \
	|| fail "Art's backdrop isn't darker on the right under layout-fancy: $(grep "plugin dimmer: from Art" "$log")"
rm -rf "$WORK/plugins/art-backdrop.plugin" "$WORK/plugins/layout-fancy.plugin" "$WORK/plugins/dimmer.plugin" "$DIM_DIR" \
	"$STATE/art-backdrop.cfg" "$STATE/layout-fancy.cfg" "$STATE/dimmer.cfg" "$STATE/plugins.txt"
