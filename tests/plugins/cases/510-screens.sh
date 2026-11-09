# shellcheck shell=sh
# shard: 2
# uses the presser 500-plugins-page builds, so it shares its shard

# plugin screens: screener's Y action opens screener.main over the game
# list, sliding in as a push whose old frame has layerer's overlay over the
# list and whose new frame is the screen before overlays; B closes it, a
# pop whose old frame has the overlay over the screen
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition\n'
build_plugin layerer 'id=layerer\nname=Layerer\napi=1\n'
mkdir -p "$STATE"
printf 'screener=on\nlayerer=on\n' >"$STATE/plugins.txt"
printf 'steps=y,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/screener.log" "plugin screener: done" || fail "the launcher exited early with screener loaded"
log="$WORK/screener.log"
plugin=screener
expect "refused dup -1 builtin -1 empty -1 no render -1 open -1 close -1"
expect "action open 0"
expect "enter screener.main over gamelist"
expect "now screener.main"
expect "transition push old FF00FFFF new 0000FFFF"
grep -qE "plugin screener: render screener.main ([0-9]+)x([0-9]+) area \1x\2 status [1-9][0-9]* drawn 1 current screener.main open -1 select -1$" "$log" \
	|| fail "screener.main wasn't lent the screen with its header: $(grep "plugin screener: render" "$log")"
expect "input screener.main pressed 2"
expect "exit screener.main"
expect "screener.main drew every frame"
expect "transition pop old FF00FFFF"
[ "$(grep -cF "plugin screener: now gamelist" "$log")" -eq 2 ] || fail "B didn't go back to the game list"
grep -qF "plugins: closed screen screener.main" "$log" || fail "closing screener.main wasn't logged"
plugin=layerer
expect "over gamelist clear 1 size 1 1 select -1 open -1"
expect "over screener.main clear 1 size 1 1 select -1 open -1"

# open_screen opens screens over each other, one at a time; close_screen
# closes the ones above too, top first; registering after init is refused;
# and without an input hook, B closes a screen
printf 'steps=open:screener.bare,open:screener.bare,open:screener.main,close:screener.bare,register,open:screener.bare,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/screener-stack.log" "plugin screener: done" || fail "the launcher exited early stacking screens"
log="$WORK/screener-stack.log"
plugin=screener
expect "open screener.bare 0"
expect "enter screener.bare over gamelist"
expect "open screener.bare -1"
expect "enter screener.main over screener.bare"
expect "open screener.main 0"
expect "now screener.main"
expect "close screener.bare 0"
main_at=$(grep -nF "plugin screener: exit screener.main" "$log" | head -1 | cut -d: -f1)
bare_at=$(grep -nF "plugin screener: exit screener.bare" "$log" | head -1 | cut -d: -f1)
if [ -z "$main_at" ] || [ -z "$bare_at" ] || [ "$main_at" -gt "$bare_at" ]; then
	fail "close_screen didn't close the top screen first"
fi
expect "register from tick -1"
[ "$(grep -cF "plugin screener: exit screener.bare" "$log")" -eq 2 ] || fail "B didn't close screener.bare"
[ "$(grep -cF "plugin screener: now gamelist" "$log")" -eq 3 ] || fail "screener didn't see the game list again after each close"

# a dialog opened from a screen's input shows over it and goes back to it;
# no screen opens while it shows, and overlays leave it alone
printf 'steps=open:screener.main,a,open:screener.bare,a,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/screener-dialog.log" "plugin screener: done" || fail "the launcher exited early with a dialog over a screen"
log="$WORK/screener-dialog.log"
expect "now dialog"
expect "open screener.bare -1"
expect "yes, now screener.main"
expect "exit screener.main"
grep -qF "plugin layerer: over dialog" "$log" && fail "layerer's overlay was drawn over a dialog"

# a MOSAIC_SCREEN_FULL screen has no header and no overlays
printf 'steps=open:screener.full,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/screener-full.log" "plugin screener: done" || fail "the launcher exited early with a full screen"
log="$WORK/screener-full.log"
grep -qE "plugin screener: render screener.full [0-9]+x[0-9]+ area [0-9]+x[0-9]+ status 0 drawn 0 " "$log" \
	|| fail "screener.full had a header: $(grep "plugin screener: render" "$log")"
expect "transition pop old 0000FFFF"
grep -qF "plugin layerer: over screener.full" "$log" && fail "layerer's overlay was drawn over a full screen"

# button hints on plugin screens: draw_hints draws stock's pills where
# stock's own screens put them, with the labels presser writes down, and
# only on the screen lent to a screen's render
# hinted <name> <hints> <steps>: screener with that hints setting, its
# drawn text in $texts
hinted() {
	texts="$WORK/$1.texts"
	: >"$texts"
	printf 'hints=%s\nsteps=%s\n' "$2" "$3" >"$STATE/screener.cfg"
	PRESSER_LOG="$texts"
	export PRESSER_LOG
	SMOKE_PRELOAD="$PRESSER"
	export SMOKE_PRELOAD
	run_launcher "$WORK/$1.log" "plugin screener: done" || fail "the launcher exited early drawing hints ($2)"
	unset PRESSER_LOG SMOKE_PRELOAD
	log="$WORK/$1.log"
	plugin=screener
}
# hints_line <hints> <screen> <left rc> <right rc>: the first render's line,
# with the pills where stock puts them, in its accent colour and no
# further; the calls it refuses; and no render with a group refused
hints_line() {
	grep -qE "plugin screener: hints $1 on $2 left $3 [0-9-]+,[0-9-]+ [0-9]+x[0-9]+ right $4 [0-9-]+,[0-9-]+ [0-9]+x[0-9]+ stock 1 " "$log" \
		|| fail "screener's $1 hints on $2 weren't where stock puts them: $(grep "plugin screener: hints $1" "$log")"
	grep -qE "plugin screener: hints $1 .* pill ([0-9A-F]{8}) accent \1 outside clear 1$" "$log" \
		|| fail "screener's $1 pill wasn't the accent colour, or went past its edge: $(grep "plugin screener: hints $1" "$log")"
	expect "hints refused again -1 wide -1 own -1 group -1 none -1 three -1 dpad -1 fn -1 null -1 empty -1"
	grep -qE "plugin screener: hints drawn [1-9][0-9]* refused 0$" "$log" \
		|| fail "screener's $1 hints were refused: $(grep "plugin screener: hints drawn" "$log")"
}
hinted hints-none "" "open:screener.main,b"
grep -qxF "REMOVE" "$texts" && fail "something drew REMOVE without screener's hints"
grep -qF "plugin screener: hints" "$log" && fail "screener drew hints without its hints setting"
hinted hints-switcher switcher "open:screener.main,b"
hints_line switcher screener.main 0 0
expect "hints from tick -1"
expect "hints on the overlay over gamelist -1"
for label in BACK REMOVE RESUME; do drew "$label"; done
expect "exit screener.main"
# a full screen, and the right group alone
hinted hints-full open "open:screener.full,b"
hints_line open screener.full 0 0
hinted hints-back back "open:screener.bare,b"
hints_line back screener.bare 1 0
# every button stock names, from a different set each frame
hinted hints-names names "open:screener.main,b"
hints_line names screener.main 0 0
for label in L1 R1 L2 R2 START SELECT INFO SLEEP DONE MORE PREV NEXT UP DOWN SORT FIND PICK; do drew "$label"; done
rm -f "$PRESSER"

# a screen whose render or input fails closes, back to the game list, and
# can't open again until the launcher restarts
for hook in render input; do
	printf 'steps=open:screener.fail,b,open:screener.fail\nfail=%s\n' "$hook" >"$STATE/screener.cfg"
	run_launcher "$WORK/screener-fail-$hook.log" "plugin screener: done" || fail "the launcher exited early with a failing $hook"
	log="$WORK/screener-fail-$hook.log"
	grep -qE "plugins: screener's screen screener.fail's $hook returned -[23], closing it until the launcher restarts" "$log" \
		|| fail "screener.fail's $hook error wasn't logged"
	expect "exit screener.fail"
	expect "open screener.fail -1"
	[ "$(grep -cF "plugin screener: now gamelist" "$log")" -eq 2 ] || fail "screener.fail's $hook error didn't go back to the game list"
done

# from the quick menu and the game switcher (Settings' start screen), a
# screen goes back to where it opened
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/minuisettings.txt.saved"
for view in 1 2; do
	name=switcher
	[ "$view" -eq 2 ] && name=quickmenu
	printf 'defaultView=%s\n' "$view" >"$SETTINGS"
	printf 'steps=open:screener.bare,b\n' >"$STATE/screener.cfg"
	SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$WORK/screener-view$view.log" "plugin screener: done" \
		|| fail "the launcher exited early opening a screen over the $name"
	log="$WORK/screener-view$view.log"
	expect "enter screener.bare over $name"
	expect "exit screener.bare"
	[ "$(grep -cF "plugin screener: now $name" "$log")" -eq 2 ] || fail "closing screener.bare didn't go back to the $name"
	grep -qF "plugin layerer: over $name" "$log" || fail "layerer's overlay wasn't drawn over the $name"
done
if [ -f "$WORK/minuisettings.txt.saved" ]; then cp "$WORK/minuisettings.txt.saved" "$SETTINGS"; else rm -f "$SETTINGS"; fi
rm -f "$WORK/minuisettings.txt.saved"

# an overlay that fails is left out from then on
printf 'steps=open:screener.bare,b\n' >"$STATE/screener.cfg"
printf 'fail=1\n' >"$STATE/layerer.cfg"
run_launcher "$WORK/layerer-fail.log" "plugin screener: done" || fail "the launcher exited early with a failing overlay"
log="$WORK/layerer-fail.log"
[ "$(grep -cF "plugins: layerer's overlay_render returned -1, leaving its overlay out until the launcher restarts" "$log")" -eq 1 ] \
	|| fail "layerer's failing overlay wasn't logged once"
rm -f "$STATE/layerer.cfg"

# the home slot: a screen marked home without slots=home is an ordinary one
[ -f "$HOME_LEFT" ] && cp "$HOME_LEFT" "$WORK/home-left.saved"
rm -f "$HOME_LEFT"
printf 'home=main\nsteps=b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/home-noslot.log" "plugin screener: done" || fail "the launcher exited early with a home screen but no slot"
log="$WORK/home-noslot.log"
grep -qF "plugins: screener's screen screener.main is marked home but screener has no slots=home, so it's an ordinary screen" "$log" \
	|| fail "the home flag without the slot wasn't logged"
grep -qF "plugin screener: enter screener.main" "$log" && fail "screener.main opened as home without the slot"
[ -f "$HOME_LEFT" ] && fail "the home file was written without a home screen"

# the slot with no screen marked home keeps the root list
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition,home\n'
printf 'steps=b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/home-none.log" "plugin screener: done" || fail "the launcher exited early owning the home slot with no home screen"
log="$WORK/home-none.log"
grep -qF "plugins: screener owns the home slot but registered no MOSAIC_SCREEN_HOME screen, using the root list" "$log" \
	|| fail "the home slot with no home screen wasn't logged"
grep -qF "plugin screener: enter screener.main" "$log" && fail "a screen opened as home with none marked"

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

# the switcher slot: recents for it (one with a save state and its
# preview, one without, one whose emulator isn't installed), put back after
SWITCHED="$CARD/.userdata/shared/.minui/game_switcher.txt"
SW_ROMS="$CARD/Roms/Switcher Test (SWT)"
SW_GONE="$CARD/Roms/Switcher Gone (SWGONE)"
SW_EMU="$CARD/Emus/desktop/SWT.pak"
SW_STATES="$CARD/.userdata/shared/.minui/SWT"
mkdir -p "$SW_ROMS" "$SW_GONE" "$SW_EMU" "$SW_STATES"
: >"$SW_ROMS/Game.gb"
: >"$SW_ROMS/Other.gb"
: >"$SW_GONE/Gone.gb"
: >"$SW_EMU/launch.sh"
printf '2' >"$SW_STATES/Game.gb.txt"
: >"$SW_STATES/Game.gb.2.bmp"
[ -f "$RECENTS" ] && cp "$RECENTS" "$WORK/switcher-recent.saved"
for saved in /tmp/next /tmp/last.txt /tmp/resume_slot.txt "$SWITCHED"; do
	[ -f "$saved" ] && cp "$saved" "$WORK/switcher-$(basename "$saved").saved"
done
rm -f "$SWITCHED" "$HOME_LEFT"
sw_recents() {
	printf '/Roms/Switcher Test (SWT)/Game.gb\tSaved Game\n/Roms/Switcher Test (SWT)/Other.gb\n/Roms/Switcher Gone (SWGONE)/Gone.gb\n' >"$RECENTS"
}
sw_recents

# a screen marked switcher without slots=switcher is an ordinary one
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition\n'
printf 'switcher=bare\nsteps=action:gamelist.switcher,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-noslot.log" "plugin screener: done" || fail "the launcher exited early with a switcher screen but no slot"
log="$WORK/switcher-noslot.log"
grep -qF "plugins: screener's screen screener.bare is marked switcher but screener has no slots=switcher, so it's an ordinary screen" "$log" \
	|| fail "the switcher flag without the slot wasn't logged"
[ "$(seen)" = "gamelist switcher gamelist " ] || fail "the built-in switcher didn't show without the slot: $(seen)"

# the slot with no screen marked switcher keeps the built-in one
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition,switcher\n'
printf 'steps=action:gamelist.switcher,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-none.log" "plugin screener: done" || fail "the launcher exited early owning the switcher slot with no screen"
log="$WORK/switcher-none.log"
grep -qF "plugins: screener owns the switcher slot but registered no MOSAIC_SCREEN_GAME_SWITCHER screen other than its home screen, using the built-in switcher" "$log" \
	|| fail "the switcher slot with no switcher screen wasn't logged"
[ "$(seen)" = "gamelist switcher gamelist " ] || fail "the built-in switcher didn't show with no switcher screen: $(seen)"

# SELECT and gamelist.switcher open the switcher screen over the game
# list, at once; screens open over it and close back to it without
# sliding, open_screen can't open it, it reads the recents, and B goes
# back to the list
printf 'switcher=bare\nsteps=select,b,action:gamelist.switcher,open:screener.main,open:screener.bare,b,recents,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher.log" "plugin screener: done" || fail "the launcher exited early with a switcher screen"
log="$WORK/switcher.log"
plugin=screener
grep -qF "plugins: screener's screen screener.bare is the game switcher" "$log" || fail "the switcher screen wasn't claimed"
[ "$(seen)" = "gamelist screener.bare gamelist screener.bare screener.main screener.bare gamelist " ] \
	|| fail "the switcher screen didn't open and close as the switcher: $(seen)"
[ "$(grep -cF "plugin screener: enter screener.bare over gamelist" "$log")" -eq 2 ] || fail "the switcher screen didn't open over the game list twice"
expect "enter screener.main over screener.bare"
expect "open screener.bare -1"
grep -qF "plugins: can't open screen screener.bare: it's the game switcher" "$log" || fail "open_screen on the switcher screen wasn't refused"
grep -qF "plugin screener: transition" "$log" && fail "something slid to or from the switcher screen: $(grep "plugin screener: transition" "$log")"
expect "recents from init info -1 null -1 open 0"
expect "recents 3"
expect "recent 0 0 name Saved Game type 2 available 1 resume 1 preview Game.gb.2.bmp"
expect "recent 1 0 name Other type 2 available 1 resume 0 preview -"
expect "recent 2 0 name Gone type 2 available 0 resume 0 preview -"
expect "recent 3 -1 name  type 0 available 0 resume 0 preview -"
grep -qE "plugin screener: old size [0-9]+ name Saved Game type 2$" "$log" || fail "a smaller MosaicRecent wasn't filled as far as it goes"
grep -qF "plugin layerer: over screener.bare" "$log" || fail "layerer's overlay wasn't drawn over the switcher screen"

# remove takes a recent out; one whose emulator isn't installed doesn't open
printf 'switcher=bare\nsteps=action:gamelist.switcher,play:2:1,remove:1,recents,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-remove.log" "plugin screener: done" || fail "the launcher exited early removing a recent"
log="$WORK/switcher-remove.log"
expect "play 2 0"
expect "remove 1 0"
expect "recents 2"
expect "recent 1 0 name Gone type 2 available 0 resume 0 preview -"
[ "$(grep -c . "$RECENTS")" -eq 2 ] || fail "the removed recent wasn't saved: $(cat "$RECENTS")"
sw_recents

# a game resumed from the switcher screen comes back to it
printf 'switcher=bare\nsteps=action:gamelist.switcher,play:0:1\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-play.log" && fail "the launcher didn't quit to resume a recent"
log="$WORK/switcher-play.log"
expect "play 0 1"
grep -qE "SWT\.pak/launch\.sh.*Game\.gb" /tmp/next 2>/dev/null || fail "the recent didn't launch with its emulator: $(cat /tmp/next 2>/dev/null)"
[ "$(cat /tmp/resume_slot.txt 2>/dev/null)" = "2" ] || fail "the recent didn't resume its state's slot: $(cat /tmp/resume_slot.txt 2>/dev/null)"
[ -f "$SWITCHED" ] || fail "a launch from the switcher screen didn't save that it came from the switcher"
grep -qF "Saved Game" "$RECENTS" || fail "the recent lost its name: $(cat "$RECENTS")"
printf 'switcher=bare\nsteps=b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-return.log" "plugin screener: done" || fail "the launcher exited early back from a game from the switcher"
log="$WORK/switcher-return.log"
[ "$(seen)" = "screener.bare gamelist " ] || fail "back from a game, the launcher didn't start on the switcher screen: $(seen)"
expect "enter screener.bare over gamelist"
[ -f "$SWITCHED" ] && fail "the switcher file outlived the start it was for"

# fresh from the switcher screen, a game starts over; from the list, it
# doesn't come back to the switcher
printf 'switcher=bare\nsteps=action:gamelist.switcher,play:0:0\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-fresh.log" && fail "the launcher didn't quit to start a recent"
[ "$(cat /tmp/resume_slot.txt 2>/dev/null)" = "8" ] || fail "a fresh start resumed a state: $(cat /tmp/resume_slot.txt 2>/dev/null)"
rm -f "$SWITCHED"
printf 'switcher=bare\nsteps=play:1:1\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-list-play.log" && fail "the launcher didn't quit to open a recent from the list"
log="$WORK/switcher-list-play.log"
expect "play 1 1"
[ -f "$SWITCHED" ] && fail "a recent opened from the list came back to the switcher"
sw_recents

# Settings' start screen opens the switcher screen
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/minuisettings.txt.saved"
printf 'defaultView=1\n' >"$SETTINGS"
printf 'switcher=bare\nsteps=b\n' >"$STATE/screener.cfg"
SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$WORK/switcher-view.log" "plugin screener: done" \
	|| fail "the launcher exited early starting on the switcher screen"
log="$WORK/switcher-view.log"
[ "$(seen)" = "screener.bare gamelist " ] || fail "the start screen wasn't the switcher screen: $(seen)"
if [ -f "$WORK/minuisettings.txt.saved" ]; then cp "$WORK/minuisettings.txt.saved" "$SETTINGS"; else rm -f "$SETTINGS"; fi
rm -f "$WORK/minuisettings.txt.saved"

# an enter that declines shows the built-in switcher that time
printf 'switcher=bare\ndecline=1\nsteps=action:gamelist.switcher,b,action:gamelist.switcher,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-decline.log" "plugin screener: done" || fail "the launcher exited early with a declining switcher screen"
log="$WORK/switcher-decline.log"
expect "declined screener.bare over gamelist"
[ "$(seen)" = "gamelist switcher gamelist screener.bare gamelist " ] || fail "a declined switcher screen didn't give way once: $(seen)"

# a switcher screen that fails gives way to the built-in switcher for the run
printf 'switcher=fail\nfail=render\nsteps=action:gamelist.switcher,b,action:gamelist.switcher,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-fail.log" "plugin screener: done" || fail "the launcher exited early with a failing switcher screen"
log="$WORK/switcher-fail.log"
grep -qF "plugins: using the built-in game switcher in place of screener.fail until the launcher restarts" "$log" \
	|| fail "the failing switcher screen's fallback wasn't logged"
expect "exit screener.fail"
[ "$(seen)" = "gamelist screener.fail switcher gamelist switcher gamelist " ] || fail "the failed switcher screen didn't give way: $(seen)"
[ "$(grep -cF "plugin screener: enter screener.fail" "$log")" -eq 1 ] || fail "the failed switcher screen opened again"

# with a home screen: the switcher screen over it closes it, and B, or the
# quick menu's B over the switcher screen, goes back to it
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition,home,switcher\n'
printf 'home=main\nswitcher=bare\nsteps=action:gamelist.switcher,b,action:gamelist.switcher,action:gamelist.quick_menu,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-home.log" "plugin screener: done" || fail "the launcher exited early with home and switcher screens"
log="$WORK/switcher-home.log"
[ "$(seen)" = "screener.main screener.bare screener.main screener.bare quickmenu screener.main " ] \
	|| fail "the switcher screen didn't go back to the home screen: $(seen)"
grep -qF "plugins: closed screen screener.main, as the switcher opened over it" "$log" || fail "the switcher screen didn't close the home screen"
grep -qF "plugins: closed screen screener.bare, as the quickmenu opened over it" "$log" || fail "the quick menu didn't close the switcher screen"

# a screen marked both home and switcher is only the home screen
printf 'home=main\nswitcher=main\nsteps=\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-both.log" "plugin screener: done" || fail "the launcher exited early with one screen marked home and switcher"
log="$WORK/switcher-both.log"
grep -qF "plugins: screener's screen screener.main is the home screen" "$log" || fail "the screen marked both wasn't the home screen"
grep -qF "is the game switcher" "$log" && fail "the home screen was the switcher screen too"

# a game from the switcher screen over the home screen comes back to the
# switcher screen, and B on it to the home screen
printf 'home=main\nswitcher=bare\nsteps=action:gamelist.switcher,play:1:1\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-home-play.log" && fail "the launcher didn't quit to open a recent over the home screen"
[ "$(cat "$HOME_LEFT" 2>/dev/null)" = "home" ] || fail "a launch from the switcher screen over home didn't save home: $(cat "$HOME_LEFT" 2>/dev/null)"
printf 'home=main\nswitcher=bare\nsteps=b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-home-return.log" "plugin screener: done" || fail "the launcher exited early back from a game over the home screen"
log="$WORK/switcher-home-return.log"
[ "$(seen)" = "screener.bare screener.main " ] || fail "back from a game, the switcher screen didn't go back to the home screen: $(seen)"

# the bundled switcher-plus, in stock's style: SELECT opens it in place of
# the built-in switcher, showing the first recent's save state preview (a
# real image this time) and stock's hints; right moves on, Y removes, and
# B goes back to the list. Screener, without the switcher slot, presses the
# buttons and logs what shows
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" switcher-plus >/dev/null || fail "building switcher-plus"
build_plugin screener 'id=screener\nname=Screener\napi=1\nslots=transition\n'
rm -rf "$WORK/plugins/layerer.plugin"
rm -f "$HOME_LEFT" "$SWITCHED"
cp "$ROOT/tests/plugins/peeker/art.png" "$SW_STATES/Game.gb.2.bmp"
sw_recents
printf 'steps=select,right,y,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-plus.log" "plugin screener: done" || fail "the launcher exited early with switcher-plus"
log="$WORK/switcher-plus.log"
plugin=switcher-plus
grep -qF "plugins: switcher-plus's screen switcher-plus.switcher is the game switcher" "$log" || fail "switcher-plus didn't take the switcher"
[ "$(seen)" = "gamelist switcher-plus.switcher gamelist " ] || fail "switcher-plus didn't open and close as the switcher: $(seen)"
expect "style full"
expect "showing 0 of 3: Saved Game, preview drawn, resume, style full"
expect "showing 1 of 3: Other, preview none, open, style full"
expect "removed 1, 2 left"
expect "showing 0 of 2: Saved Game, preview drawn, resume, style full"
[ "$(grep -c . "$RECENTS")" -eq 2 ] || fail "switcher-plus's removal wasn't saved: $(cat "$RECENTS")"
grep -qF "plugins: switcher-plus's screen switcher-plus.switcher's" "$log" && fail "switcher-plus's screen was turned off"
sw_recents

# A resumes the selected recent, and the launcher comes back to switcher-plus
printf 'steps=action:gamelist.switcher,a\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-plus-play.log" && fail "the launcher didn't quit to resume a recent from switcher-plus"
log="$WORK/switcher-plus-play.log"
expect "opening 0: Saved Game"
grep -qE "SWT\.pak/launch\.sh.*Game\.gb" /tmp/next 2>/dev/null || fail "switcher-plus's recent didn't launch: $(cat /tmp/next 2>/dev/null)"
[ "$(cat /tmp/resume_slot.txt 2>/dev/null)" = "2" ] || fail "switcher-plus's recent didn't resume: $(cat /tmp/resume_slot.txt 2>/dev/null)"
printf 'steps=b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-plus-return.log" "plugin screener: done" || fail "the launcher exited early back from a game from switcher-plus"
log="$WORK/switcher-plus-return.log"
[ "$(seen)" = "switcher-plus.switcher gamelist " ] || fail "back from a game, the launcher didn't start on switcher-plus: $(seen)"
sw_recents

# the strip and list styles; in the list, down past the last recent wraps,
# and a recent whose emulator isn't installed doesn't open
printf 'style=strip\n' >"$STATE/switcher-plus.cfg"
printf 'steps=action:gamelist.switcher,right,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-plus-strip.log" "plugin screener: done" || fail "the launcher exited early with switcher-plus's strip"
log="$WORK/switcher-plus-strip.log"
expect "showing 0 of 3: Saved Game, preview drawn, resume, style strip"
expect "showing 1 of 3: Other, preview none, open, style strip"
grep -qF "plugins: switcher-plus's screen switcher-plus.switcher's" "$log" && fail "switcher-plus's strip was turned off"
printf 'style=list\n' >"$STATE/switcher-plus.cfg"
printf 'steps=action:gamelist.switcher,down,down,a,down,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-plus-list.log" "plugin screener: done" || fail "the launcher exited early with switcher-plus's list"
log="$WORK/switcher-plus-list.log"
expect "showing 0 of 3: Saved Game, preview drawn, resume, style list"
expect "showing 2 of 3: Gone, preview none, open, style list"
expect "can't open 2: Gone"
[ "$(grep -cF "plugin switcher-plus: showing 0 of 3: Saved Game, preview drawn" "$log")" -eq 2 ] || fail "down past the last recent didn't wrap"
grep -qF "plugins: switcher-plus's screen switcher-plus.switcher's" "$log" && fail "switcher-plus's list was turned off"

# switcher-plus's captions: Game was played 2h 15m over 2 sessions 3 days
# ago and Other never, in playtime-captions' defaults while it isn't
# loaded and in its settings while it is. Local time is about noon, as for
# tracker
[ -f "$PLAY_DB" ] && mv "$PLAY_DB" "$WORK/game_logs.sqlite.saved"
now=$(date +%s)
hours_behind=$(( $(date -u +%H | sed 's/^0//') - 12 ))
make_play_db <<SQL || fail "making the switcher captions fixture"
CREATE TABLE rom(id INTEGER PRIMARY KEY, type TEXT, name TEXT, file_path TEXT, image_path TEXT, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE UNIQUE INDEX rom_id_index ON rom(id);
CREATE TABLE play_activity(rom_id INTEGER, play_time INTEGER, created_at INTEGER DEFAULT (strftime('%s', 'now')), updated_at INTEGER);
CREATE INDEX play_activity_rom_id_index ON play_activity(rom_id);
INSERT INTO rom(id, type, name, file_path) VALUES(1, '', 'Game', 'Switcher Test (SWT)/Game.gb');
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 7500, $((now - 10 * 86400)));
INSERT INTO play_activity(rom_id, play_time, created_at) VALUES(1, 600, $((now - 3 * 86400)));
SQL
TZ="TST$(printf '%+d' "$hours_behind")"
export TZ
printf 'captions=on\n' >"$STATE/switcher-plus.cfg"
printf 'steps=action:gamelist.switcher,right,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-plus-captions.log" "plugin screener: done" || fail "the launcher exited early with switcher-plus's captions"
log="$WORK/switcher-plus-captions.log"
expect "style full, captions on"
expect "captions time on, last on, sessions off, format relative, never off, achievements off"
expect "showing 0 of 3: Saved Game, preview drawn, resume, style full, caption 2h 15m · 3 days ago"
expect "showing 1 of 3: Other, preview none, open, style full, no caption"
grep -qF "plugins: switcher-plus's screen switcher-plus.switcher's" "$log" && fail "switcher-plus's screen was turned off with captions"
make -s -C "$ROOT/plugins" playtime-captions ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" >/dev/null || fail "building playtime-captions"
printf 'sessions=on\nnever=on\n' >"$STATE/playtime-captions.cfg"
run_launcher "$WORK/switcher-plus-captions-set.log" "plugin screener: done" || fail "the launcher exited early with switcher-plus's captions and playtime-captions"
log="$WORK/switcher-plus-captions-set.log"
expect "captions time on, last on, sessions on, format relative, never on, achievements off"
expect "showing 0 of 3: Saved Game, preview drawn, resume, style full, caption 2h 15m · 3 days ago · 2 sessions"
expect "showing 1 of 3: Other, preview none, open, style full, caption Never played"
grep -qF "plugins: switcher-plus's screen switcher-plus.switcher's" "$log" && fail "switcher-plus's screen was turned off with playtime-captions"
# the list's, under the selected recent's preview
printf 'style=list\ncaptions=on\n' >"$STATE/switcher-plus.cfg"
printf 'steps=action:gamelist.switcher,down,b\n' >"$STATE/screener.cfg"
run_launcher "$WORK/switcher-plus-captions-list.log" "plugin screener: done" || fail "the launcher exited early with switcher-plus's captions in the list"
log="$WORK/switcher-plus-captions-list.log"
expect "showing 0 of 3: Saved Game, preview drawn, resume, style list, caption 2h 15m · 3 days ago · 2 sessions"
expect "showing 1 of 3: Other, preview none, open, style list, caption Never played"
grep -qF "plugins: switcher-plus's screen switcher-plus.switcher's" "$log" && fail "switcher-plus's list was turned off with captions"
# with the history unknown, nothing, even with never on
printf 'style=strip\ncaptions=on\n' >"$STATE/switcher-plus.cfg"
printf 'steps=action:gamelist.switcher,right,b\n' >"$STATE/screener.cfg"
MOSAIC_LAUNCHER_SQLITE="$WORK/no-such-libsqlite3"
export MOSAIC_LAUNCHER_SQLITE
run_launcher "$WORK/switcher-plus-captions-unknown.log" "plugin screener: done" || fail "the launcher exited early with switcher-plus's captions and no sqlite"
unset MOSAIC_LAUNCHER_SQLITE
log="$WORK/switcher-plus-captions-unknown.log"
expect "showing 0 of 3: Saved Game, preview drawn, resume, style strip, no caption"
expect "showing 1 of 3: Other, preview none, open, style strip, no caption"
unset TZ
rm -f "$PLAY_DB"
[ -f "$WORK/game_logs.sqlite.saved" ] && mv "$WORK/game_logs.sqlite.saved" "$PLAY_DB"
rm -rf "$WORK/plugins/playtime-captions.plugin" "$STATE/playtime-captions.cfg"
: >"$SW_STATES/Game.gb.2.bmp"
rm -rf "$WORK/plugins/switcher-plus.plugin" "$STATE/switcher-plus.cfg"

rm -rf "$SW_ROMS" "$SW_GONE" "$SW_EMU" "$SW_STATES"
rmdir "$CARD/Emus/desktop" 2>/dev/null
if [ -f "$WORK/switcher-recent.saved" ]; then cp "$WORK/switcher-recent.saved" "$RECENTS"; else rm -f "$RECENTS"; fi
for saved in /tmp/next /tmp/last.txt /tmp/resume_slot.txt "$SWITCHED"; do
	if [ -f "$WORK/switcher-$(basename "$saved").saved" ]; then cp "$WORK/switcher-$(basename "$saved").saved" "$saved"; else rm -f "$saved"; fi
	rm -f "$WORK/switcher-$(basename "$saved").saved"
done
rm -f "$WORK/switcher-recent.saved"

rm -f "$HOME_LEFT"
if [ -f "$WORK/home-left.saved" ]; then cp "$WORK/home-left.saved" "$HOME_LEFT"; fi
rm -f "$WORK/home-left.saved"
rm -rf "$WORK/plugins/screener.plugin" "$WORK/plugins/layerer.plugin" "$STATE/screener.cfg" "$STATE/layerer.cfg" "$STATE/plugins.txt"
case_log=""
