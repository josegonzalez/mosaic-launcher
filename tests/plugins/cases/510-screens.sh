# shellcheck shell=sh

PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi

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
	expect "hints refused again -1 wide -1 own -1 group -1 none -1 three -1 unknown -1 fn -1 null -1 empty -1"
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

rm -rf "$WORK/plugins/screener.plugin" "$WORK/plugins/layerer.plugin" "$STATE/screener.cfg" "$STATE/layerer.cfg" "$STATE/plugins.txt"
case_log=""
