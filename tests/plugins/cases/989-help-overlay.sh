# shellcheck shell=sh

# first-run's help overlay: its action, run by screener, opens it over the
# game list, which lists the game list's own buttons and the shared Y
# menu of screener's Open and the help itself, which the help_button
# setting puts on Y. A press closes it without acting on the screen under
# it: SELECT doesn't open the game switcher. Over the game switcher it
# lists the switcher's own buttons and B closes it there; over first-run's
# Buttons page what that screen declares; over screener's bare screen, which
# has no input, B BACK; and over the quick menu its own buttons.
HO_CFG="$STATE/first-run.cfg"
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" first-run >/dev/null || fail "building first-run"
build_plugin screener 'id=screener\nname=Screener\napi=1\n'
mkdir -p "$STATE"
rm -f "$HO_CFG" "$HOME_LEFT"
printf 'first-run=on\nscreener=on\n' >"$STATE/plugins.txt"
printf 'seen=on\nhelp_button=Y\n' >"$HO_CFG"
# over the game list, closed with SELECT, then over the game switcher
printf 'steps=action:first-run.help,select,select,action:first-run.help,b,b\n' >"$STATE/screener.cfg"
log="$WORK/help-overlay.log"
run_launcher "$log" "plugin screener: done" || fail "the launcher exited early with first-run's help"
plugin=first-run
expect "help over gamelist"
expect "help A: OPEN"
grep -qE "plugin first-run: help Y: MENU: (Open, Help|Help, Open)$" "$log" || fail "first-run's help didn't list the Y menu: $(grep "help Y" "$log")"
expect "help MENU: QUICK MENU"
expect "help SELECT: SWITCHER"
expect "help over switcher"
expect "help B: BACK"
expect "help SELECT: BACK"
[ "$(grep -c "plugin first-run: help closed" "$log")" -eq 2 ] || fail "first-run's help didn't close twice: $(grep -c "help closed" "$log")"
# the first SELECT only closed it, and B closed it over the switcher
[ "$(seen)" = "gamelist switcher gamelist " ] || fail "first-run's help acted on the screen under it: $(seen)"

# over first-run's Buttons page and screener's bare screen
printf 'steps=action:first-run.buttons,action:first-run.help,b,b,open:screener.bare,action:first-run.help,b,b\n' >"$STATE/screener.cfg"
log="$WORK/help-overlay-screens.log"
run_launcher "$log" "plugin screener: done" || fail "the launcher exited early with first-run's help over screens"
expect "help over first-run.buttons"
expect "help A: CLOSE"
expect "help over screener.bare"
expect "help B: BACK"
[ "$(grep -c "plugin first-run: help closed" "$log")" -eq 2 ] || fail "first-run's help didn't close over the screens: $(grep -c "help closed" "$log")"
[ "$(seen)" = "gamelist first-run.buttons gamelist screener.bare gamelist " ] || fail "first-run's help acted on a screen under it: $(seen)"

# over the quick menu
printf 'steps=action:gamelist.quick_menu,action:first-run.help,b,b\n' >"$STATE/screener.cfg"
log="$WORK/help-overlay-quick.log"
run_launcher "$log" "plugin screener: done" || fail "the launcher exited early with first-run's help over the quick menu"
expect "help over quickmenu"
expect "help B: BACK"
expect "help MENU: BACK"
expect "help closed"
[ "$(seen)" = "gamelist quickmenu gamelist " ] || fail "first-run's help acted on the quick menu: $(seen)"
grep -qF "plugins: first-run's" "$WORK"/help-overlay*.log && fail "a first-run hook failed"

rm -f "$HO_CFG" "$STATE/plugins.txt" "$STATE/screener.cfg"
rm -rf "$WORK/plugins/first-run.plugin" "$WORK/plugins/screener.plugin"
case_log=""
