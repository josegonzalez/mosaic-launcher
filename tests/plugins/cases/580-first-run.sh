# shellcheck shell=sh
# shard: 1

# the bundled first-run: on a card where it hasn't been seen it opens by
# itself over the game list, and screener walks it: on to the toggles,
# flipping two, back to the welcome, then on through the PIN, plugins and
# last pages and A to finish, which sets its seen setting. Started again
# it stays closed, and its action opens it, with a PIN set and the
# suggestions read from plugins.txt, until Y skips it. With a home screen
# (screener's) it opens over that instead, and closing goes back there.
FR_CFG="$STATE/first-run.cfg"
FR_PIN="$STATE/pin.txt"
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" first-run >/dev/null || fail "building first-run"
build_plugin screener 'id=screener\nname=Screener\napi=1\n'
mkdir -p "$STATE" "$(dirname "$SETTINGS")"
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/first-run-settings.saved"
rm -f "$FR_CFG" "$FR_PIN" "$HOME_LEFT"

printf 'steps=a,right,down,right,b,a,a,a,a,a\n' >"$STATE/screener.cfg"
run_launcher "$WORK/first-run.log" "plugin screener: done" || fail "the launcher exited early with first-run"
log="$WORK/first-run.log"
plugin=first-run
expect "not seen yet"
expect "opened by itself over gamelist"
grep -qE "plugin first-run: toggles: show_clock (on|off), show_battery_percent (on|off), haptics (on|off), notify_adjustments (on|off)$" "$log" \
	|| fail "first-run didn't list stock's toggles"
expect "pin: not set"
expect "suggesting home-dashboard off, switcher-plus off, favorites off, search off"
expect "showing welcome, 1 of 5"
expect "showing toggles, 2 of 5"
grep -qE "plugin first-run: show_clock now (on|off)$" "$log" || fail "first-run didn't flip the clock"
grep -qE "plugin first-run: show_battery_percent now (on|off)$" "$log" || fail "first-run didn't flip the battery percentage"
expect "showing pin, 3 of 5"
expect "showing plugins, 4 of 5"
expect "showing done, 5 of 5"
expect "finished"
expect "closed"
[ "$(seen)" = "first-run.main gamelist " ] || [ "$(seen)" = "gamelist first-run.main gamelist " ] \
	|| fail "first-run didn't open over the game list and close: $(seen)"
grep -qx "seen=on" "$FR_CFG" || fail "first-run didn't save that it was seen"
grep -qF "plugins: first-run's screen first-run.main's" "$log" && fail "first-run's screen was turned off"

# seen: it stays closed, and its action opens it until Y skips it
printf '1234\n' >"$FR_PIN"
printf 'first-run=on\nscreener=on\n' >"$STATE/plugins.txt"
printf 'suggest=first-run, favorites\n' >>"$FR_CFG"
printf 'steps=action:first-run.open,y\n' >"$STATE/screener.cfg"
run_launcher "$WORK/first-run-seen.log" "plugin screener: done" || fail "the launcher exited early with first-run seen"
log="$WORK/first-run-seen.log"
expect "already seen"
grep -qF "plugin first-run: opened by itself" "$log" && fail "first-run opened by itself once seen"
expect "pin: set"
expect "suggesting first-run on, favorites off"
expect "skipped"
[ "$(seen)" = "gamelist first-run.main gamelist " ] || fail "first-run's action didn't open it: $(seen)"
rm -f "$FR_PIN" "$STATE/plugins.txt"

# over a home screen, skipped with Y, back to the home screen
rm -f "$FR_CFG" "$HOME_LEFT"
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=home\n'
printf 'home=main\nsteps=y\n' >"$STATE/screener.cfg"
run_launcher "$WORK/first-run-home.log" "plugin screener: done" || fail "the launcher exited early with first-run and a home screen"
log="$WORK/first-run-home.log"
expect "opened by itself over screener.main"
expect "skipped"
case "$(seen)" in
*"first-run.main screener.main ") ;;
*) fail "first-run didn't open over the home screen and go back to it: $(seen)" ;;
esac
grep -qx "seen=on" "$FR_CFG" || fail "first-run didn't save that it was skipped"

rm -f "$SETTINGS" "$FR_CFG" "$HOME_LEFT"
[ -e "$WORK/first-run-settings.saved" ] && mv "$WORK/first-run-settings.saved" "$SETTINGS"
rm -rf "$WORK/plugins/first-run.plugin" "$WORK/plugins/screener.plugin" "$STATE/screener.cfg"
case_log=""
