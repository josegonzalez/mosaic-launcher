# shellcheck shell=sh
# shard: 3

# filterer's input_filter runs every frame, before its tick; the desktop
# has no hold switch; the new buttons follow the d-pad; and bind_button and
# override_button refuse them
build_plugin filterer 'id=filterer\nname=Filterer\napi=1\n'
build_plugin sifter 'id=sifter\nname=Sifter\napi=1\n'
mkdir -p "$STATE"
printf 'filterer=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/filterer.log" "plugin filterer: done" || fail "the launcher exited early with filterer loaded"
log="$WORK/filterer.log"
plugin=filterer
expect "hold_switch -1"
expect "buttons MENU 14 POWER 15 FN1 16 FN2 17 FN3 18"
expect "bind MENU -1 POWER -1 FN1 -1 FN3 -1 UP -1"
expect "override MENU -1 POWER -1 FN2 -1"
grep -qE "plugin filterer: done: .*, calls [1-9][0-9]+, missed 0, doubled 0, sleeps 0$" "$log" \
	|| fail "filterer's filter didn't run once a frame before its tick: $(grep "plugin filterer: done" "$log")"
grep -qF "plugin filterer: input size" "$log" && fail "filterer's input has the wrong size"

# on the game list, down moves the selection, and sifter, after filterer
# in load order, sees the press
printf 'filterer=on\nsifter=on\n' >"$STATE/plugins.txt"
printf 'press=down\nhold=100\n' >"$STATE/filterer.cfg"
run_launcher "$WORK/filterer-down.log" "plugin filterer: done" || fail "the launcher exited early pressing down"
log="$WORK/filterer-down.log"
expect "filter pressed 800 held 800"
expect "filter released 800"
moved "$log" || fail "down didn't move the selection: $(grep "plugin filterer: done" "$log")"
grep -qF "plugin sifter: sees pressed 800 held 800" "$log" || fail "sifter didn't see down pressed"

# taken by filterer, it doesn't move, and sifter sees it held but not pressed
printf 'press=down\nhold=100\ntake=down\n' >"$STATE/filterer.cfg"
run_launcher "$WORK/filterer-take-down.log" "plugin filterer: done" || fail "the launcher exited early taking down"
log="$WORK/filterer-take-down.log"
expect "filter pressed 800 held 800"
moved "$log" && fail "down moved the selection while taken: $(grep "plugin filterer: done" "$log")"
grep -qF "plugin sifter: sees pressed 0 held 800" "$log" || fail "sifter didn't see down held without the press"
grep -qF "plugin sifter: sees pressed 800" "$log" && fail "sifter saw a press filterer took"

# a filter's error turns it off for the run: it takes down only until then
printf 'press=down\nhold=100\ntake=down\nfail=1\n' >"$STATE/filterer.cfg"
printf 'filterer=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/filterer-fail.log" "plugin filterer: done" || fail "the launcher exited early with filterer failing"
log="$WORK/filterer-fail.log"
[ "$(grep -cF "plugins: filterer's input_filter returned -1, no longer filtering input with it until the launcher restarts" "$log")" -eq 1 ] \
	|| fail "filterer's failing filter wasn't logged once"
grep -qE "plugin filterer: done: .*, calls 3, " "$log" || fail "filterer's filter was called after its error: $(grep "plugin filterer: done" "$log")"
moved "$log" || fail "down didn't move the selection after the filter's error"

# on the quick menu and the game switcher (Settings' start screen), B goes
# back to the game list, which then draws its rows, unless filterer takes it
[ -f "$SETTINGS" ] && cp "$SETTINGS" "$WORK/minuisettings.txt.saved"
for view in 1 2; do
	printf 'defaultView=%s\n' "$view" >"$SETTINGS"
	printf 'press=b\nhold=100\n' >"$STATE/filterer.cfg"
	SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$WORK/filterer-view$view.log" "plugin filterer: done" \
		|| fail "the launcher exited early starting on screen $view"
	log="$WORK/filterer-view$view.log"
	expect "filter pressed 2 held 2"
	grep -qE "plugin filterer: done: .*, rows [1-9]" "$log" || fail "B didn't leave screen $view: $(grep "plugin filterer: done" "$log")"
	printf 'press=b\nhold=100\ntake=b\n' >"$STATE/filterer.cfg"
	SHARED_USERDATA_PATH="$(dirname "$SETTINGS")" run_launcher "$WORK/filterer-view$view-take.log" "plugin filterer: done" \
		|| fail "the launcher exited early starting on screen $view taking B"
	log="$WORK/filterer-view$view-take.log"
	expect "filter pressed 2 held 2"
	grep -qE "plugin filterer: done: .*, rows 0, " "$log" || fail "B left screen $view while taken: $(grep "plugin filterer: done" "$log")"
done
if [ -f "$WORK/minuisettings.txt.saved" ]; then cp "$WORK/minuisettings.txt.saved" "$SETTINGS"; else rm -f "$SETTINGS"; fi
rm -f "$WORK/minuisettings.txt.saved"

# in a confirm dialog, A says yes, unless filterer takes it
printf 'press=a\nhold=100\nconfirm=1\n' >"$STATE/filterer.cfg"
run_launcher "$WORK/filterer-confirm.log" "plugin filterer: done" || fail "the launcher exited early with a dialog"
log="$WORK/filterer-confirm.log"
expect "filter pressed 1 held 1"
grep -qE "plugin filterer: done: .*, yes 1, " "$log" || fail "A didn't say yes: $(grep "plugin filterer: done" "$log")"
printf 'press=a\nhold=100\nconfirm=1\ntake=a\n' >"$STATE/filterer.cfg"
run_launcher "$WORK/filterer-confirm-take.log" "plugin filterer: done" || fail "the launcher exited early with a dialog taking A"
log="$WORK/filterer-confirm-take.log"
grep -qE "plugin filterer: done: .*, yes 0, " "$log" || fail "A said yes while taken: $(grep "plugin filterer: done" "$log")"

# a power press filterer takes doesn't sleep
printf 'press=power\nhold=200\ntake=all\n' >"$STATE/filterer.cfg"
run_launcher "$WORK/filterer-tap.log" "plugin filterer: done" || fail "the launcher exited early with a taken power tap"
log="$WORK/filterer-tap.log"
expect "filter pressed 8000 held 8000"
grep -qE "plugin filterer: done: .*, sleeps 0$" "$log" || fail "a taken power tap slept: $(grep "plugin filterer: done" "$log")"
grep -qF "Entering hybrid sleep" "$log" && fail "a taken power tap slept"
grep -qF "before_poweroff" "$log" && fail "a taken power tap powered off"

# but held for a second it powers off all the same, through the host
printf 'press=power\ntake=all\n' >"$STATE/filterer.cfg"
run_launcher "$WORK/filterer-hold.log" "plugin filterer: before_poweroff"
log="$WORK/filterer-hold.log"
expect "before_poweroff, held a second 1"
grep -qF "plugins: power held through an input filter, powering off" "$log" || fail "the host didn't power off for a taken long press"
grep -qF "plugin filterer: before_sleep" "$log" && fail "a taken long press ran before_sleep"

# and with nothing taken, stock powers off for it itself
printf 'press=power\n' >"$STATE/filterer.cfg"
run_launcher "$WORK/filterer-hold-stock.log" "plugin filterer: before_poweroff"
log="$WORK/filterer-hold-stock.log"
expect "before_poweroff, held a second 1"
grep -qF "plugins: power held through an input filter" "$log" && fail "the host powered off for a press stock had"

# stock ignores a power press in the first second after a wake, so a held
# press filterer takes then doesn't power off either, but one after does
printf 'press=power\ntake=all\nwake=200\n' >"$STATE/filterer.cfg"
run_launcher "$WORK/filterer-hold-woke.log" "plugin filterer: done" || fail "the launcher exited early with a taken hold after a wake"
log="$WORK/filterer-hold-woke.log"
expect "pressing "
grep -qE "plugin filterer: done: .*, sleeps 1$" "$log" || fail "filterer's tap didn't sleep once: $(grep "plugin filterer: done" "$log")"
grep -qF "plugin filterer: before_poweroff" "$log" && fail "a taken hold just after a wake powered off"
grep -qF "plugins: power held through an input filter" "$log" && fail "the host powered off for a taken hold just after a wake"
printf 'press=power\ntake=all\nwake=1200\n' >"$STATE/filterer.cfg"
run_launcher "$WORK/filterer-hold-later.log" "plugin filterer: before_poweroff"
log="$WORK/filterer-hold-later.log"
expect "before_poweroff, held a second 1"
grep -qF "plugins: power held through an input filter, powering off" "$log" || fail "the host didn't power off for a taken hold a second after a wake"
# as stock does with nothing taken
printf 'press=power\nwake=200\n' >"$STATE/filterer.cfg"
run_launcher "$WORK/filterer-hold-woke-stock.log" "plugin filterer: done" || fail "the launcher exited early with a hold after a wake"
log="$WORK/filterer-hold-woke-stock.log"
grep -qE "plugin filterer: done: .*, sleeps 1$" "$log" || fail "filterer's tap didn't sleep once: $(grep "plugin filterer: done" "$log")"
grep -qF "plugin filterer: before_poweroff" "$log" && fail "stock powered off for a hold just after a wake"
rm -rf "$WORK/plugins/filterer.plugin" "$WORK/plugins/sifter.plugin" "$STATE/filterer.cfg" "$STATE/plugins.txt"
