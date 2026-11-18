# shellcheck shell=sh

# plugin screens in the home slot, with screener and layerer as in 510-screens
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition\n'
build_plugin layerer 'id=layerer\nname=Layerer\napi=1\n'
mkdir -p "$STATE"
printf 'screener=on\nlayerer=on\n' >"$STATE/plugins.txt"

# what home_screen said, from the first tick on
homes() { sed -n 's/.*plugin screener: home //p' "$log" | tr '\n' ' '; }

# the home slot: a screen marked home without slots=home is an ordinary one
[ -f "$HOME_LEFT" ] && cp "$HOME_LEFT" "$WORK/home-left.saved"
rm -f "$HOME_LEFT"
printf 'home=main\nsteps=b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/home-noslot.log" "plugin screener: done" || fail "the launcher exited early with a home screen but no slot"
log="$WORK/home-noslot.log"
grep -qF "plugins: screener's screen screener.main is marked home but screener has no slots=home, so it's an ordinary screen" "$log" \
	|| fail "the home flag without the slot wasn't logged"
grep -qF "plugin screener: enter screener.main" "$log" && fail "screener.main opened as home without the slot"
[ "$(homes)" = "none " ] || fail "home_screen wasn't NULL with no home slot owner: $(homes)"
[ -f "$HOME_LEFT" ] && fail "the home file was written without a home screen"

# the slot with no screen marked home keeps the root list
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition,home\n'
printf 'steps=b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/home-none.log" "plugin screener: done" || fail "the launcher exited early owning the home slot with no home screen"
log="$WORK/home-none.log"
grep -qF "plugins: screener owns the home slot but registered no MOSAIC_SCREEN_HOME screen, using the root list" "$log" \
	|| fail "the home slot with no home screen wasn't logged"
grep -qF "plugin screener: enter screener.main" "$log" && fail "a screen opened as home with none marked"
[ "$(homes)" = "none " ] || fail "home_screen wasn't NULL with no screen marked home: $(homes)"

# the home screen shows first, with no slide; closing it goes on to the
# root list as a push, B at the root goes back to it as a pop, and closing
# it again pushes the list back in
printf 'home=main\nsteps=b,b,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/home.log" "plugin screener: done" || fail "the launcher exited early with a home screen"
log="$WORK/home.log"
plugin=screener
grep -qF "plugins: screener's screen screener.main is the home screen" "$log" || fail "the home screen wasn't claimed"
[ "$(sed -n 's/.*plugin screener: now //p' "$log" | head -1)" = "screener.main" ] || fail "the launcher didn't start on the home screen"
expect "enter screener.main over gamelist"
[ "$(grep -cF "plugin screener: enter screener.main over gamelist" "$log")" -eq 2 ] || fail "B at the root didn't go back to the home screen"
[ "$(grep -cF "plugin screener: now gamelist" "$log")" -eq 2 ] || fail "closing the home screen didn't show the root list twice"
[ "$(sed -n 's/.*plugin screener: transition \([a-z]*\) .*/\1/p' "$log" | tr '\n' ' ')" = "push pop push " ] \
	|| fail "home's slides weren't push, pop, push: $(grep "plugin screener: transition" "$log")"
expect "run from init -1"
# home_screen names it from the first tick, open or not, and is NULL from init
expect "home_screen from init none"
[ "$(homes)" = "screener.main " ] || fail "home_screen didn't stay screener.main: $(homes)"

# run_action opens the quick menu and the game switcher from the home
# screen, closing it, and B on them goes back to it; unknown and
# unavailable actions are refused
printf 'home=main\nsteps=action:nope,action:gamelist.back,action:gamelist.quick_menu,b,action:gamelist.switcher,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/home-actions.log" "plugin screener: done" || fail "the launcher exited early running actions from the home screen"
log="$WORK/home-actions.log"
expect "run nope -1"
expect "run gamelist.back -1"
expect "run gamelist.quick_menu 0"
expect "run gamelist.switcher 0"
[ "$(sed -n 's/.*plugin screener: now //p' "$log" | tr '\n' ' ')" = "screener.main quickmenu screener.main switcher screener.main " ] \
	|| fail "the quick menu and switcher didn't go back to the home screen: $(grep "plugin screener: now" "$log")"
[ "$(grep -cF "plugin screener: exit screener.main" "$log")" -ge 2 ] || fail "the quick menu and switcher didn't close the home screen"
grep -qF "plugins: closed screen screener.main, as the quickmenu opened over it" "$log" || fail "the quick menu over the home screen wasn't logged"

# a launch from the home screen comes back to it, and one from the list
# comes back to the list; /tmp/next and /tmp/last.txt are put back after
PAK="$WORK/home/Home.pak"
mkdir -p "$PAK"
: >"$PAK/launch.sh"
[ -f /tmp/next ] && cp /tmp/next "$WORK/home-next.saved"
[ -f /tmp/last.txt ] && cp /tmp/last.txt "$WORK/home-last.saved"
printf 'home=main\nsteps=pak:%s\n' "$PAK" >"$STATE/screener.cfg"
run_launcher "$WORK/home-launch.log" && fail "the launcher didn't quit to launch a pak from the home screen"
log="$WORK/home-launch.log"
expect "open pak 1"
[ "$(cat "$HOME_LEFT" 2>/dev/null)" = "home" ] || fail "a launch from the home screen didn't save home: $(cat "$HOME_LEFT" 2>/dev/null)"
printf 'home=main\nsteps=\n' >"$STATE/screener.cfg"
run_launcher "$WORK/home-return.log" "plugin screener: done" || fail "the launcher exited early back from the home screen's pak"
log="$WORK/home-return.log"
[ "$(sed -n 's/.*plugin screener: now //p' "$log" | head -1)" = "screener.main" ] || fail "back from a pak, the launcher didn't start on the home screen"
[ -f "$HOME_LEFT" ] && fail "the home file outlived the start it was for"
printf 'home=main\nsteps=b,pak:%s\n' "$PAK" >"$STATE/screener.cfg"
run_launcher "$WORK/home-list-launch.log" && fail "the launcher didn't quit to launch a pak from the list"
[ "$(cat "$HOME_LEFT" 2>/dev/null)" = "list" ] || fail "a launch from the list didn't save list: $(cat "$HOME_LEFT" 2>/dev/null)"
printf 'home=main\nsteps=\n' >"$STATE/screener.cfg"
run_launcher "$WORK/home-list-return.log" "plugin screener: done" || fail "the launcher exited early back from the list's pak"
log="$WORK/home-list-return.log"
[ "$(sed -n 's/.*plugin screener: now //p' "$log" | head -1)" = "gamelist" ] || fail "back from a pak launched from the list, the launcher didn't start on the list"
grep -qF "plugin screener: enter screener.main" "$log" && fail "the home screen opened back from a pak launched from the list"
if [ -f "$WORK/home-next.saved" ]; then cp "$WORK/home-next.saved" /tmp/next; else rm -f /tmp/next; fi
if [ -f "$WORK/home-last.saved" ]; then cp "$WORK/home-last.saved" /tmp/last.txt; else rm -f /tmp/last.txt; fi
rm -rf "${WORK:?}/home" "$WORK/home-next.saved" "$WORK/home-last.saved"

# starting on the game switcher or the quick menu (Settings' start
# screen), B goes back to the home screen rather than the list
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/minuisettings.txt.saved"
for view in 1 2; do
	name=switcher
	[ "$view" -eq 2 ] && name=quickmenu
	printf 'defaultView=%s\n' "$view" >"$SETTINGS"
	printf 'home=main\nsteps=b\n' >"$STATE/screener.cfg"
	SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$WORK/home-view$view.log" "plugin screener: done" \
		|| fail "the launcher exited early starting on the $name with a home screen"
	log="$WORK/home-view$view.log"
	grep -qF "plugins: the $name goes back to the home screen" "$log" || fail "the $name didn't note the home screen under it"
	[ "$(sed -n 's/.*plugin screener: now //p' "$log" | tr '\n' ' ')" = "$name screener.main " ] \
		|| fail "B on the $name didn't go to the home screen: $(grep "plugin screener: now" "$log")"
	expect "enter screener.main over gamelist"
done
if [ -f "$WORK/minuisettings.txt.saved" ]; then cp "$WORK/minuisettings.txt.saved" "$SETTINGS"; else rm -f "$SETTINGS"; fi
rm -f "$WORK/minuisettings.txt.saved"

# a home screen that fails leaves the root list for the rest of the run
printf 'home=fail\nfail=render\nsteps=b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/home-fail.log" "plugin screener: done" || fail "the launcher exited early with a failing home screen"
log="$WORK/home-fail.log"
grep -qF "plugins: using the root list in place of the home screen until the launcher restarts" "$log" \
	|| fail "the failing home screen's fallback wasn't logged"
expect "exit screener.fail"
[ "$(grep -cF "plugin screener: enter screener.fail" "$log")" -eq 1 ] || fail "B at the root opened the failed home screen again"
[ "$(homes)" = "screener.fail none " ] || fail "home_screen wasn't NULL after the home screen failed: $(homes)"

rm -f "$HOME_LEFT"
if [ -f "$WORK/home-left.saved" ]; then cp "$WORK/home-left.saved" "$HOME_LEFT"; fi
rm -f "$WORK/home-left.saved"
rm -rf "$WORK/plugins/screener.plugin" "$WORK/plugins/layerer.plugin" "$STATE/screener.cfg" "$STATE/layerer.cfg" "$STATE/plugins.txt"
case_log=""
