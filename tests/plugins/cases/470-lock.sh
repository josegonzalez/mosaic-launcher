# shellcheck shell=sh
# shard: 3

# the launcher's built-in lock: with its chord set on the Launcher page,
# holding SELECT and START locks every button, so locker's down doesn't
# move the selection and its own filter sees no presses; holding them
# again unlocks, and the chord's releases don't reach the game list, so the
# next down moves it. Locked on the game list, the lock screen opens, and
# it closes once unlocked. Desktop has no hold switch, which the launcher
# logs once
build_plugin locker 'id=locker\nname=Locker\napi=1\n'
printf 'locker=on\n' >"$STATE/plugins.txt"
printf 'lock_chord=select+start\nlock_switch=on\n' >"$STATE/settings.txt"
run_launcher "$WORK/lock.log" "plugin locker: done" || fail "the launcher exited early with the lock on"
log="$WORK/lock.log"
[ "$(grep -cF "lock: unsupported on desktop: no hold switch, so only the chord locks" "$log")" -eq 1 ] \
	|| fail "the lock didn't log the missing hold switch once"
grep -qF "lock: SELECT+START held 1s locks and unlocks, and the hold switch locks" "$log" \
	|| fail "the lock didn't log its settings"
for line in "lock: locked by the chord" "plugins: opened ambient screen lock after " "lock: lock screen " \
	"lock: unlocked by the chord" "lock: closing the lock screen"; do
	grep -qF "$line" "$log" || fail "the lock didn't log \"$line\""
done
grep -qF ", Hold SELECT+START to unlock" "$log" \
	|| fail "the lock screen didn't say how to unlock: $(grep "lock: lock screen" "$log" | head -n 1)"
grep -qF "lock: locked by the switch" "$log" && fail "the lock locked by a switch desktop doesn't have"
indexes=$(sed -n 's/.*plugin locker: done: index \([0-9-]*\) \([0-9-]*\) \([0-9-]*\),.*/\1 \2 \3/p' "$log")
# shellcheck disable=SC2086
set -- $indexes
if [ "$#" -ne 3 ]; then
	fail "locker didn't finish: $(grep "plugin locker: done" "$log")"
elif [ "$2" -ne "$1" ]; then
	fail "down moved the selection while locked: $indexes"
elif [ "$3" -ne $(($1 + 1)) ]; then
	fail "down didn't move the selection once unlocked: $indexes"
fi
grep -qE "plugin locker: done: .*, locked presses 0, after presses [1-9][0-9]*, leaked 0$" "$log" \
	|| fail "the lock let presses through while locked, or the chord's releases after: $(grep "plugin locker: done" "$log")"

# while locked, the padlock shows left of the status pill in the theme's
# colours, which spotter reads back
build_plugin spotter 'id=spotter\nname=Spotter\napi=1\n'
printf 'locker=on\nspotter=on\n' >"$STATE/plugins.txt"
printf 'unlock=0\n' >"$STATE/locker.cfg"
printf 'lock_chord=select+start\n' >"$STATE/settings.txt"
run_launcher "$WORK/lock-widget.log" "plugin locker: done" "lock: padlock at " \
	|| fail "the launcher exited early with the lock's padlock"
log="$WORK/lock-widget.log"
grep -qE "lock: padlock at [0-9]+,15 90x90$" "$log" \
	|| fail "the lock's padlock is wrong: $(grep "lock: padlock" "$log")"
grep -qF "lock: unlocked" "$log" && fail "the lock unlocked without the chord"
hl_x=$(sed -n 's/.*lock: padlock at \([0-9]*\),.*/\1/p' "$log" | tail -n 1)
runs=$(grep "plugin spotter: runs " "$log" | tail -n 1 | sed 's/.*: runs //; s/,.*//; s/-/ /g')
# shellcheck disable=SC2086
set -- $runs
if [ -z "$hl_x" ] || [ "$#" -lt 4 ]; then
	fail "spotter didn't see the lock's padlock and the status pill: $(grep "plugin spotter: runs" "$log" | tail -n 1)"
elif [ $(($1 - hl_x)) -gt 2 ] || [ $((hl_x - $1)) -gt 2 ] || [ $(($2 - hl_x - 90)) -gt 2 ] || [ $((hl_x + 90 - $2)) -gt 2 ]; then
	fail "the lock didn't draw at $hl_x, 90 wide: spotter saw $runs"
fi
grep "plugin spotter: runs " "$log" | tail -n 1 | grep -qF ", accent 1 hint 1" \
	|| fail "the lock's padlock isn't in the theme's colours: $(grep "plugin spotter: runs" "$log" | tail -n 1)"
rm -rf "$WORK/plugins/locker.plugin" "$WORK/plugins/spotter.plugin" "$STATE/locker.cfg" "$STATE/plugins.txt" \
	"$STATE/settings.txt"
