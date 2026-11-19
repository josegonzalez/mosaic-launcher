# shellcheck shell=sh

# switcher-plus's boxart style: SELECT shows the first recent's box art,
# found by art-boxart in nx-redux's .media/boxart2d; right moves on to one
# with only a thumbnail, one with only a save state's preview and one with
# none, and wraps on a press, and B goes back to the list. Screener,
# without the switcher slot, presses the buttons
BA_ROMS="$CARD/Roms/Boxart Test (BAT)"
BA_EMU="$CARD/Emus/desktop/BAT.pak"
BA_STATES="$CARD/.userdata/shared/.minui/BAT"
mkdir -p "$BA_ROMS/.media/boxart2d" "$BA_EMU" "$BA_STATES" "$(dirname "$RECENTS")" "$STATE"
for game in Boxed Thumbed Saved Bare; do : >"$BA_ROMS/$game.gb"; done
: >"$BA_EMU/launch.sh"
cp "$ROOT/tests/plugins/peeker/art.png" "$BA_ROMS/.media/boxart2d/Boxed.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$BA_ROMS/.media/Thumbed.png"
printf '1' >"$BA_STATES/Saved.gb.txt"
cp "$ROOT/tests/plugins/peeker/art.png" "$BA_STATES/Saved.gb.1.bmp"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/boxart-recent.saved"
printf '/Roms/Boxart Test (BAT)/Boxed.gb\n/Roms/Boxart Test (BAT)/Thumbed.gb\n/Roms/Boxart Test (BAT)/Saved.gb\n/Roms/Boxart Test (BAT)/Bare.gb\n' >"$RECENTS"
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" switcher-plus art-boxart >/dev/null \
	|| fail "building switcher-plus and art-boxart"
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition\n'
printf 'screener=on\nswitcher-plus=on\nart-boxart=on\n' >"$STATE/plugins.txt"
printf 'style=boxart\n' >"$STATE/switcher-plus.cfg"
printf 'steps=select,right,right,right,right,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-boxart.log" "plugin screener: done" || fail "the launcher exited early with switcher-plus's boxart style"
log="$WORK/switcher-boxart.log"
plugin=switcher-plus
expect "style boxart"
grep -qF "plugins: switcher-plus's screen switcher-plus.switcher is the game switcher" "$log" || fail "switcher-plus didn't take the switcher"
[ "$(seen)" = "gamelist switcher-plus.switcher gamelist " ] || fail "switcher-plus's boxart style didn't open and close as the switcher: $(seen)"
expect "showing 0 of 4: Boxed, art box, open, style boxart"
expect "showing 1 of 4: Thumbed, art thumbnail, open, style boxart"
expect "showing 2 of 4: Saved, art preview, resume, style boxart"
expect "showing 3 of 4: Bare, art none, open, style boxart"
[ "$(grep -cF "plugin switcher-plus: showing 0 of 4: Boxed, art box" "$log")" -eq 2 ] || fail "right past the last recent didn't wrap"
grep -qF "plugins: switcher-plus's screen switcher-plus.switcher's" "$log" && fail "switcher-plus's boxart style was turned off"

# without art-boxart there's no box, and Boxed has no other art
printf 'screener=on\nswitcher-plus=on\nart-boxart=off\n' >"$STATE/plugins.txt"
printf 'steps=select,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-boxart-none.log" "plugin screener: done" || fail "the launcher exited early with the boxart style and no art-boxart"
log="$WORK/switcher-boxart-none.log"
expect "showing 0 of 4: Boxed, art none, open, style boxart"

if [ -f "$WORK/boxart-recent.saved" ]; then cp "$WORK/boxart-recent.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
rm -rf "$BA_ROMS" "$BA_EMU" "$BA_STATES" "$WORK/plugins/switcher-plus.plugin" "$WORK/plugins/art-boxart.plugin" \
	"$WORK/plugins/screener.plugin" "$STATE/switcher-plus.cfg" "$STATE/screener.cfg" "$STATE/plugins.txt" "$WORK/boxart-recent.saved"
