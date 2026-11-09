# shellcheck shell=sh
# shard: 1

# gridder owns the layout slot and draws the game list as a grid on the
# screen it's lent, inside the area above the hints; its input moves the
# selection through the launcher, which keeps it
mkdir -p "$STATE"
printf 'gridder=on\n' >"$STATE/plugins.txt"
build_plugin gridder 'id=gridder\nname=Gridder\napi=1\nslots=layout\n'
run_launcher "$WORK/gridder.log" "plugin gridder: renders" || fail "the launcher exited early with gridder loaded"
log="$WORK/gridder.log"
plugin=gridder
grep -qF "plugins: gridder draws the game list" "$log" || fail "gridder didn't take the game list"
# on the root, whose count depends on the card
grep -qE "plugin gridder: layout_init count [0-9]+ selected 0 depth 1, area 0,0 1024x663, status inside 1, view size 1$" "$log" \
	|| fail "gridder's first view is wrong: $(grep "plugin gridder: layout_init" "$log")"
expect "input first: pressed 0 repeated 0 released 0 held 0 consumed 0, input size 1"
expect "render depth 1 count "
expect "render clipped 1 (fill 0), select -1, free -1"
expect "render decorated 0, bad index -1 -1, no rect -1, outside -1, not the screen -1"
expect "render header 0x0, beside status 1"
expect "after render fill -1, decorate -1"
expect "open folder 1"
expect "render depth 2 count 7 selected 0 Apple, window 0-7 of 7 rows, letter 0 of 7, names 1, art 1 \"\""
expect "render lit FF0000, background 000080"
expect "input move 0 to 4: 0, bad -1, now 4"
expect "render depth 2 count 7 selected 4 Elder, window 0-7 of 7 rows, letter 4 of 7, names 1, art 1 \"\""
grep -qF "plugins: gridder's layout" "$log" && fail "gridder's layout was turned off"
[ "$(grep -c "plugin gridder: layout_init" "$log")" -eq 1 ] || fail "layout_init didn't run once"

# its errors hand the game list back to the built-in list, which keeps
# drawing; the plugin's other hooks keep running
for hook in init input render; do
	echo "fail=$hook" >"$STATE/gridder.cfg"
	run_launcher "$WORK/gridder-$hook.log" "plugin gridder: renders" || fail "the launcher exited early with gridder's $hook failing"
	log="$WORK/gridder-$hook.log"
	case "$hook" in
	init) grep -qF "plugins: gridder declined the layout slot (layout_init returned 1), using the built-in list" "$log" || fail "the declined layout wasn't logged" ;;
	*) grep -qF "plugins: gridder's layout_$hook returned -1, using the built-in list until the launcher restarts" "$log" || fail "the failed layout_$hook wasn't logged" ;;
	esac
	expect "open folder 1"
	# how many times each hook ran by the 60th tick: none after the error
	case "$hook" in
	init) want="renders 0, inputs 0" ;;
	input) want="renders [0-9]+, inputs 3" ;;
	render) want="renders 3, inputs [0-9]+" ;;
	esac
	grep -qE "plugin gridder: $want\$" "$log" || fail "gridder's hooks didn't stop at the error: $(grep "plugin gridder: renders" "$log")"
done
rm -f "$STATE/gridder.cfg"

# another plugin can't have the slot while gridder does, and the one
# plugins.txt picks wins it; an owner without layout_render leaves the
# built-in list
build_plugin hello 'id=hello\nname=Hello\napi=1\nslots=layout\n'
rm -f "$STATE/hello.cfg" # an earlier case's greeting
printf 'gridder=on\nhello=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/gridder-rival.log" "plugin gridder: renders" || fail "the launcher exited early with two layouts"
grep -qF "plugins: not loading hello: the layout slot belongs to gridder" "$WORK/gridder-rival.log" || fail "the layout slot conflict wasn't logged"
printf 'gridder=on\nhello=on\nslot.layout=hello\n' >"$STATE/plugins.txt"
run_launcher "$WORK/gridder-picked.log" "plugin hello: hello from desktop" "plugins: hello owns the layout slot but has no layout_render" || fail "the launcher exited early with hello picked"
log="$WORK/gridder-picked.log"
grep -qF "plugins: not loading gridder: the layout slot belongs to hello" "$log" || fail "the picked owner didn't win the layout slot"
grep -qF "plugin gridder:" "$log" && fail "gridder ran without the layout slot"
rm -rf "$WORK/plugins/hello.plugin" "$STATE/hello.cfg"

# without slots=layout it isn't asked to draw
printf 'id=gridder\nname=Gridder\napi=1\n' >"$WORK/plugins/gridder.plugin/plugin.txt"
printf 'gridder=on\n' >"$STATE/plugins.txt"
run_launcher "$WORK/gridder-unslotted.log" "plugin gridder: renders" || fail "the launcher exited early with gridder unslotted"
log="$WORK/gridder-unslotted.log"
grep -qF "plugins: gridder has a layout but no slots=layout, so it won't be called" "$log" || fail "the missing layout slot wasn't reported"
expect "renders 0, inputs 0"

# a layout that crashes drawing the first frame pauses plugins on the next
# start, as any crash while starting does
printf 'id=gridder\nname=Gridder\napi=1\nslots=layout\n' >"$WORK/plugins/gridder.plugin/plugin.txt"
echo "fail=crash" >"$STATE/gridder.cfg"
run_launcher "$WORK/gridder-crash.log" && fail "gridder's crashing render didn't crash the launcher"
grep -qF "plugin gridder: crashing in render" "$WORK/gridder-crash.log" || fail "gridder's render didn't run before the crash"
start_launcher "$WORK/gridder-after-crash.log" "pausing them" || fail "the launcher crashed again instead of pausing plugins"
grep -q "pausing them" "$WORK/gridder-after-crash.log" || fail "plugins weren't paused after gridder's crash"
rm -rf "$WORK/plugins/gridder.plugin" "$STATE/gridder.cfg" "$STATE/plugins.txt"
