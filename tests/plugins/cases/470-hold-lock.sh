# shellcheck shell=sh
# shard: 3

# the bundled hold-lock: holding SELECT and START locks every button, so
# locker's down doesn't move the selection and its own filter, after
# hold-lock's, sees no presses; holding them again unlocks, and the
# chord's releases don't reach the game list, so the next down moves it.
# Desktop has no hold switch, which hold-lock logs once
make -s -C "$ROOT/plugins" ARCH="$ARCH" OUT="$WORK/plugins" CC="$CC" hold-lock >/dev/null || fail "building hold-lock"
build_plugin locker 'id=locker\nname=Locker\napi=1\n'
printf 'hold-lock=on\nlocker=on\n' >"$STATE/plugins.txt"
printf 'switch=on\n' >"$STATE/hold-lock.cfg"
run_launcher "$WORK/hold-lock.log" "plugin locker: done" || fail "the launcher exited early with hold-lock loaded"
log="$WORK/hold-lock.log"
plugin=hold-lock
[ "$(grep -cF "plugin hold-lock: unsupported on desktop: no hold switch, so only the chord locks" "$log")" -eq 1 ] \
	|| fail "hold-lock didn't log the missing hold switch once"
expect "locked by the chord"
expect "unlocked by the chord"
grep -qF "plugin hold-lock: locked by the switch" "$log" && fail "hold-lock locked by a switch desktop doesn't have"
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
	|| fail "hold-lock let presses through while locked, or the chord's releases after: $(grep "plugin locker: done" "$log")"

# while locked, the padlock shows left of the status pill in the theme's
# colours, which spotter, after it in load order, reads back
build_plugin spotter 'id=spotter\nname=Spotter\napi=1\n'
printf 'hold-lock=on\nlocker=on\nspotter=on\n' >"$STATE/plugins.txt"
printf 'unlock=0\n' >"$STATE/locker.cfg"
rm -f "$STATE/hold-lock.cfg"
run_launcher "$WORK/hold-lock-widget.log" "plugin locker: done" "plugin hold-lock: padlock at " \
	|| fail "the launcher exited early with hold-lock's padlock"
log="$WORK/hold-lock-widget.log"
grep -qE "plugin hold-lock: padlock at [0-9]+,15 90x90$" "$log" \
	|| fail "hold-lock's padlock is wrong: $(grep "plugin hold-lock: padlock" "$log")"
grep -qF "plugin hold-lock: unlocked" "$log" && fail "hold-lock unlocked without the chord"
hl_x=$(sed -n 's/.*plugin hold-lock: padlock at \([0-9]*\),.*/\1/p' "$log" | tail -n 1)
runs=$(grep "plugin spotter: runs " "$log" | tail -n 1 | sed 's/.*: runs //; s/,.*//; s/-/ /g')
# shellcheck disable=SC2086
set -- $runs
if [ -z "$hl_x" ] || [ "$#" -lt 4 ]; then
	fail "spotter didn't see hold-lock's padlock and the status pill: $(grep "plugin spotter: runs" "$log" | tail -n 1)"
elif [ $(($1 - hl_x)) -gt 2 ] || [ $((hl_x - $1)) -gt 2 ] || [ $(($2 - hl_x - 90)) -gt 2 ] || [ $((hl_x + 90 - $2)) -gt 2 ]; then
	fail "hold-lock didn't draw at $hl_x, 90 wide: spotter saw $runs"
fi
grep "plugin spotter: runs " "$log" | tail -n 1 | grep -qF ", accent 1 hint 1" \
	|| fail "hold-lock's padlock isn't in the theme's colours: $(grep "plugin spotter: runs" "$log" | tail -n 1)"
grep -qF "plugins: hold-lock's" "$log" && fail "hold-lock's widget or filter was turned off"
rm -rf "$WORK/plugins/hold-lock.plugin" "$WORK/plugins/locker.plugin" "$WORK/plugins/spotter.plugin" \
	"$STATE/hold-lock.cfg" "$STATE/locker.cfg" "$STATE/plugins.txt"
