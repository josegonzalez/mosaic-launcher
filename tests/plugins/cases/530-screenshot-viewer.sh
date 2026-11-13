# shellcheck shell=sh
# shard: 3

# the bundled screenshot-viewer: screener runs its action, steps through
# the grid, opens the full view, steps on and deletes the oldest after the
# confirm, then backs out to the grid and the list. A link to a file
# outside the folder and a macOS ._ file aren't listed, and the link's
# file stays. Then its capture action saves the screen, from the list and
# from its own screen
SV_SHOTS="$CARD/Screenshots"
SV_OUTSIDE="$CARD/Screenshot Outside"
[ -e "$SV_SHOTS" ] && mv "$SV_SHOTS" "$WORK/screenshots.saved"
mkdir -p "$SV_SHOTS" "$SV_OUTSIDE"
cp "$ROOT/tests/plugins/peeker/art.png" "$SV_SHOTS/Newest Game.2026-11-09-08-30-00.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$SV_SHOTS/Dr. Middle.2026-11-08-14-03-05.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$SV_SHOTS/Oldest.png"
touch -t 202001010000 "$SV_SHOTS/Oldest.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$SV_SHOTS/._Newest Game.2026-11-09-08-30-00.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$SV_OUTSIDE/Secret.png"
ln -s "$SV_OUTSIDE/Secret.png" "$SV_SHOTS/Linked.2026-12-01-00-00-00.png"
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" screenshot-viewer >/dev/null || fail "building screenshot-viewer"
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition\n'
printf 'steps=action:screenshot-viewer.open,right,a,right,y,a,b,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/screenshot-viewer.log" "plugin screener: done" || fail "the launcher exited early with screenshot-viewer"
log="$WORK/screenshot-viewer.log"
plugin=screenshot-viewer
expect "3 screenshots in $SV_SHOTS"
expect "grid 0 of 3: Newest Game, 2026-11-09 08:30, "
expect "grid 1 of 3: Dr. Middle, 2026-11-08 14:03, "
expect "view 1 of 3: Dr. Middle, 2026-11-08 14:03, image drawn"
expect "view 2 of 3: Oldest, no date, image drawn"
expect "deleted Oldest.png, 2 left"
expect "view 1 of 2: Dr. Middle, 2026-11-08 14:03, image drawn"
expect "grid 1 of 2: Dr. Middle, 2026-11-08 14:03, "
[ "$(seen)" = "gamelist screenshot-viewer.main dialog screenshot-viewer.main gamelist " ] \
	|| fail "screenshot-viewer didn't open, confirm and close: $(seen)"
[ -e "$SV_SHOTS/Oldest.png" ] && fail "screenshot-viewer didn't delete the screenshot"
[ -f "$SV_SHOTS/Dr. Middle.2026-11-08-14-03-05.png" ] || fail "screenshot-viewer deleted another screenshot"
if [ ! -L "$SV_SHOTS/Linked.2026-12-01-00-00-00.png" ] || [ ! -f "$SV_OUTSIDE/Secret.png" ]; then
	fail "screenshot-viewer touched the link"
fi
grep -qF "plugins: screenshot-viewer's screen screenshot-viewer.main's" "$log" && fail "screenshot-viewer's screen was turned off"

# its capture action saves the game list, which then shows in the grid,
# and a capture from the grid shows there too, on the same selection
rm -f "$SV_SHOTS"/*.png
printf 'steps=action:screenshot-viewer.capture,action:screenshot-viewer.open,wait,wait,wait,wait,wait,action:screenshot-viewer.capture,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/screenshot-viewer-capture.log" "plugin screener: done" || fail "the launcher exited early capturing with screenshot-viewer"
log="$WORK/screenshot-viewer-capture.log"
plugin=screenshot-viewer
[ "$(grep -c "plugin screenshot-viewer: saved $SV_SHOTS/Launcher\." "$log")" -eq 2 ] || fail "screenshot-viewer didn't save twice"
expect "1 screenshots in $SV_SHOTS"
expect "grid 0 of 1: Launcher, "
expect "grid 1 of 2: Launcher, "
[ "$(find "$SV_SHOTS" -name 'Launcher.*.png' | wc -l | tr -d ' ')" -eq 2 ] || fail "screenshot-viewer's captures aren't in $SV_SHOTS"
rm -rf "$SV_SHOTS" "$SV_OUTSIDE"
[ -e "$WORK/screenshots.saved" ] && mv "$WORK/screenshots.saved" "$SV_SHOTS"
rm -rf "$WORK/plugins/screenshot-viewer.plugin" "$WORK/plugins/screener.plugin" "$STATE/screener.cfg"
case_log=""
