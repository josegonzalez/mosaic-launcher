# shellcheck shell=sh
# shard: 1

# the bundled screensaver: with idle=1 and a folder of two images and a
# broken one, it opens over the list a second after start, slides from
# the first image to the second and skips the broken one, screener's A
# closes it without opening the selected folder, it opens again and B
# closes it. It opens and closes the same over screener's home screen.
# Then with charging-screen on and its test battery charging,
# it waits while charging-screen's screen opens and closes, and opens once
# unplugger unplugs the battery
SS_DIR="$CARD/Screensaver Case"
mkdir -p "$SS_DIR"
cp "$ROOT/tests/plugins/peeker/art.png" "$SS_DIR/a.png"
cp "$ROOT/tests/plugins/peeker/art.png" "$SS_DIR/b.png"
printf 'not an image\n' >"$SS_DIR/broken.png"
printf 'not an image\n' >"$SS_DIR/notes.txt"
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" screensaver >/dev/null || fail "building screensaver"
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition\n'
printf 'idle=1\nsource=folder\nfolder=Screensaver Case\ninterval=1\neffect=slide\nclock=24h\n' >"$STATE/screensaver.cfg"
printf 'steps=wait,wait,wait,wait,wait,wait,wait,wait,wait,a,wait,wait,wait,wait,wait,wait,b\n' >"$STATE/screener.cfg"
plugin=screensaver
log="$WORK/screensaver.log"
ss_wait=$WAIT
WAIT=$((WAIT + 10))
run_launcher "$log" "plugin screener: done" || fail "the launcher exited early with screensaver"
WAIT=$ss_wait
expect "opens after 1s idle on the list or home: folder, every 1s, slide, clock 24h"
expect "folder $SS_DIR"
expect "opened after 1s idle on the list: 3 images from folder"
expect "showing 1 of 3: a.png"
expect "sliding to 2 of 3"
expect "showing 2 of 3: b.png"
expect "couldn't load broken.png"
expect "closed by a button"
grep -qF "plugin screensaver: showing 3 of 3" "$log" && fail "screensaver showed the broken image"
[ "$(grep -c "plugin screensaver: closed by a button" "$log")" -ge 2 ] || fail "screensaver didn't close twice"
seen | grep -Eq '^gamelist screensaver\.main gamelist screensaver\.main gamelist ' \
	|| fail "screensaver didn't open, close, open and close: $(seen)"
# A opening the root's selected folder would be a push after a push
sed -n 's/.*plugin screener: transition \([a-z]*\) .*/\1/p' "$log" | tr '\n' ' ' | grep -Eq '^(push pop )+(push )?$' \
	|| fail "screensaver's closing press acted on the list: $(grep "plugin screener: transition" "$log")"
grep -qF "plugins: screensaver's screen screensaver.main's" "$log" && fail "screensaver's screen was turned off"

# the same over screener's home screen, whose A would open a dialog
[ -f "$HOME_LEFT" ] && cp "$HOME_LEFT" "$WORK/screensaver-home-left.saved"
rm -f "$HOME_LEFT"
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition,home\n'
printf 'home=main\nsteps=wait,wait,wait,wait,wait,wait,a,wait,wait,wait\n' >"$STATE/screener.cfg"
log="$WORK/screensaver-home.log"
WAIT=$((WAIT + 10))
run_launcher "$log" "plugin screener: done" || fail "the launcher exited early with screensaver over home"
WAIT=$ss_wait
expect "opened after 1s idle on home: 3 images from folder"
expect "closed by a button"
grep -qF "plugin screener: home screener.main" "$log" || fail "screener.main wasn't the home screen"
seen | grep -Eq '^screener\.main screensaver\.main screener\.main ' \
	|| fail "screensaver didn't open and close over home: $(seen)"
grep -qF "plugin screener: input screener.main" "$log" && fail "screensaver's closing press reached the home screen"
if [ -f "$WORK/screensaver-home-left.saved" ]; then mv "$WORK/screensaver-home-left.saved" "$HOME_LEFT"; else rm -f "$HOME_LEFT"; fi
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition\n'

# charging-screen comes first while charging
CS_FAKE="$WORK/charging-screen.fake"
printf '73 1\n' >"$CS_FAKE"
printf 'charging-screen=on\n' >"$STATE/plugins.txt"
printf 'idle=2\n' >"$STATE/charging-screen.cfg"
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" charging-screen >/dev/null || fail "building charging-screen"
build_plugin unplugger 'id=unplugger\nname=Unplugger\napi=1\n'
printf 'steps=wait,wait,wait,wait,wait,wait,wait,a,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait,wait\n' \
	>"$STATE/screener.cfg"
export MOSAIC_CHARGING_SCREEN_FAKE="$CS_FAKE"
log="$WORK/screensaver-charging.log"
WAIT=$((WAIT + 10))
run_launcher "$log" "plugin screener: done" || fail "the launcher exited early with screensaver and charging-screen"
WAIT=$ss_wait
unset MOSAIC_CHARGING_SCREEN_FAKE
expect "opens after 1s idle on the list or home: folder, every 1s, slide, clock 24h, after charging-screen while charging"
expect "waiting: charging-screen's screen comes first while charging"
expect "opened after 1s idle on the list: 3 images from folder"
grep -qF "plugin unplugger: unplugged" "$log" || fail "unplugger didn't unplug"
seen | grep -Eq '^gamelist charging-screen\.main gamelist charging-screen\.main gamelist screensaver\.main ' \
	|| fail "screensaver didn't wait for charging-screen: $(seen)"
rm -rf "$SS_DIR" "$WORK/plugins/screensaver.plugin" "$WORK/plugins/charging-screen.plugin" \
	"$WORK/plugins/screener.plugin" "$WORK/plugins/unplugger.plugin" "$STATE/screener.cfg" "$STATE/screensaver.cfg" \
	"$STATE/charging-screen.cfg" "$STATE/plugins.txt" "$CS_FAKE"
case_log=""
