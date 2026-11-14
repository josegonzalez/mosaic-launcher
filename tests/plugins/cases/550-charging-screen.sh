# shellcheck shell=sh
# shard: 3

# the bundled charging-screen: on desktop, with no battery, it loads and
# does nothing. With the test battery charging and idle=1, the launcher
# opens it as an ambient screen over
# the list a second after start, screener's A closes it without opening
# the selected folder, it opens again, and unplugger's unplug closes it.
# Then the same over screener's home screen, whose A would open a dialog
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" charging-screen >/dev/null || fail "building charging-screen"
plugin=charging-screen
log="$WORK/charging-screen-desktop.log"
run_launcher "$log" "plugin charging-screen: unsupported on desktop: there's no battery, so the screen never opens" \
	|| fail "the launcher exited early with charging-screen on desktop"
grep -qF "plugin charging-screen: opens after" "$log" && fail "charging-screen didn't stay off without a battery"

CS_FAKE="$WORK/charging-screen.fake"
printf '73 1\n' >"$CS_FAKE"
printf 'idle=1\n' >"$STATE/charging-screen.cfg"
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition\n'
build_plugin unplugger 'id=unplugger\nname=Unplugger\napi=1\n'
printf 'steps=wait,wait,wait,wait,wait,wait,a,wait,wait,wait,wait,wait,wait\n' >"$STATE/screener.cfg"
export MOSAIC_CHARGING_SCREEN_FAKE="$CS_FAKE"
log="$WORK/charging-screen.log"
run_launcher "$log" "plugin screener: done" || fail "the launcher exited early with charging-screen"
unset MOSAIC_CHARGING_SCREEN_FAKE
expect "opens after 1s idle on the list or home while charging, clock 24h, with the test battery"
expect "opened after 1s idle: 73%, Charging"
expect "drew 73%, Charging"
expect "closed by a button"
expect "closed: not charging"
grep -qF "plugin unplugger: unplugged" "$log" || fail "unplugger didn't unplug"
[ "$(seen)" = "gamelist charging-screen.main gamelist charging-screen.main gamelist " ] \
	|| fail "charging-screen didn't open, close, open and close: $(seen)"
# A opening the root's selected folder would be another push
[ "$(sed -n 's/.*plugin screener: transition \([a-z]*\) .*/\1/p' "$log" | tr '\n' ' ')" = "push pop push pop " ] \
	|| fail "charging-screen's closing press acted on the list: $(grep "plugin screener: transition" "$log")"
grep -qF "plugins: charging-screen's screen charging-screen.main's" "$log" && fail "charging-screen's screen was turned off"

[ -f "$HOME_LEFT" ] && cp "$HOME_LEFT" "$WORK/charging-home-left.saved"
rm -f "$HOME_LEFT"
printf '73 1\n' >"$CS_FAKE"
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition,home\n'
printf 'home=main\nsteps=wait,wait,wait,wait,wait,wait,a,wait,wait,wait,wait,wait,wait\n' >"$STATE/screener.cfg"
export MOSAIC_CHARGING_SCREEN_FAKE="$CS_FAKE"
log="$WORK/charging-screen-home.log"
run_launcher "$log" "plugin screener: done" || fail "the launcher exited early with charging-screen over home"
unset MOSAIC_CHARGING_SCREEN_FAKE
expect "opened after 1s idle: 73%, Charging"
expect "closed by a button"
expect "closed: not charging"
grep -qF "plugin screener: home screener.main" "$log" || fail "screener.main wasn't the home screen"
[ "$(seen)" = "screener.main charging-screen.main screener.main charging-screen.main screener.main " ] \
	|| fail "charging-screen didn't open, close, open and close over home: $(seen)"
grep -qF "plugin screener: input screener.main" "$log" && fail "charging-screen's closing press reached the home screen"
[ "$(sed -n 's/.*plugin screener: transition \([a-z]*\) .*/\1/p' "$log" | tr '\n' ' ')" = "push pop push pop " ] \
	|| fail "charging-screen over home didn't slide in and out twice: $(grep "plugin screener: transition" "$log")"
if [ -f "$WORK/charging-home-left.saved" ]; then mv "$WORK/charging-home-left.saved" "$HOME_LEFT"; else rm -f "$HOME_LEFT"; fi
rm -rf "$WORK/plugins/charging-screen.plugin" "$WORK/plugins/screener.plugin" "$WORK/plugins/unplugger.plugin" \
	"$STATE/screener.cfg" "$STATE/charging-screen.cfg" "$CS_FAKE"
case_log=""
