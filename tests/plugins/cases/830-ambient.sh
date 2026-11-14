# shellcheck shell=sh
# shard: 1

# ambient screens and the lock: ambler registers two ambient screens that
# both want to show after a second idle, and the user's order puts its
# second before the lock and its first after, so its second opens and its
# first never does. It then asks the launcher to lock, which opens the
# lock screen with ambler's lines; X there runs ambler's ping, and once
# it's been idle a second, ambler's second, ahead of the lock, opens over
# the lock screen, still locked. A screen its ambient= doesn't list,
# another plugin's, one already ambient, a button the lock screen doesn't
# take, an unknown action and a button already used are refused
build_plugin ambler 'id=ambler\nname=Ambler\napi=1\nambient=ambler.first,ambler.second\n'
printf 'ambler=on\n' >"$STATE/plugins.txt"
printf 'lock_chord=select+start\nambient_order=ambler.second,lock,ambler.first\n' >"$STATE/settings.txt"
run_launcher "$WORK/ambient.log" "plugin ambler: done" || fail "the launcher exited early with ambler"
log="$WORK/ambient.log"
plugin=ambler
expect "refusals: -1 -1 -1 -1 -1 -1"
grep -qF "plugins: ambient screens in order: ambler.second, lock, ambler.first" "$log" \
	|| fail "the ambient screens aren't in the user's order: $(grep "ambient screens in order" "$log")"
expect "lock 0, locked 1"
grep -qF "| Ambler says hi | second line" "$log" \
	|| fail "the lock screen didn't show ambler's lines: $(grep "lock: lock screen" "$log" | head -n 1)"
expect "pinged on lock, locked 1"
expect "done: ambler.second over the lock, locked 1, pinged 1, entered first 0 second 2"
grep -qF "plugin ambler: stuck" "$log" && fail "ambler got stuck: $(grep "plugin ambler: stuck" "$log")"

# without the user's order, the lock comes first, then the rest as they
# load, so ambler's first opens before its second
printf 'lock_chord=select+start\n' >"$STATE/settings.txt"
run_launcher "$WORK/ambient-default.log" "plugin ambler: entered ambler.first" \
	|| fail "the launcher exited early with ambler and the default order"
log="$WORK/ambient-default.log"
grep -qF "plugins: ambient screens in order: lock, ambler.first, ambler.second" "$log" \
	|| fail "the ambient screens aren't in the default order: $(grep "ambient screens in order" "$log")"
grep -qF "plugin ambler: entered ambler.second" "$log" && fail "ambler's second opened before its first"

# the Launcher page lists the lock and ambler's two in that order, and
# moves one (A, down, A); right on Lock chord picks the next chord. Up from
# the first row goes round to the last, Turn on or off, so it doesn't
# matter whether Try again shows
PRESSER="$WORK/presser.so"
if [ "$(uname -s)" = Darwin ]; then
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror -undefined dynamic_lookup "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" \
		|| fail "building presser"
else
	"$CC" -shared -fPIC -std=gnu99 -Wall -Werror "$ROOT/tests/plugins/presser/presser.c" -o "$PRESSER" -ldl || fail "building presser"
fi
printf 'lock_chord=select+start\nambient_order=ambler.second,lock,ambler.first\n' >"$STATE/settings.txt"
case_log="$WORK/ambient-page.log"
texts="$WORK/ambient-page.texts"
: >"$texts"
PRESSER_KEYS="up up a a down a b up up up right" PRESSER_LOG="$texts" SMOKE_PRELOAD="$PRESSER" SMOKE_UNTIL="presser: done" \
	MOSAIC_LAUNCHER_PAK_DIR="$ROOT/pak" MOSAIC_LAUNCHER_PLUGIN_PATH="$WORK/plugins" \
	"$ROOT/scripts/desktop-smoke.sh" "$case_log" 40 --settings >/dev/null 2>"$WORK/smoke.err" \
	|| fail "the Launcher page didn't run its presses"
log="$case_log"
for text in "Lock chord" "Hold the chord for (seconds)" "Lock with the hold switch" "Ambient screens" "Lock" \
	"Ambler (first)" "Ambler (second)" "Moving"; do
	drew "$text"
done
grep -qxF "ambient_order=lock,ambler.second,ambler.first" "$STATE/settings.txt" \
	|| fail "moving ambler's second down didn't save the order: $(cat "$STATE/settings.txt")"
grep -qxF "lock_chord=l1+r1" "$STATE/settings.txt" \
	|| fail "right on Lock chord didn't pick the next chord: $(cat "$STATE/settings.txt")"
rm -rf "$WORK/plugins/ambler.plugin" "$STATE/plugins.txt" "$STATE/settings.txt"
